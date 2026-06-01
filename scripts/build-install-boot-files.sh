#!/bin/bash
# Build the bootable OS install layout inside an existing target root.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-build/x86_64}"
INSTALL_ROOT="${2:-${BUILD_DIR}/system-image}"
LIMINE_CFG_SOURCE="${LIMINE_CFG_SOURCE:-${ROOT_DIR}/os-x86_64/limine.conf}"
BOOT_PROFILE="${BOOT_PROFILE:-installed-system}"
KERNEL_PATH="${BUILD_DIR}/kernel/os-x86_64.elf"
BOOT_MANAGER_DIR="${BUILD_DIR}/boot-assets/os-boot-manager"
BOOT_MANAGER_SYNC="${ROOT_DIR}/scripts/update-os-boot-manager.sh"

BOOT_MANAGER_DIR="$("$BOOT_MANAGER_SYNC" "$BOOT_MANAGER_DIR")"
LIMINE_BIN_DIR="${BOOT_MANAGER_DIR}/bin"

GREEN='\033[0;32m'
NC='\033[0m'

log() {
    echo -e "${GREEN}[BOOT-FILES]${NC} $1"
}

require_file() {
    if [ ! -f "$1" ]; then
        echo "[ERROR] Required file not found: $1" >&2
        exit 1
    fi
}

case "$BOOT_PROFILE" in
    installed-system)
        INSTALLER_STATE_SOURCE="installed-system"
        BOOTABLE_SOURCE="installed-system"
        BIOS_BOOTABLE_SOURCE="installed-system"
        FIRST_BOOT_SETUP="1"
        ;;
    installer)
        INSTALLER_STATE_SOURCE="installer-iso"
        BOOTABLE_SOURCE="installer"
        BIOS_BOOTABLE_SOURCE="installer"
        FIRST_BOOT_SETUP="0"
        ;;
    *)
        echo "[ERROR] Unsupported BOOT_PROFILE: $BOOT_PROFILE" >&2
        exit 1
        ;;
esac

require_file "$KERNEL_PATH"
require_file "$LIMINE_CFG_SOURCE"
require_file "$LIMINE_BIN_DIR/BOOTX64.EFI"
require_file "$LIMINE_BIN_DIR/limine-bios.sys"
require_file "$LIMINE_BIN_DIR/limine-bios-cd.bin"
require_file "$LIMINE_BIN_DIR/limine-uefi-cd.bin"

mkdir -p "$INSTALL_ROOT/boot"
mkdir -p "$INSTALL_ROOT/EFI/BOOT"
mkdir -p "$INSTALL_ROOT/limine"
mkdir -p "$INSTALL_ROOT/System"

cp "$KERNEL_PATH" "$INSTALL_ROOT/boot/main.sys"
cp "$KERNEL_PATH" "$INSTALL_ROOT/boot/bootloader.sys"
cp "$LIMINE_CFG_SOURCE" "$INSTALL_ROOT/limine.conf"
cp "$LIMINE_CFG_SOURCE" "$INSTALL_ROOT/boot/limine.conf"
cp "$LIMINE_CFG_SOURCE" "$INSTALL_ROOT/limine/limine.conf"
cp "$LIMINE_CFG_SOURCE" "$INSTALL_ROOT/EFI/BOOT/limine.conf"
cp "$LIMINE_BIN_DIR/limine-bios.sys" "$INSTALL_ROOT/boot/"
cp "$LIMINE_BIN_DIR/limine-bios-cd.bin" "$INSTALL_ROOT/boot/"
cp "$LIMINE_BIN_DIR/limine-uefi-cd.bin" "$INSTALL_ROOT/boot/"
cp "$LIMINE_BIN_DIR/BOOTX64.EFI" "$INSTALL_ROOT/EFI/BOOT/"

cat > "$INSTALL_ROOT/INSTALLERS.TXT" <<'EOF'
OS8 Installer Types

1. Graphical Installer
   Boot menu entry: "OS8 Graphical Installer"
   Use this for the normal desktop installer flow.
EOF

cat > "$INSTALL_ROOT/BOOTABLE.CFG" <<EOF
bootable=1
loader=limine
source=${BOOTABLE_SOURCE}
EOF

cat > "$INSTALL_ROOT/EFI/BOOT/BOOTABLE.CFG" <<EOF
bootable=1
loader=limine
source=${BOOTABLE_SOURCE}
EOF

cat > "$INSTALL_ROOT/boot/BOOTABLE.CFG" <<EOF
bootable=1
scheme=mbr
active_partition=System
loader=limine
source=${BIOS_BOOTABLE_SOURCE}
EOF

cat > "$INSTALL_ROOT/System/installer-state.txt" <<EOF
installed=1
profile=system-image
source=${INSTALLER_STATE_SOURCE}
first_boot_setup=${FIRST_BOOT_SETUP}
EOF

cat > "$INSTALL_ROOT/System/efi-boot.cfg" <<EOF
bootable=1
loader=limine
source=${BOOTABLE_SOURCE}
EOF

cat > "$INSTALL_ROOT/System/mbr-boot.cfg" <<EOF
bootable=1
scheme=mbr
active_partition=System
loader=limine
source=${BIOS_BOOTABLE_SOURCE}
EOF

cat > "$INSTALL_ROOT/IMAGE_INFO.txt" <<EOF
OS8 System Image

This image contains:
- a staged OS install tree
- Limine BIOS and UEFI boot files

Primary payload files:
- /boot/main.sys
- /boot/bootloader.sys
- /limine.conf
- /boot/limine.conf
- /limine/limine.conf
- /EFI/BOOT/limine.conf
- /boot/limine-bios.sys
- /boot/limine-bios-cd.bin
- /boot/limine-uefi-cd.bin
- /EFI/BOOT/BOOTX64.EFI
EOF

log "Boot files staged into $INSTALL_ROOT"
