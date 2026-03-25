#!/bin/bash
# Create a BIOS-only DOS-style text installer partition image.

set -e

BUILD_DIR="${1:-build/x86_64}"
IMAGE_DIR="${2:-image}"
IMAGE_NAME="${IMAGE_NAME:-os-x86_64-dos-installer.img}"
IMAGE_SIZE_MB="${IMAGE_SIZE_MB:-16}"
DOS_SYSTEM_IMAGE="${DOS_SYSTEM_IMAGE:-${IMAGE_DIR}/os-x86_64-system.img}"
DOS_INSTALLER_COM="${DOS_INSTALLER_COM:-${BUILD_DIR}/boot/OSINST.COM}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FREEDOS_BOOT_IMAGE="${FREEDOS_BOOT_IMAGE:-${ROOT_DIR}/boot/bios/freedos/fdboot.img}"

GREEN='\033[0;32m'
NC='\033[0m'

log() {
    echo -e "${GREEN}[DOS-INSTALLER]${NC} $1"
}

require_file() {
    if [ ! -f "$1" ]; then
        echo "[ERROR] Required file not found: $1" >&2
        exit 1
    fi
}

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "[ERROR] Required command not found: $1" >&2
        exit 1
    fi
}

mkdir -p "$IMAGE_DIR"

STAGE1="$BUILD_DIR/boot/installer_stage1.bin"
STAGE2="$BUILD_DIR/boot/installer_stage2.bin"
IMAGE_PATH="$IMAGE_DIR/$IMAGE_NAME"

require_file "$STAGE1"
require_file "$STAGE2"

if [ -f "$DOS_SYSTEM_IMAGE" ]; then
    SYSTEM_IMAGE_ENABLED=1
    SYSTEM_IMAGE_SIZE=$(stat -c%s "$DOS_SYSTEM_IMAGE")
    SYSTEM_IMAGE_SECTORS=$(((SYSTEM_IMAGE_SIZE + 511) / 512))
    REQUIRED_MB=$((((SYSTEM_IMAGE_SIZE + (32 * 1024 * 1024) + 1048575)) / 1048576))
    if [ "$REQUIRED_MB" -gt "$IMAGE_SIZE_MB" ]; then
        IMAGE_SIZE_MB="$REQUIRED_MB"
    fi
else
    SYSTEM_IMAGE_ENABLED=0
    SYSTEM_IMAGE_SIZE=0
    SYSTEM_IMAGE_SECTORS=0
fi

FREEDOS_ENABLED=0
if [ -f "$FREEDOS_BOOT_IMAGE" ]; then
    FREEDOS_ENABLED=1
fi

build_freedos_installer_image() {
    require_file "$FREEDOS_BOOT_IMAGE"
    require_file "$DOS_INSTALLER_COM"
    require_file "$DOS_SYSTEM_IMAGE"
    require_cmd mcopy
    require_cmd mdir

    local template_size
    template_size=$(stat -c%s "$FREEDOS_BOOT_IMAGE")
    local required_size=$((IMAGE_SIZE_MB * 1024 * 1024))

    if [ "$template_size" -lt "$required_size" ]; then
        echo "[ERROR] FreeDOS boot image template is too small: $FREEDOS_BOOT_IMAGE" >&2
        echo "        Template size: ${template_size} bytes" >&2
        echo "        Required size: ${required_size} bytes" >&2
        echo "        Provide a larger FreeDOS HDD image or lower IMAGE_SIZE_MB." >&2
        exit 1
    fi

    log "Creating FreeDOS-based installer image at $IMAGE_PATH"
    cp "$FREEDOS_BOOT_IMAGE" "$IMAGE_PATH"

    cat > "$BUILD_DIR/boot/AUTOEXEC.BAT" <<'EOF'
@ECHO OFF
CLS
ECHO OS8 DOS Installer
ECHO.
OSINST.COM
ECHO.
ECHO Installer exited. Press CTRL+ALT+DEL to reboot.
COMMAND.COM
EOF

    cat > "$BUILD_DIR/boot/CONFIG.SYS" <<'EOF'
SHELL=COMMAND.COM /P
FILES=20
BUFFERS=20
DOS=HIGH
EOF

    log "Seeding FreeDOS boot image with installer files"
    mcopy -o -i "$IMAGE_PATH" "$DOS_INSTALLER_COM" ::/OSINST.COM
    mcopy -o -i "$IMAGE_PATH" "$DOS_SYSTEM_IMAGE" ::/OSSYS.IMG
    mcopy -o -i "$IMAGE_PATH" "$BUILD_DIR/boot/AUTOEXEC.BAT" ::/AUTOEXEC.BAT
    mcopy -o -i "$IMAGE_PATH" "$BUILD_DIR/boot/CONFIG.SYS" ::/CONFIG.SYS

    log "FreeDOS image contents:"
    mdir -i "$IMAGE_PATH" ::/
    log "Image created successfully: $IMAGE_PATH"
    ls -lh "$IMAGE_PATH"
    echo ""
    log "QEMU test: qemu-system-i386 -drive format=raw,file=$IMAGE_PATH"
    log "QEMU x86_64 BIOS test: qemu-system-x86_64 -drive format=raw,file=$IMAGE_PATH"
}

