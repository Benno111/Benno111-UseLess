#!/bin/bash
# Create a UEFI-bootable FAT disk image for x86_64 without loop devices.

set -e

BUILD_DIR="${1:-build/x86_64}"
IMAGE_DIR="${2:-image}"
IMAGE_NAME="${IMAGE_NAME:-vibos-x86_64.img}"
IMAGE_SIZE_MB="${IMAGE_SIZE_MB:-100}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
KERNEL_PATH="${BUILD_DIR}/kernel/vibos-x86_64.elf"
LIMINE_BIN_DIR="${ROOT_DIR}/vib-os-x86_64/limine/bin"

GREEN='\033[0;32m'
NC='\033[0m'

log() {
    echo -e "${GREEN}[UEFI-IMAGE]${NC} $1"
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

IMAGE_PATH="$IMAGE_DIR/$IMAGE_NAME"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

require_file "$KERNEL_PATH"
require_file "$LIMINE_BIN_DIR/BOOTX64.EFI"
require_cmd mkfs.fat
require_cmd mmd
require_cmd mcopy

log "Creating UEFI disk image: $IMAGE_PATH (${IMAGE_SIZE_MB}M)"
dd if=/dev/zero of="$IMAGE_PATH" bs=1M count="$IMAGE_SIZE_MB" status=none
mkfs.fat -F 32 -n OSNEXT64 "$IMAGE_PATH" >/dev/null

cat > "$TMP_DIR/limine.conf" <<'EOF'
# Limine Configuration File
# OS next stage x64

timeout: 0
default_entry: 1

/OS next stage
    protocol: limine
    kernel_path: boot():/boot/main.sys
EOF

cat > "$TMP_DIR/bootable.cfg" <<'EOF'
bootable=1
loader=limine
source=dos-system-image
EOF

cat > "$TMP_DIR/bios-bootable.cfg" <<'EOF'
bootable=1
scheme=mbr
active_partition=System
loader=limine
source=dos-system-image
EOF

log "Seeding UEFI boot files into FAT image"
mmd -i "$IMAGE_PATH" ::/EFI
mmd -i "$IMAGE_PATH" ::/EFI/BOOT
mmd -i "$IMAGE_PATH" ::/boot
mmd -i "$IMAGE_PATH" ::/limine

mcopy -i "$IMAGE_PATH" "$KERNEL_PATH" ::/boot/main.sys
mcopy -i "$IMAGE_PATH" "$KERNEL_PATH" ::/boot/bootloader.sys
mcopy -i "$IMAGE_PATH" "$LIMINE_BIN_DIR/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$IMAGE_PATH" "$TMP_DIR/limine.conf" ::/limine.conf
mcopy -i "$IMAGE_PATH" "$TMP_DIR/limine.conf" ::/boot/limine.conf
mcopy -i "$IMAGE_PATH" "$TMP_DIR/limine.conf" ::/limine/limine.conf
mcopy -i "$IMAGE_PATH" "$TMP_DIR/limine.conf" ::/EFI/BOOT/limine.conf
mcopy -i "$IMAGE_PATH" "$TMP_DIR/bootable.cfg" ::/BOOTABLE.CFG
mcopy -i "$IMAGE_PATH" "$TMP_DIR/bootable.cfg" ::/EFI/BOOT/BOOTABLE.CFG
mcopy -i "$IMAGE_PATH" "$TMP_DIR/bios-bootable.cfg" ::/boot/BOOTABLE.CFG

log "UEFI boot image created successfully: $IMAGE_PATH"
ls -lh "$IMAGE_PATH"
echo ""
log "To write to USB: sudo dd if=$IMAGE_PATH of=/dev/sdX bs=4M status=progress"
