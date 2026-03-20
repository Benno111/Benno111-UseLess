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

STAGE2_SIZE=$(stat -c%s "$STAGE2")
STAGE2_SECTORS=$(((STAGE2_SIZE + 511) / 512))
IMAGE_TOTAL_SECTORS=$(((IMAGE_SIZE_MB * 1024 * 1024) / 512))
STAGE2_PADDED="$BUILD_DIR/boot/installer_stage2.padded.bin"
STAGE1_PATCHED="$BUILD_DIR/boot/installer_stage1.patched.bin"

log "Creating DOS-style installer volume image at $IMAGE_PATH"
dd if=/dev/zero of="$IMAGE_PATH" bs=1M count="$IMAGE_SIZE_MB" 2>/dev/null

cp "$STAGE1" "$STAGE1_PATCHED"
python3 - "$STAGE1_PATCHED" "$STAGE2_SECTORS" <<'PATCHPY'
import sys
path = sys.argv[1]
sectors = int(sys.argv[2])
with open(path, "r+b") as f:
    data = bytearray(f.read())
    marker = b"\x10\x00\x00\x7E\x00\x00"
    off = data.find(marker)
    if off == -1:
        raise SystemExit("stage2 sector-count marker not found in stage1 image")
    data[off:off+2] = sectors.to_bytes(2, "little")
    f.seek(0)
    f.write(data)
PATCHPY
cp "$STAGE2" "$STAGE2_PADDED"
python3 - "$STAGE2_PADDED" "$IMAGE_TOTAL_SECTORS" <<'PATCHPY'
import sys
path = sys.argv[1]
sector_count = int(sys.argv[2])
marker = (0x0BADF00D).to_bytes(4, "little")
replacement = sector_count.to_bytes(4, "little")
with open(path, "r+b") as f:
    data = bytearray(f.read())
    off = data.find(marker)
    if off == -1:
        raise SystemExit("image sector-count marker not found in stage2 image")
    data[off:off+4] = replacement
    f.seek(0)
    f.write(data)
PATCHPY
truncate -s $((STAGE2_SECTORS * 512)) "$STAGE2_PADDED"

log "Writing stage 1 volume boot sector"
dd if="$STAGE1_PATCHED" of="$IMAGE_PATH" bs=512 count=1 conv=notrunc 2>/dev/null

log "Writing stage 2 text installer ($STAGE2_SECTORS sectors)"
dd if="$STAGE2_PADDED" of="$IMAGE_PATH" bs=512 seek=1 conv=notrunc 2>/dev/null

log "Image created successfully: $IMAGE_PATH"
log "Installer image size: $IMAGE_SIZE_MB MiB ($IMAGE_TOTAL_SECTORS sectors)"
ls -lh "$IMAGE_PATH"
echo ""
log "QEMU test: qemu-system-i386 -drive format=raw,file=$IMAGE_PATH"
log "QEMU x86_64 BIOS test: qemu-system-x86_64 -drive format=raw,file=$IMAGE_PATH"
