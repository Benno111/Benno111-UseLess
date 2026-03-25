#!/bin/bash
# Create a standalone BIOS-bootable ISO for the DOS-based setup using FreeDOS.

set -e

BUILD_DIR="${1:-build/x86_64}"
IMAGE_DIR="${2:-image}"
ISO_NAME="${ISO_NAME:-os-x86_64-dos-installer.iso}"
ISO_ROOT="${BUILD_DIR}/dos_installer_iso_root"
DOS_INSTALLER_COM="${DOS_INSTALLER_COM:-${BUILD_DIR}/boot/OSINST.COM}"
DOS_SYSTEM_IMAGE="${DOS_SYSTEM_IMAGE:-${IMAGE_DIR}/os-x86_64-system.img}"
FREEDOS_CACHE_DIR="${BUILD_DIR}/freedos"
FREEDOS_ARCHIVE_URL="${FREEDOS_ARCHIVE_URL:-https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/FD14-FloppyEdition.zip}"
FREEDOS_ARCHIVE_PATH="${FREEDOS_CACHE_DIR}/FD14-FloppyEdition.zip"
FREEDOS_SOURCE_IMAGE="${FREEDOS_CACHE_DIR}/fd-x86.img"
FREEDOS_BOOT_IMAGE="${FREEDOS_CACHE_DIR}/os8-freedos-boot.img"
FREEDOS_SHSUCDX_URL="${FREEDOS_SHSUCDX_URL:-https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/shsucdx.zip}"
FREEDOS_SHSUCDX_ZIP="${FREEDOS_CACHE_DIR}/shsucdx.zip"
FREEDOS_SHSUCDX_COM="${FREEDOS_CACHE_DIR}/SHSUCDX.COM"
FREEDOS_UDVD2_URL="${FREEDOS_UDVD2_URL:-https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/drivers/udvd2.zip}"
FREEDOS_UDVD2_ZIP="${FREEDOS_CACHE_DIR}/udvd2.zip"
FREEDOS_UDVD2_SYS="${FREEDOS_CACHE_DIR}/UDVD2.SYS"

GREEN='\033[0;32m'
NC='\033[0m'

log() {
    echo -e "${GREEN}[DOS-INSTALLER-ISO]${NC} $1"
}

