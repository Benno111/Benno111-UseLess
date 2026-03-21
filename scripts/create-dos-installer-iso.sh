#!/bin/bash
# Create a standalone BIOS-bootable ISO for the DOS-style text installer.

set -e

BUILD_DIR="${1:-build/x86_64}"
IMAGE_DIR="${2:-image}"
ISO_NAME="${ISO_NAME:-os-x86_64-dos-installer.iso}"
ISO_ROOT="${BUILD_DIR}/dos_installer_iso_root"
DOS_INSTALLER_IMAGE="${DOS_INSTALLER_IMAGE:-${IMAGE_DIR}/os-x86_64-dos-installer.img}"
DOS_INSTALLER_COM="${DOS_INSTALLER_COM:-${BUILD_DIR}/boot/OSINST.COM}"

GREEN='\033[0;32m'
NC='\033[0m'

log() {
    echo -e "${GREEN}[DOS-INSTALLER-ISO]${NC} $1"
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

require_file "$DOS_INSTALLER_IMAGE"
require_cmd xorriso

mkdir -p "$IMAGE_DIR"
rm -rf "$ISO_ROOT"

log "Preparing DOS installer ISO root at $ISO_ROOT"
mkdir -p "$ISO_ROOT/boot"
mkdir -p "$ISO_ROOT/dos"

cp "$DOS_INSTALLER_IMAGE" "$ISO_ROOT/boot/dos-installer.img"
cp "$DOS_INSTALLER_IMAGE" "$ISO_ROOT/dos/OSINST.IMG"

if [ -f "$DOS_INSTALLER_COM" ]; then
    cp "$DOS_INSTALLER_COM" "$ISO_ROOT/dos/OSINST.COM"
fi

cat > "$ISO_ROOT/README.TXT" <<EOF
OS next stage DOS Installer ISO

This ISO boots directly into the BIOS text-mode DOS rescue installer.

Included files:
- /boot/dos-installer.img : El Torito boot image used by this ISO
- /dos/OSINST.IMG         : raw BIOS installer disk image
- /dos/OSINST.COM         : DOS-side writer utility (if built)

Usage:
1. Boot this ISO in BIOS mode.
2. Use the text installer to deploy the fallback installer media to a target disk.
3. If you already have DOS available, you can instead run OSINST.COM and point it at OSINST.IMG.
EOF

ISO_PATH="${IMAGE_DIR}/${ISO_NAME}"
rm -f "$ISO_PATH"

log "Creating standalone BIOS DOS installer ISO: $ISO_PATH"
xorriso -as mkisofs \
    -V "OSK-DOS-INSTALL" \
    -b boot/dos-installer.img \
    -no-emul-boot \
    -boot-load-size full \
    -boot-info-table \
    "$ISO_ROOT" \
    -o "$ISO_PATH"

log "Validating DOS installer ISO contents..."
require_iso_path() {
    local iso_path="$1"
    local output
    output=$(xorriso -indev "$ISO_PATH" -find "$iso_path" -exec lsdl 2>/dev/null || true)
    if [ -z "$output" ]; then
        echo "[ERROR] Missing required ISO path: $iso_path" >&2
        exit 1
    fi
}

require_iso_path "/boot/dos-installer.img"
require_iso_path "/dos/OSINST.IMG"
require_iso_path "/README.TXT"
if [ -f "$DOS_INSTALLER_COM" ]; then
    require_iso_path "/dos/OSINST.COM"
fi

log "DOS installer ISO created successfully: $ISO_PATH"
ls -lh "$ISO_PATH"
echo ""
log "BIOS VM test: qemu-system-x86_64 -M q35 -m 512M -cdrom $ISO_PATH -serial stdio"
