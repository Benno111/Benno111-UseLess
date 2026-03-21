#!/bin/bash
# Create a hybrid x86_64 ISO that boots via both BIOS and UEFI.
# The resulting ISO can be attached to VMs directly or written to USB media.

set -e

BUILD_DIR="${1:-build/x86_64}"
IMAGE_DIR="${2:-image}"
ISO_NAME="${ISO_NAME:-os8-x86_64.iso}"
ISO_ROOT="${BUILD_DIR}/iso_root"
KERNEL_PATH="${BUILD_DIR}/kernel/vibos-x86_64.elf"
LIMINE_BIN_DIR="$(cd "$(dirname "$0")/.." && pwd)/vib-os-x86_64/limine/bin"
LIMINE_CFG="${LIMINE_CFG:-$(cd "$(dirname "$0")/.." && pwd)/vib-os-x86_64/limine.conf}"
INSTALL_LIMINE_CFG="${INSTALL_LIMINE_CFG:-$(cd "$(dirname "$0")/.." && pwd)/vib-os-x86_64/limine.conf}"
INSTALL_ROOT="${ISO_ROOT}/install/system-image"
DOS_INSTALLER_IMAGE="${DOS_INSTALLER_IMAGE:-${IMAGE_DIR}/os-x86_64-dos-installer.img}"
DOS_INSTALLER_COM="${DOS_INSTALLER_COM:-${BUILD_DIR}/boot/OSINST.COM}"

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
require_file "$INSTALL_LIMINE_CFG"
require_file "$LIMINE_BIN_DIR/BOOTX64.EFI"
require_file "$LIMINE_BIN_DIR/limine-bios.sys"
require_file "$LIMINE_BIN_DIR/limine-bios-cd.bin"
require_file "$LIMINE_BIN_DIR/limine-uefi-cd.bin"
require_file "$LIMINE_BIN_DIR/limine"
require_cmd xorriso

mkdir -p "$IMAGE_DIR"
rm -rf "$ISO_ROOT"

log "Preparing ISO root at $ISO_ROOT"
mkdir -p "$ISO_ROOT/boot"
mkdir -p "$ISO_ROOT/EFI/BOOT"
mkdir -p "$ISO_ROOT/limine"
mkdir -p "$ISO_ROOT/dos"
mkdir -p "$INSTALL_ROOT/boot"
mkdir -p "$INSTALL_ROOT/EFI/BOOT"
mkdir -p "$INSTALL_ROOT/limine"
mkdir -p "$INSTALL_ROOT/dos"

if [ -f "$DOS_INSTALLER_IMAGE" ]; then
    DOS_INSTALLER_ENABLED=1
else
    DOS_INSTALLER_ENABLED=0
fi

if [ -f "$DOS_INSTALLER_COM" ]; then
    DOS_INSTALLER_COM_ENABLED=1
else
    DOS_INSTALLER_COM_ENABLED=0
fi

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

if [ "$DOS_INSTALLER_ENABLED" -eq 1 ]; then
    cp "$DOS_INSTALLER_IMAGE" "$ISO_ROOT/boot/dos-installer.img"
    cp "$DOS_INSTALLER_IMAGE" "$INSTALL_ROOT/boot/dos-installer.img"
    cp "$DOS_INSTALLER_IMAGE" "$ISO_ROOT/dos/OSINST.IMG"
    cp "$DOS_INSTALLER_IMAGE" "$INSTALL_ROOT/dos/OSINST.IMG"
fi

if [ "$DOS_INSTALLER_COM_ENABLED" -eq 1 ]; then
    cp "$DOS_INSTALLER_COM" "$ISO_ROOT/dos/OSINST.COM"
    cp "$DOS_INSTALLER_COM" "$INSTALL_ROOT/dos/OSINST.COM"
fi

cat > "$ISO_ROOT/dos/README.TXT" <<EOF
OS next stage DOS Rescue Tools

This folder contains the DOS-side fallback installer package:
- OSINST.COM : run this from an existing DOS system
- OSINST.IMG : raw fallback installer disk image

