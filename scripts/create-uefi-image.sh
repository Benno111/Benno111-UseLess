#!/bin/bash
# Create a BIOS+UEFI bootable MBR disk image for x86_64 without loop devices.

set -e

BUILD_DIR="${1:-build/x86_64}"
IMAGE_DIR="${2:-image}"
IMAGE_NAME="${IMAGE_NAME:-vibos-x86_64.img}"
IMAGE_SIZE_MB="${IMAGE_SIZE_MB:-100}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
KERNEL_PATH="${BUILD_DIR}/kernel/vibos-x86_64.elf"
LIMINE_BIN_DIR="${ROOT_DIR}/vib-os-x86_64/limine/bin"
LIMINE_SRC_DIR="${ROOT_DIR}/vib-os-x86_64/limine"
LIMINE_TOOL_PATH="${LIMINE_BIN_DIR}/limine"

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

mkdir -p "$IMAGE_DIR"

IMAGE_PATH="$IMAGE_DIR/$IMAGE_NAME"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

require_file "$KERNEL_PATH"
require_file "$LIMINE_BIN_DIR/BOOTX64.EFI"
require_file "$LIMINE_BIN_DIR/limine-bios.sys"
require_cmd mkfs.fat
require_cmd mmd
require_cmd mcopy
require_cmd sfdisk

LIMINE_TOOL="$(resolve_limine_tool)"

log "Creating UEFI disk image: $IMAGE_PATH (${IMAGE_SIZE_MB}M)"
dd if=/dev/zero of="$IMAGE_PATH" bs=1M count="$IMAGE_SIZE_MB" status=none

PART_START_SECTORS=2048
TOTAL_SECTORS=$((IMAGE_SIZE_MB * 1024 * 1024 / 512))
PART_SIZE_SECTORS=$((TOTAL_SECTORS - PART_START_SECTORS))
FAT_OFFSET_BYTES=$((PART_START_SECTORS * 512))
MTOOLS_IMAGE="${IMAGE_PATH}@@${FAT_OFFSET_BYTES}"

printf 'label: dos\nlabel-id: 0x4f534e58\nunit: sectors\n\n%s,%s,0x0c,*\n' \
    "$PART_START_SECTORS" "$PART_SIZE_SECTORS" | sfdisk "$IMAGE_PATH" >/dev/null

mkfs.fat -F 32 --offset "$PART_START_SECTORS" -n OSNEXT64 "$IMAGE_PATH" >/dev/null

cat > "$TMP_DIR/limine.conf" <<'EOF'
# Limine Configuration File
# OS next stage x64

timeout: 0

/OS next stage
    protocol: limine
    kernel_path: boot():/boot/main.sys
EOF

cat > "$TMP_DIR/bootable.cfg" <<'EOF'
bootable=1
loader=limine
source=installer
EOF

cat > "$TMP_DIR/bios-bootable.cfg" <<'EOF'
bootable=1
scheme=mbr
active_partition=System
loader=limine
source=installer
EOF

cat > "$TMP_DIR/installer-state.txt" <<'EOF'
installed=1
profile=system-image
source=installer-iso
EOF

cat > "$TMP_DIR/efi-boot.cfg" <<'EOF'
bootable=1
loader=limine
source=installer
EOF

cat > "$TMP_DIR/mbr-boot.cfg" <<'EOF'
bootable=1
scheme=mbr
active_partition=System
loader=limine
source=installer
EOF

log "Seeding UEFI boot files into FAT image"
mmd -i "$MTOOLS_IMAGE" ::/EFI
mmd -i "$MTOOLS_IMAGE" ::/EFI/BOOT
mmd -i "$MTOOLS_IMAGE" ::/boot
mmd -i "$MTOOLS_IMAGE" ::/limine
mmd -i "$MTOOLS_IMAGE" ::/System

mcopy -i "$MTOOLS_IMAGE" "$KERNEL_PATH" ::/boot/main.sys
mcopy -i "$MTOOLS_IMAGE" "$KERNEL_PATH" ::/boot/bootloader.sys
mcopy -i "$MTOOLS_IMAGE" "$LIMINE_BIN_DIR/limine-bios.sys" ::/limine-bios.sys
mcopy -i "$MTOOLS_IMAGE" "$LIMINE_BIN_DIR/limine-bios.sys" ::/boot/limine-bios.sys
mcopy -i "$MTOOLS_IMAGE" "$LIMINE_BIN_DIR/limine-bios.sys" ::/limine/limine-bios.sys
mcopy -i "$MTOOLS_IMAGE" "$LIMINE_BIN_DIR/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$MTOOLS_IMAGE" "$TMP_DIR/limine.conf" ::/limine.conf
mcopy -i "$MTOOLS_IMAGE" "$TMP_DIR/limine.conf" ::/boot/limine.conf
mcopy -i "$MTOOLS_IMAGE" "$TMP_DIR/limine.conf" ::/limine/limine.conf
mcopy -i "$MTOOLS_IMAGE" "$TMP_DIR/limine.conf" ::/EFI/BOOT/limine.conf
mcopy -i "$MTOOLS_IMAGE" "$TMP_DIR/bootable.cfg" ::/BOOTABLE.CFG
mcopy -i "$MTOOLS_IMAGE" "$TMP_DIR/bootable.cfg" ::/EFI/BOOT/BOOTABLE.CFG
mcopy -i "$MTOOLS_IMAGE" "$TMP_DIR/bios-bootable.cfg" ::/boot/BOOTABLE.CFG
mcopy -i "$MTOOLS_IMAGE" "$TMP_DIR/installer-state.txt" ::/System/installer-state.txt
mcopy -i "$MTOOLS_IMAGE" "$TMP_DIR/efi-boot.cfg" ::/System/efi-boot.cfg
mcopy -i "$MTOOLS_IMAGE" "$TMP_DIR/mbr-boot.cfg" ::/System/mbr-boot.cfg

"$LIMINE_TOOL" bios-install --force-mbr "$IMAGE_PATH" || {
    echo "[ERROR] Failed to install Limine BIOS stages into $IMAGE_PATH" >&2
    exit 1
}

log "BIOS+UEFI boot image created successfully: $IMAGE_PATH"
ls -lh "$IMAGE_PATH"
echo ""
log "To write to USB: sudo dd if=$IMAGE_PATH of=/dev/sdX bs=4M status=progress"
