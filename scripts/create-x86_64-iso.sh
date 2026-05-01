#!/bin/bash
# Create a hybrid x86_64 ISO that boots via both BIOS and UEFI.
# The resulting ISO can be attached to VMs directly or written to USB media.

set -e

BUILD_DIR="${1:-build/x86_64}"
IMAGE_DIR="${2:-image}"
ISO_NAME="${ISO_NAME:-os8-x86_64.iso}"
ISO_ROOT="${BUILD_DIR}/iso_root"
KERNEL_PATH="${BUILD_DIR}/kernel/os-x86_64.elf"
X86_64_BOOT_ASSET_DIR="$(cd "$(dirname "$0")/.." && pwd)/os-x86_64"
LIMINE_BIN_DIR="${X86_64_BOOT_ASSET_DIR}/limine/bin"
LIMINE_SRC_DIR="${X86_64_BOOT_ASSET_DIR}/limine"
LIMINE_TOOL_PATH="${LIMINE_BIN_DIR}/limine"
LIMINE_CFG="${LIMINE_CFG:-${X86_64_BOOT_ASSET_DIR}/limine.conf}"
INSTALL_LIMINE_CFG="${INSTALL_LIMINE_CFG:-${X86_64_BOOT_ASSET_DIR}/limine.conf}"
INSTALL_ROOT="${ISO_ROOT}/install/system-image"

GREEN='\033[0;32m'
NC='\033[0m'

log() {
    echo -e "${GREEN}[X86_64-ISO]${NC} $1"
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

ensure_executable() {
    local path="$1"
    if [ ! -x "$path" ]; then
        chmod +x "$path" 2>/dev/null || true
    fi
    if [ ! -x "$path" ]; then
        echo "[ERROR] Required executable is not runnable: $path" >&2
        exit 1
    fi
}

tool_runs() {
    local path="$1"
    if [ ! -x "$path" ]; then
        return 1
    fi
    if "$path" --help >/dev/null 2>&1; then
        return 0
    fi
    case $? in
        126|127)
            return 1
            ;;
        *)
            return 0
            ;;
    esac
}

resolve_limine_tool() {
    local host_tool="${LIMINE_SRC_DIR}/limine-host"

    if tool_runs "$LIMINE_TOOL_PATH"; then
        printf '%s\n' "$LIMINE_TOOL_PATH"
        return 0
    fi

    require_file "${LIMINE_SRC_DIR}/limine.c"
    require_cmd cc

    if ! tool_runs "$host_tool"; then
        log "Bundled Limine host tool is not runnable on this platform; building a native copy"
        cc -g -O2 -pipe -Wall -Wextra -std=c99 "${LIMINE_SRC_DIR}/limine.c" -o "$host_tool"
        ensure_executable "$host_tool"
    fi

    if ! tool_runs "$host_tool"; then
        echo "[ERROR] Failed to build a runnable Limine host tool: $host_tool" >&2
        exit 1
    fi

    printf '%s\n' "$host_tool"
}

require_file "$KERNEL_PATH"
require_file "$LIMINE_CFG"
require_file "$INSTALL_LIMINE_CFG"
require_file "$LIMINE_BIN_DIR/BOOTX64.EFI"
require_file "$LIMINE_BIN_DIR/limine-bios.sys"
require_file "$LIMINE_BIN_DIR/limine-bios-cd.bin"
require_file "$LIMINE_BIN_DIR/limine-uefi-cd.bin"
require_cmd xorriso

LIMINE_TOOL="$(resolve_limine_tool)"

mkdir -p "$IMAGE_DIR"
rm -rf "$ISO_ROOT"

log "Preparing ISO root at $ISO_ROOT"
mkdir -p "$ISO_ROOT/boot"
mkdir -p "$ISO_ROOT/EFI/BOOT"
mkdir -p "$ISO_ROOT/limine"
mkdir -p "$INSTALL_ROOT/boot"
mkdir -p "$INSTALL_ROOT/EFI/BOOT"
mkdir -p "$INSTALL_ROOT/limine"

if [ -d "${BUILD_DIR}/assets" ]; then
    mkdir -p "$ISO_ROOT/assets"
    cp -R "${BUILD_DIR}/assets"/. "$ISO_ROOT/assets/"
    mkdir -p "$INSTALL_ROOT/assets"
    cp -R "${BUILD_DIR}/assets"/. "$INSTALL_ROOT/assets/"
fi

