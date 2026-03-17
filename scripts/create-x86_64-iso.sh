#!/bin/bash
# Create a hybrid x86_64 ISO that boots via both BIOS and UEFI.
# The resulting ISO can be attached to VMs directly or written to USB media.

set -e

BUILD_DIR="${1:-build/x86_64}"
IMAGE_DIR="${2:-image}"
ISO_NAME="${ISO_NAME:-vibos-x86_64.iso}"
ISO_ROOT="${BUILD_DIR}/iso_root"
KERNEL_PATH="${BUILD_DIR}/kernel/vibos-x86_64.elf"
LIMINE_BIN_DIR="$(cd "$(dirname "$0")/.." && pwd)/vib-os-x86_64/limine/bin"
LIMINE_CFG="${LIMINE_CFG:-$(cd "$(dirname "$0")/.." && pwd)/vib-os-x86_64/limine.conf}"
VALIDATE_ISO="${VALIDATE_ISO:-0}"
REBUILD_ISO_ROOT="${REBUILD_ISO_ROOT:-0}"

GREEN='\033[0;32m'
NC='\033[0m'

log() {
    echo -e "${GREEN}[X86_64-ISO]${NC} $1"
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

require_file "$KERNEL_PATH"
require_file "$LIMINE_CFG"
require_file "$LIMINE_BIN_DIR/BOOTX64.EFI"
require_file "$LIMINE_BIN_DIR/limine-bios.sys"
require_file "$LIMINE_BIN_DIR/limine-bios-cd.bin"
require_file "$LIMINE_BIN_DIR/limine-uefi-cd.bin"
require_file "$LIMINE_BIN_DIR/limine"
require_cmd xorriso

mkdir -p "$IMAGE_DIR"
if [ "$REBUILD_ISO_ROOT" = "1" ]; then
    rm -rf "$ISO_ROOT"
fi

log "Preparing ISO root at $ISO_ROOT"
mkdir -p "$ISO_ROOT/boot"
mkdir -p "$ISO_ROOT/EFI/BOOT"
mkdir -p "$ISO_ROOT/limine"

if [ -d "${BUILD_DIR}/assets" ]; then
    mkdir -p "$ISO_ROOT/assets"
    cp -Ru "${BUILD_DIR}/assets"/. "$ISO_ROOT/assets/"
fi

# Keep both names so the ISO matches the embedded config and the repo's
# existing naming used elsewhere.
cp -u "$KERNEL_PATH" "$ISO_ROOT/boot/kernel.elf"
cp -u "$KERNEL_PATH" "$ISO_ROOT/boot/vibos.elf"
cp -u "$LIMINE_CFG" "$ISO_ROOT/limine.conf"
cp -u "$LIMINE_CFG" "$ISO_ROOT/boot/limine.conf"
cp -u "$LIMINE_CFG" "$ISO_ROOT/limine/limine.conf"
cp -u "$LIMINE_CFG" "$ISO_ROOT/EFI/BOOT/limine.conf"

cp -u "$LIMINE_BIN_DIR/limine-bios.sys" "$ISO_ROOT/boot/"
cp -u "$LIMINE_BIN_DIR/limine-bios-cd.bin" "$ISO_ROOT/boot/"
cp -u "$LIMINE_BIN_DIR/limine-uefi-cd.bin" "$ISO_ROOT/boot/"
cp -u "$LIMINE_BIN_DIR/BOOTX64.EFI" "$ISO_ROOT/EFI/BOOT/"

ISO_PATH="${IMAGE_DIR}/${ISO_NAME}"
rm -f "$ISO_PATH"

log "Creating hybrid BIOS+UEFI ISO: $ISO_PATH"
xorriso -as mkisofs \
    -b boot/limine-bios-cd.bin \
    -no-emul-boot \
    -boot-load-size 4 \
    -boot-info-table \
    --efi-boot boot/limine-uefi-cd.bin \
    -efi-boot-part \
    --efi-boot-image \
    --protective-msdos-label \
    "$ISO_ROOT" \
    -o "$ISO_PATH"

"$LIMINE_BIN_DIR/limine" bios-install "$ISO_PATH" >/dev/null 2>&1 || true

if [ "$VALIDATE_ISO" = "1" ]; then
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

    require_iso_path "/boot/kernel.elf"
    require_iso_path "/boot/limine-bios-cd.bin"
    require_iso_path "/boot/limine-uefi-cd.bin"
    require_iso_path "/boot/limine-bios.sys"
    require_iso_path "/EFI/BOOT/BOOTX64.EFI"
    require_iso_path "/limine.conf"
    require_iso_path "/boot/limine.conf"
    require_iso_path "/EFI/BOOT/limine.conf"
fi

log "ISO created successfully: $ISO_PATH"
ls -lh "$ISO_PATH"
echo ""
log "BIOS VM test:  qemu-system-x86_64 -M q35 -m 512M -cdrom $ISO_PATH -serial stdio"
log "UEFI VM test:  qemu-system-x86_64 -M q35 -m 512M -cdrom $ISO_PATH -bios /usr/share/OVMF/OVMF_CODE.fd -serial stdio"
log "Rufus: select $ISO_PATH and write it in ISO mode to a USB drive"
