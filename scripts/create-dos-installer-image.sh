#!/bin/bash
# Create a BIOS-only DOS-style text installer partition image.

set -e

BUILD_DIR="${1:-build/x86_64}"
IMAGE_DIR="${2:-image}"
IMAGE_NAME="${IMAGE_NAME:-os-x86_64-dos-installer.img}"
IMAGE_SIZE_MB="${IMAGE_SIZE_MB:-16}"

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

mkdir -p "$IMAGE_DIR"

STAGE1="$BUILD_DIR/boot/installer_stage1.bin"
STAGE2="$BUILD_DIR/boot/installer_stage2.bin"
IMAGE_PATH="$IMAGE_DIR/$IMAGE_NAME"

require_file "$STAGE1"
require_file "$STAGE2"

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
with open(path, "r+b") as f:
    data = bytearray(f.read())
    marker = b"\x10\x00\x00\x7E\x00\x00"
    off = data.find(marker)
    if off == -1:
        raise SystemExit("stage2 sector-count marker not found in stage1 image")
    data[off:off+2] = stage2_sectors.to_bytes(2, "little")
    data[13] = sectors_per_cluster
    data[14:16] = reserved_sectors.to_bytes(2, "little")
    data[17:19] = root_entries.to_bytes(2, "little")
    data[22:24] = fat_sectors.to_bytes(2, "little")
    data[32:36] = image_total_sectors.to_bytes(4, "little")
    f.seek(0)
    f.write(data)
PATCHPY
cp "$STAGE2" "$STAGE2_PADDED"
python3 - "$STAGE2_PADDED" "$IMAGE_TOTAL_SECTORS" <<'PATCHPY'
import sys
path = sys.argv[1]
sector_count = int(sys.argv[2])
marker = (0x0BADF00D).to_bytes(4, "little")
replacement = sector_count.to_bytes(4, "little")
with open(path, "r+b") as f:
    data = bytearray(f.read())
    off = data.find(marker)
    if off == -1:
        raise SystemExit("image sector-count marker not found in stage2 image")
    data[off:off+4] = replacement
    f.seek(0)
    f.write(data)
PATCHPY
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

log "Image created successfully: $IMAGE_PATH"
log "Installer image size: $IMAGE_SIZE_MB MiB ($IMAGE_TOTAL_SECTORS sectors)"
log "FAT16 layout: reserved=$RESERVED_SECTORS spc=$SECTORS_PER_CLUSTER fat_sectors=$FAT_SECTORS root_entries=$ROOT_ENTRIES"
ls -lh "$IMAGE_PATH"
echo ""
log "QEMU test: qemu-system-i386 -drive format=raw,file=$IMAGE_PATH"
log "QEMU x86_64 BIOS test: qemu-system-x86_64 -drive format=raw,file=$IMAGE_PATH"