link_or_copy() {
    local src="$1"
    local dst="$2"
    rm -f "$dst"
    if ! ln "$src" "$dst" 2>/dev/null; then
        cp "$src" "$dst"
    fi
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

download_if_missing() {
    local url="$1"
    local path="$2"

    if [ -f "$path" ]; then
        return 0
    fi

    log "Downloading $(basename "$path") from official FreeDOS sources"
    curl -L --fail --retry 3 "$url" -o "$path"
}

extract_zip_member() {
    local zip_path="$1"
    local pattern="$2"
    local output_path="$3"

    if [ -f "$output_path" ]; then
        return 0
    fi

    mkdir -p "$(dirname "$output_path")"

    local tmp_dir
    tmp_dir="$(mktemp -d)"
    unzip -qq "$zip_path" -d "$tmp_dir"
    local member
    member="$(find "$tmp_dir" -type f \( -iname "$pattern" -o -ipath "*/$pattern" \) | head -n 1)"
    if [ -z "$member" ]; then
        rm -rf "$tmp_dir"
        echo "[ERROR] Could not extract $pattern from $zip_path" >&2
        exit 1
    fi
    mv "$member" "$output_path"
    rm -rf "$tmp_dir"
}

ensure_freedos_assets() {
    mkdir -p "$FREEDOS_CACHE_DIR"
    require_cmd curl
    require_cmd unzip
    require_cmd mcopy
    require_cmd mdel
    require_cmd xorriso

    download_if_missing "$FREEDOS_ARCHIVE_URL" "$FREEDOS_ARCHIVE_PATH"
    extract_zip_member "$FREEDOS_ARCHIVE_PATH" 'fd-x86.img' "$FREEDOS_SOURCE_IMAGE"

    download_if_missing "$FREEDOS_SHSUCDX_URL" "$FREEDOS_SHSUCDX_ZIP"
    extract_zip_member "$FREEDOS_SHSUCDX_ZIP" '*SHSUCDX.COM' "$FREEDOS_SHSUCDX_COM"

    download_if_missing "$FREEDOS_UDVD2_URL" "$FREEDOS_UDVD2_ZIP"
    extract_zip_member "$FREEDOS_UDVD2_ZIP" '*UDVD2.SYS' "$FREEDOS_UDVD2_SYS"
}

prepare_freedos_boot_image() {
    cp "$FREEDOS_SOURCE_IMAGE" "$FREEDOS_BOOT_IMAGE"

    cat > "${FREEDOS_CACHE_DIR}/FDAUTO.BAT" <<'EOF'
@ECHO OFF
CLS
ECHO.
ECHO   OS8 Setup
ECHO   Powered by FreeDOS
ECHO.
SHSUCDX /Q /D:OS8CD001 /L:R
IF EXIST R:\DOS\OSINST.COM GOTO RUN
ECHO The OS8 setup CD was not detected.
ECHO Attach the installer ISO and reboot.
GOTO DONE
:RUN
R:
CD \DOS
OSINST.COM
:DONE
ECHO.
ECHO Type EXIT to return to the FreeDOS shell.
EOF

    cat > "${FREEDOS_CACHE_DIR}/FDCONFIG.SYS" <<'EOF'
LASTDRIVE=Z
DEVICE=UDVD2.SYS /D:OS8CD001
SHELL=C:\FREEDOS\BIN\COMMAND.COM C:\FREEDOS\BIN /P=C:\FDAUTO.BAT
EOF

    mdel -i "$FREEDOS_BOOT_IMAGE" ::/FDAUTO.BAT >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/FDCONFIG.SYS >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/SHSUCDX.COM >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/UDVD2.SYS >/dev/null 2>&1 || true

    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "${FREEDOS_CACHE_DIR}/FDAUTO.BAT" ::/FDAUTO.BAT
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "${FREEDOS_CACHE_DIR}/FDCONFIG.SYS" ::/FDCONFIG.SYS
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "$FREEDOS_SHSUCDX_COM" ::/SHSUCDX.COM
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "$FREEDOS_UDVD2_SYS" ::/UDVD2.SYS

    if command -v mlabel >/dev/null 2>&1; then
        mlabel -i "$FREEDOS_BOOT_IMAGE" ::OS8FD14 >/dev/null 2>&1 || true
    fi
}

build_eltorito_hdd_image() {
    local partition_image="$1"
    local hdd_image="$2"

    python3 - "$partition_image" "$hdd_image" <<'PY'
import sys

partition_path, hdd_path = sys.argv[1], sys.argv[2]
partition = bytearray(open(partition_path, "rb").read())
if len(partition) < 512:
    raise SystemExit("partition boot image is too small")
if len(partition) % 512 != 0:
    raise SystemExit("partition image must be 512-byte aligned")

partition_sectors = len(partition) // 512
partition[28:32] = (1).to_bytes(4, "little")

mbr = bytearray(512)
code = bytes([
    0xFA,
    0x31, 0xC0,
    0x8E, 0xD8,
    0x8E, 0xC0,
    0x8E, 0xD0,
    0xBC, 0x00, 0x70,
    0x88, 0x16, 0x3F, 0x7C,
    0xBB, 0x00, 0x7C,
    0xB4, 0x02,
    0xB0, 0x01,
    0xB5, 0x00,
    0xB1, 0x02,
    0xB6, 0x00,
    0x8A, 0x16, 0x3F, 0x7C,
    0xCD, 0x13,
    0x72, 0x02,
    0xEA, 0x00, 0x7C, 0x00, 0x00,
    0xBE, 0x40, 0x7C,
    0xB4, 0x0E,
    0x31, 0xDB,
    0xB3, 0x07,
    0xAC,
    0x84, 0xC0,
    0x74, 0x04,
    0xCD, 0x10,
    0xEB, 0xF7,
    0xF4,
    0xEB, 0xFD,
])
msg = b"Boot sector load failed\x00"
boot_drive_offset = 0x3F
msg_offset = 0x40
if len(code) != boot_drive_offset:
    raise SystemExit(f"unexpected MBR code size: {len(code)}")
if msg_offset + len(msg) > 446:
    raise SystemExit("MBR message overruns partition table")

mbr[:len(code)] = code
mbr[boot_drive_offset] = 0
mbr[msg_offset:msg_offset + len(msg)] = msg

entry = bytearray(16)
entry[0] = 0x80
entry[1:4] = bytes([0x00, 0x02, 0x00])
entry[4] = 0x06
entry[5:8] = bytes([0xFE, 0xFF, 0xFF])
entry[8:12] = (1).to_bytes(4, "little")
entry[12:16] = partition_sectors.to_bytes(4, "little")
mbr[446:462] = entry
mbr[510:512] = b"\x55\xAA"

with open(hdd_path, "wb") as f:
    f.write(mbr)
    f.write(partition)
PY
}

require_file "$DOS_INSTALLER_COM"
require_file "$DOS_SYSTEM_IMAGE"

ensure_freedos_assets
prepare_freedos_boot_image

mkdir -p "$IMAGE_DIR"
rm -rf "$ISO_ROOT"

log "Preparing FreeDOS-based installer ISO root at $ISO_ROOT"
mkdir -p "$ISO_ROOT/boot"
mkdir -p "$ISO_ROOT/dos"

link_or_copy "$FREEDOS_BOOT_IMAGE" "$ISO_ROOT/boot/os8-freedos.img"
build_eltorito_hdd_image "$ISO_ROOT/boot/os8-freedos.img" "$ISO_ROOT/boot/os8-freedos-eltorito.img"
link_or_copy "$DOS_INSTALLER_COM" "$ISO_ROOT/dos/OSINST.COM"
link_or_copy "$DOS_SYSTEM_IMAGE" "$ISO_ROOT/dos/OSSYS.IMG"

cat > "$ISO_ROOT/README.TXT" <<EOF
OS8 DOS Installer ISO

This ISO boots a rebranded FreeDOS environment and launches the OS8 setup
utility from the CD after loading FreeDOS CD-ROM support.

Included files:
- /boot/os8-freedos-eltorito.img : El Torito HDD wrapper used for BIOS boot
- /boot/os8-freedos.img          : patched FreeDOS boot image
- /dos/OSINST.COM                : OS8 DOS installer utility
- /dos/OSSYS.IMG                 : raw OS8 system image written by the installer

FreeDOS components pulled from official mirrors during build:
- FD14-FloppyEdition.zip (fd-x86.img)
- shsucdx.zip (SHSUCDX.COM)
- udvd2.zip (UDVD2.SYS)
EOF

cat > "$ISO_ROOT/FREEDOS.TXT" <<EOF
FreeDOS Integration Notes

This installer ISO boots through FreeDOS 1.4 media from official FreeDOS
mirrors and uses the FreeDOS CD-ROM stack:
- UDVD2.SYS
- SHSUCDX.COM

Source URLs:
- $FREEDOS_ARCHIVE_URL
- $FREEDOS_SHSUCDX_URL
- $FREEDOS_UDVD2_URL
EOF

ISO_PATH="${IMAGE_DIR}/${ISO_NAME}"
rm -f "$ISO_PATH"

log "Creating standalone BIOS FreeDOS installer ISO: $ISO_PATH"
xorriso -as mkisofs \
    -V "OS8-FD-SETUP" \
    -c boot/boot.cat \
    -b boot/os8-freedos-eltorito.img \
    -hard-disk-boot \
    "$ISO_ROOT" \
    -o "$ISO_PATH"

log "Validating ISO contents..."
require_iso_path() {
    local iso_path="$1"
    local output
    output=$(xorriso -indev "$ISO_PATH" -find "$iso_path" -exec lsdl 2>/dev/null || true)
    if [ -z "$output" ]; then
        echo "[ERROR] Missing required ISO path: $iso_path" >&2
        exit 1
    fi
}

require_iso_path "/boot/os8-freedos-eltorito.img"
require_iso_path "/boot/os8-freedos.img"
require_iso_path "/dos/OSINST.COM"
require_iso_path "/dos/OSSYS.IMG"
require_iso_path "/README.TXT"
require_iso_path "/FREEDOS.TXT"

log "DOS installer ISO created successfully: $ISO_PATH"
ls -lh "$ISO_PATH"
echo ""
log "BIOS VM test: qemu-system-x86_64 -M q35 -m 512M -cdrom $ISO_PATH -serial stdio"