# Keep both names so the ISO matches the embedded config and the repo's
# existing naming used elsewhere.
cp "$KERNEL_PATH" "$ISO_ROOT/boot/main.sys"
cp "$KERNEL_PATH" "$ISO_ROOT/boot/bootloader.sys"
cp "$LIMINE_CFG" "$ISO_ROOT/limine.conf"
cp "$LIMINE_CFG" "$ISO_ROOT/boot/limine.conf"
cp "$LIMINE_CFG" "$ISO_ROOT/limine/limine.conf"
cp "$LIMINE_CFG" "$ISO_ROOT/EFI/BOOT/limine.conf"
cp "$KERNEL_PATH" "$INSTALL_ROOT/boot/main.sys"
cp "$KERNEL_PATH" "$INSTALL_ROOT/boot/bootloader.sys"
cp "$INSTALL_LIMINE_CFG" "$INSTALL_ROOT/limine.conf"
cp "$INSTALL_LIMINE_CFG" "$INSTALL_ROOT/boot/limine.conf"
cp "$INSTALL_LIMINE_CFG" "$INSTALL_ROOT/limine/limine.conf"
cp "$INSTALL_LIMINE_CFG" "$INSTALL_ROOT/EFI/BOOT/limine.conf"
cp "$LIMINE_BIN_DIR/limine-bios.sys" "$INSTALL_ROOT/boot/"
cp "$LIMINE_BIN_DIR/limine-bios-cd.bin" "$INSTALL_ROOT/boot/"
cp "$LIMINE_BIN_DIR/limine-uefi-cd.bin" "$INSTALL_ROOT/boot/"
cp "$LIMINE_BIN_DIR/BOOTX64.EFI" "$INSTALL_ROOT/EFI/BOOT/"

cp "$LIMINE_BIN_DIR/limine-bios.sys" "$ISO_ROOT/boot/"
cp "$LIMINE_BIN_DIR/limine-bios-cd.bin" "$ISO_ROOT/boot/"
cp "$LIMINE_BIN_DIR/limine-uefi-cd.bin" "$ISO_ROOT/boot/"
cp "$LIMINE_BIN_DIR/BOOTX64.EFI" "$ISO_ROOT/EFI/BOOT/"

cat > "$ISO_ROOT/INSTALLERS.TXT" <<EOF
OS8 Installer Types

1. Graphical Installer
   Boot menu entry: "OS8 Graphical Installer"
   Use this for the normal desktop installer flow.
EOF

cp "$ISO_ROOT/INSTALLERS.TXT" "$INSTALL_ROOT/INSTALLERS.TXT"

cat > "$INSTALL_ROOT/IMAGE_INFO.txt" <<EOF
OS8 System Image

This ISO contains:
- a graphical installer environment
- a bundled system image payload at /install/system-image

Primary payload files:
- /install/system-image/boot/main.sys
- /install/system-image/boot/bootloader.sys
- /install/system-image/limine.conf
- /install/system-image/boot/limine.conf
- /install/system-image/limine/limine.conf
- /install/system-image/EFI/BOOT/limine.conf
- /install/system-image/boot/limine-bios.sys
- /install/system-image/boot/limine-bios-cd.bin
- /install/system-image/boot/limine-uefi-cd.bin
- /install/system-image/EFI/BOOT/BOOTX64.EFI

If present, repo assets are mirrored under:
- /install/system-image/assets

- /INSTALLERS.TXT

The installer GUI boots from the top-level ISO files and installs the bundled
desktop/system layout represented by this image payload.
EOF

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

"$LIMINE_TOOL" bios-install "$ISO_PATH" >/dev/null 2>&1 || true

log "Validating ISO contents..."
ISO_CONTENTS_FILE="${ISO_ROOT}/iso-contents.txt"
xorriso -indev "$ISO_PATH" -find / -type f -exec lsdl > "$ISO_CONTENTS_FILE"

require_iso_path() {
    local iso_path="$1"
    local output
    output=$(xorriso -indev "$ISO_PATH" -find "$iso_path" -exec lsdl 2>/dev/null || true)
    if [ -z "$output" ]; then
        echo "[ERROR] Missing required ISO path: $iso_path" >&2
        exit 1
    fi
}

require_iso_path "/boot/main.sys"
require_iso_path "/boot/limine-bios-cd.bin"
require_iso_path "/boot/limine-uefi-cd.bin"
require_iso_path "/boot/limine-bios.sys"
require_iso_path "/EFI/BOOT/BOOTX64.EFI"
require_iso_path "/limine.conf"
require_iso_path "/boot/limine.conf"
require_iso_path "/EFI/BOOT/limine.conf"
require_iso_path "/INSTALLERS.TXT"
require_iso_path "/install/system-image/INSTALLERS.TXT"
require_iso_path "/install/system-image/boot/main.sys"
require_iso_path "/install/system-image/boot/bootloader.sys"
require_iso_path "/install/system-image/limine.conf"
require_iso_path "/install/system-image/boot/limine.conf"
require_iso_path "/install/system-image/limine/limine.conf"
require_iso_path "/install/system-image/EFI/BOOT/limine.conf"
require_iso_path "/install/system-image/boot/limine-bios.sys"
require_iso_path "/install/system-image/boot/limine-bios-cd.bin"
require_iso_path "/install/system-image/boot/limine-uefi-cd.bin"
require_iso_path "/install/system-image/EFI/BOOT/BOOTX64.EFI"
require_iso_path "/install/system-image/IMAGE_INFO.txt"

log "ISO created successfully: $ISO_PATH"
ls -lh "$ISO_PATH"
echo ""
log "BIOS VM test:  qemu-system-x86_64 -M q35 -m 512M -cdrom $ISO_PATH -serial stdio"
log "UEFI VM test:  qemu-system-x86_64 -M q35 -m 512M -cdrom $ISO_PATH -bios /usr/share/OVMF/OVMF_CODE.fd -serial stdio"
log "Rufus: select $ISO_PATH and write it in ISO mode to a USB drive"
