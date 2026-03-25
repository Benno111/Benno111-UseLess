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

build_eltorito_hdd_image() {
    local partition_image="$1"
    local hdd_image="$2"

    python3 - "$partition_image" "$hdd_image" <<'PY'
import os
import struct
import sys

partition_path, hdd_path = sys.argv[1], sys.argv[2]
partition = bytearray(open(partition_path, "rb").read())
if len(partition) < 512:
    raise SystemExit("partition boot image is too small")
if len(partition) % 512 != 0:
    raise SystemExit("partition image must be 512-byte aligned")

partition_sectors = len(partition) // 512

# Tell the partition boot sector it lives at LBA 1 inside the HDD wrapper.
partition[28:32] = (1).to_bytes(4, "little")

mbr = bytearray(512)
code = bytes([
    0xFA,                         # cli
    0x31, 0xC0,                   # xor ax, ax
    0x8E, 0xD8,                   # mov ds, ax
    0x8E, 0xC0,                   # mov es, ax
    0x8E, 0xD0,                   # mov ss, ax
    0xBC, 0x00, 0x70,             # mov sp, 0x7000
    0x88, 0x16, 0x3A, 0x7C,       # mov [0x7C3A], dl
    0xBB, 0x00, 0x7C,             # mov bx, 0x7C00
    0xB4, 0x02,                   # mov ah, 0x02
    0xB0, 0x01,                   # mov al, 0x01
    0xB5, 0x00,                   # mov ch, 0x00
    0xB1, 0x02,                   # mov cl, 0x02
    0xB6, 0x00,                   # mov dh, 0x00
    0x8A, 0x16, 0x3A, 0x7C,       # mov dl, [0x7C3A]
    0xCD, 0x13,                   # int 0x13
    0x72, 0x02,                   # jc fail
    0xEA, 0x00, 0x7C, 0x00, 0x00, # jmp 0x0000:0x7C00
    0xBE, 0x3B, 0x7C,             # fail: mov si, msg
    0xB4, 0x0E,                   # mov ah, 0x0E
    0x31, 0xDB,                   # xor bx, bx
    0xB3, 0x07,                   # mov bl, 0x07
    0xAC,                         # lodsb
    0x84, 0xC0,                   # test al, al
    0x74, 0x04,                   # jz hang
    0xCD, 0x10,                   # int 0x10
    0xEB, 0xF7,                   # jmp print loop
    0xF4,                         # hang: hlt
    0xEB, 0xFD,                   # jmp hang
])
msg = b"Boot sector load failed\x00"
boot_drive_offset = 0x3A
msg_offset = 0x3B
if len(code) != boot_drive_offset:
    raise SystemExit(f"unexpected MBR code size: {len(code)}")
if msg_offset + len(msg) > 446:
    raise SystemExit("MBR message overruns partition table")

mbr[:len(code)] = code
mbr[boot_drive_offset] = 0
mbr[msg_offset:msg_offset + len(msg)] = msg

entry = bytearray(16)
entry[0] = 0x80                      # active
entry[1:4] = bytes([0x00, 0x02, 0x00])
entry[4] = 0x06                      # FAT16
entry[5:8] = bytes([0xFE, 0xFF, 0xFF])
entry[8:12] = (1).to_bytes(4, "little")
entry[12:16] = partition_sectors.to_bytes(4, "little")
offset = 446
mbr[offset:offset + 16] = entry
mbr[510:512] = b"\x55\xAA"

with open(hdd_path, "wb") as f:
    f.write(mbr)
    f.write(partition)
PY
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

require_file "$DOS_INSTALLER_IMAGE"
require_cmd xorriso

mkdir -p "$IMAGE_DIR"
rm -rf "$ISO_ROOT"

log "Preparing DOS installer ISO root at $ISO_ROOT"
mkdir -p "$ISO_ROOT/boot"
mkdir -p "$ISO_ROOT/dos"

link_or_copy "$DOS_INSTALLER_IMAGE" "$ISO_ROOT/boot/dos-installer.img"
build_eltorito_hdd_image "$ISO_ROOT/boot/dos-installer.img" "$ISO_ROOT/boot/dos-installer-eltorito.img"
link_or_copy "$ISO_ROOT/boot/dos-installer.img" "$ISO_ROOT/dos/OSINST.IMG"

if [ -f "$DOS_INSTALLER_COM" ]; then
    link_or_copy "$DOS_INSTALLER_COM" "$ISO_ROOT/dos/OSINST.COM"
fi

cat > "$ISO_ROOT/README.TXT" <<EOF
OS8 DOS Installer ISO

This ISO boots directly into the BIOS text-mode DOS rescue installer.

Included files:
- /boot/dos-installer-eltorito.img : El Torito HDD wrapper used for BIOS boot
- /boot/dos-installer.img : raw DOS installer partition image
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
    -c boot/boot.cat \
    -b boot/dos-installer-eltorito.img \
    -hard-disk-boot \
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

require_iso_path "/boot/dos-installer-eltorito.img"
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
