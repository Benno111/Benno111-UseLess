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

log "Creating DOS-style installer volume image at $IMAGE_PATH"
dd if=/dev/zero of="$IMAGE_PATH" bs=1M count="$IMAGE_SIZE_MB" 2>/dev/null

log "Writing stage 1 volume boot sector"
dd if="$STAGE1" of="$IMAGE_PATH" bs=512 count=1 conv=notrunc 2>/dev/null

log "Writing stage 2 text installer"
dd if="$STAGE2" of="$IMAGE_PATH" bs=512 seek=1 conv=notrunc 2>/dev/null

log "Image created successfully: $IMAGE_PATH"
ls -lh "$IMAGE_PATH"
echo ""
log "QEMU test: qemu-system-i386 -drive format=raw,file=$IMAGE_PATH"
log "QEMU x86_64 BIOS test: qemu-system-x86_64 -drive format=raw,file=$IMAGE_PATH"