if [ "$FREEDOS_ENABLED" -eq 1 ]; then
    build_freedos_installer_image
    exit 0
fi

STAGE2_SIZE=$(stat -c%s "$STAGE2")
STAGE2_SECTORS=$(((STAGE2_SIZE + 511) / 512))
IMAGE_TOTAL_SECTORS=$(((IMAGE_SIZE_MB * 1024 * 1024) / 512))
RESERVED_SECTORS=$((1 + STAGE2_SECTORS))
ROOT_ENTRIES=32
ROOT_DIR_SECTORS=$((((ROOT_ENTRIES * 32) + 511) / 512))
FAT_COUNT=1
STAGE2_PADDED="$BUILD_DIR/boot/installer_stage2.padded.bin"
STAGE1_PATCHED="$BUILD_DIR/boot/installer_stage1.patched.bin"

if [ "$IMAGE_TOTAL_SECTORS" -le 32768 ]; then
    SECTORS_PER_CLUSTER=4
elif [ "$IMAGE_TOTAL_SECTORS" -le 262144 ]; then
    SECTORS_PER_CLUSTER=8
else
    SECTORS_PER_CLUSTER=16
fi

FAT_SECTORS=1
while true; do
    DATA_SECTORS=$((IMAGE_TOTAL_SECTORS - RESERVED_SECTORS - ROOT_DIR_SECTORS - FAT_COUNT * FAT_SECTORS))
    if [ "$DATA_SECTORS" -le 0 ]; then
        echo "[ERROR] DOS installer image geometry is invalid" >&2
        exit 1
    fi
    CLUSTER_COUNT=$((DATA_SECTORS / SECTORS_PER_CLUSTER))
    REQUIRED_FAT_SECTORS=$((((CLUSTER_COUNT + 2) * 2 + 511) / 512))
    if [ "$REQUIRED_FAT_SECTORS" -eq "$FAT_SECTORS" ]; then
        break
    fi
    FAT_SECTORS="$REQUIRED_FAT_SECTORS"
done

log "Creating DOS-style installer volume image at $IMAGE_PATH"
dd if=/dev/zero of="$IMAGE_PATH" bs=1M count="$IMAGE_SIZE_MB" 2>/dev/null

cp "$STAGE1" "$STAGE1_PATCHED"
python3 - "$STAGE1_PATCHED" "$STAGE2_SECTORS" "$IMAGE_TOTAL_SECTORS" "$RESERVED_SECTORS" "$SECTORS_PER_CLUSTER" "$ROOT_ENTRIES" "$FAT_SECTORS" <<'PATCHPY'
import sys
path = sys.argv[1]
stage2_sectors = int(sys.argv[2])
image_total_sectors = int(sys.argv[3])
reserved_sectors = int(sys.argv[4])
sectors_per_cluster = int(sys.argv[5])
root_entries = int(sys.argv[6])
fat_sectors = int(sys.argv[7])
total_sectors_16 = image_total_sectors if image_total_sectors <= 0xFFFF else 0
total_sectors_32 = 0 if total_sectors_16 else image_total_sectors
with open(path, "r+b") as f:
    data = bytearray(f.read())
    marker = b"\x10\x00\x00\x7E\x00\x00"
    off = data.find(marker)
    if off == -1:
        raise SystemExit("stage2 sector-count marker not found in stage1 image")
    data[off:off+2] = stage2_sectors.to_bytes(2, "little")
    data[11:13] = (512).to_bytes(2, "little")
    data[13] = sectors_per_cluster
    data[14:16] = reserved_sectors.to_bytes(2, "little")
    data[16] = 1
    data[17:19] = root_entries.to_bytes(2, "little")
    data[19:21] = total_sectors_16.to_bytes(2, "little")
    data[21] = 0xF8
    data[22:24] = fat_sectors.to_bytes(2, "little")
    data[24:26] = (63).to_bytes(2, "little")
    data[26:28] = (255).to_bytes(2, "little")
    data[32:36] = image_total_sectors.to_bytes(4, "little")
    data[32:36] = total_sectors_32.to_bytes(4, "little")
    data[36] = 0x80
    data[37] = 0
    data[38] = 0x29
    data[39:43] = (0x4F534E58).to_bytes(4, "little")
    data[43:54] = b"OSKSETUP   "
    data[54:62] = b"FAT16   "
    f.seek(0)
    f.write(data)