Usage from DOS:
1. Copy both files to a DOS machine or DOS boot disk.
2. Boot DOS and change into the directory containing these files.
3. Run OSINST.COM.
4. Choose a target BIOS disk and confirm the write.

The target disk will be overwritten sector-by-sector.
EOF

cp "$ISO_ROOT/dos/README.TXT" "$INSTALL_ROOT/dos/README.TXT"

cat > "$ISO_ROOT/INSTALLERS.TXT" <<EOF
OS next stage Installer Types

1. Graphical Installer
   Boot menu entry: "OS next stage Graphical Installer"
   Use this for the normal desktop installer flow.

2. DOS Rescue Installer
   Boot menu entry: "OS next stage DOS Rescue Installer"
   Use this only as a fallback when the graphical installer path fails.

DOS rescue files are also mirrored in /dos:
- OSINST.COM
- OSINST.IMG
- README.TXT
EOF

cp "$ISO_ROOT/INSTALLERS.TXT" "$INSTALL_ROOT/INSTALLERS.TXT"

cat > "$INSTALL_ROOT/IMAGE_INFO.txt" <<EOF
OS next stage System Image

This ISO contains:
- a graphical installer environment
- a separate DOS rescue installer path
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

DOS fallback tools included in the ISO:
- /boot/dos-installer.img
- /dos/OSINST.COM
- /dos/OSINST.IMG
- /dos/README.TXT
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
    $(if [ "$DOS_INSTALLER_ENABLED" -eq 1 ]; then printf '%s ' -append_partition 2 0x06 "$DOS_INSTALLER_IMAGE"; fi) \
    "$ISO_ROOT" \
    -o "$ISO_PATH"

"$LIMINE_BIN_DIR/limine" bios-install "$ISO_PATH" >/dev/null 2>&1 || true

if [ "$DOS_INSTALLER_ENABLED" -eq 1 ]; then
    patch_dos_installer_partition() {
        local image_path="$1"
        local start_lba
        local partition_entry_offset=462
        local byte0
        local byte1
        local byte2
        local byte3

        start_lba=$(od -An -t u4 -j 470 -N 4 "$image_path" | tr -d ' \n')
        if [ -z "$start_lba" ] || [ "$start_lba" = "0" ]; then
            echo "[ERROR] Failed to locate appended DOS installer partition in $image_path" >&2
            exit 1
        fi

        # Mark appended partition 2 active so BIOS chainloading treats it as
        # a bootable volume instead of a passive data partition.
        printf '\200' | dd of="$image_path" bs=1 seek="$partition_entry_offset" conv=notrunc status=none

        byte0=$(( start_lba        & 0xFF ))
        byte1=$(( (start_lba >> 8) & 0xFF ))
        byte2=$(( (start_lba >> 16) & 0xFF ))
        byte3=$(( (start_lba >> 24) & 0xFF ))

        printf "\\$(printf '%03o' "$byte0")\\$(printf '%03o' "$byte1")\\$(printf '%03o' "$byte2")\\$(printf '%03o' "$byte3")" \
            | dd of="$image_path" bs=1 seek=$(( start_lba * 512 + 28 )) conv=notrunc status=none
    }

    log "Patching DOS installer partition chainload base"
    patch_dos_installer_partition "$ISO_PATH"
    "$LIMINE_BIN_DIR/limine" bios-install "$ISO_PATH" >/dev/null 2>&1 || true
fi

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
if [ "$DOS_INSTALLER_ENABLED" -eq 1 ]; then
    require_iso_path "/boot/dos-installer.img"
    require_iso_path "/install/system-image/boot/dos-installer.img"
    require_iso_path "/dos/OSINST.IMG"
    require_iso_path "/install/system-image/dos/OSINST.IMG"
fi
if [ "$DOS_INSTALLER_COM_ENABLED" -eq 1 ]; then
    require_iso_path "/dos/OSINST.COM"
    require_iso_path "/install/system-image/dos/OSINST.COM"
fi
require_iso_path "/dos/README.TXT"
require_iso_path "/install/system-image/INSTALLERS.TXT"
require_iso_path "/install/system-image/dos/README.TXT"
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