PATCHPY
cp "$STAGE2" "$STAGE2_PADDED"
if [ "$SYSTEM_IMAGE_ENABLED" -eq 1 ]; then
python3 - "$STAGE2_PADDED" "$RESERVED_SECTORS" "$FAT_SECTORS" "$ROOT_DIR_SECTORS" "$SYSTEM_IMAGE_SECTORS" <<'PATCHPY'
import sys
path = sys.argv[1]
reserved = int(sys.argv[2])
fat_sectors = int(sys.argv[3])
root_dir_sectors = int(sys.argv[4])
system_image_sectors = int(sys.argv[5])
start_lba = reserved + fat_sectors + root_dir_sectors
start_marker = (0x13572468).to_bytes(4, "little")
count_marker = (0x24681357).to_bytes(4, "little")
with open(path, "r+b") as f:
    data = bytearray(f.read())
    off = data.find(start_marker)
    if off == -1:
        raise SystemExit("system image start marker not found in stage2 image")
    data[off:off+4] = start_lba.to_bytes(4, "little")
    off = data.find(count_marker)
    if off == -1:
        raise SystemExit("system image sector-count marker not found in stage2 image")
    data[off:off+4] = system_image_sectors.to_bytes(4, "little")
    f.seek(0)
    f.write(data)
PATCHPY
fi
truncate -s $((STAGE2_SECTORS * 512)) "$STAGE2_PADDED"

log "Writing stage 1 volume boot sector"
dd if="$STAGE1_PATCHED" of="$IMAGE_PATH" bs=512 count=1 conv=notrunc 2>/dev/null

log "Writing stage 2 text installer ($STAGE2_SECTORS sectors)"
dd if="$STAGE2_PADDED" of="$IMAGE_PATH" bs=512 seek=1 conv=notrunc 2>/dev/null

log "Seeding FAT16 metadata"
python3 - "$IMAGE_PATH" "$RESERVED_SECTORS" "$FAT_SECTORS" "$ROOT_DIR_SECTORS" <<'PATCHPY'
import sys

path = sys.argv[1]
reserved = int(sys.argv[2])
fat_sectors = int(sys.argv[3])
root_dir_sectors = int(sys.argv[4])

with open(path, "r+b") as f:
    f.seek(reserved * 512)
    fat = bytearray(fat_sectors * 512)
    fat[:4] = b"\xF8\xFF\xFF\xFF"
    f.write(fat)
    f.seek((reserved + fat_sectors) * 512)
    f.write(b"\x00" * (root_dir_sectors * 512))
PATCHPY

if [ "$SYSTEM_IMAGE_ENABLED" -eq 1 ]; then
    SYSTEM_IMAGE_START_LBA=$((RESERVED_SECTORS + FAT_SECTORS + ROOT_DIR_SECTORS))
    log "Embedding regular system image payload at LBA $SYSTEM_IMAGE_START_LBA ($SYSTEM_IMAGE_SECTORS sectors)"
    dd if="$DOS_SYSTEM_IMAGE" of="$IMAGE_PATH" bs=512 seek="$SYSTEM_IMAGE_START_LBA" conv=notrunc 2>/dev/null
fi

log "Image created successfully: $IMAGE_PATH"
log "Installer image size: $IMAGE_SIZE_MB MiB ($IMAGE_TOTAL_SECTORS sectors)"
log "FAT16 layout: reserved=$RESERVED_SECTORS spc=$SECTORS_PER_CLUSTER fat_sectors=$FAT_SECTORS root_entries=$ROOT_ENTRIES"
if [ "$SYSTEM_IMAGE_ENABLED" -eq 1 ]; then
    log "Bundled regular system image: $(basename "$DOS_SYSTEM_IMAGE")"
fi
ls -lh "$IMAGE_PATH"
echo ""
log "QEMU test: qemu-system-i386 -drive format=raw,file=$IMAGE_PATH"
log "QEMU x86_64 BIOS test: qemu-system-x86_64 -drive format=raw,file=$IMAGE_PATH"
