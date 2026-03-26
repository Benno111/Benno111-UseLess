#!/bin/bash
# Create a FreeDOS-based DOS installer disk image from source-built assets.

set -e

BUILD_DIR="${1:-build/x86_64}"
IMAGE_DIR="${2:-image}"
IMAGE_NAME="${IMAGE_NAME:-os8-x86_64-dos-installer.img}"
IMAGE_SIZE_MB="${IMAGE_SIZE_MB:-16}"
DOS_SYSTEM_IMAGE="${DOS_SYSTEM_IMAGE:-${IMAGE_DIR}/os8-x86_64-system.img}"
DOS_INSTALLER_COM="${DOS_INSTALLER_COM:-${BUILD_DIR}/boot/OSINST.COM}"
FREEDOS_CACHE_DIR="${BUILD_DIR}/freedos"
FREEDOS_BOOT_IMAGE="${FREEDOS_CACHE_DIR}/os8-freedos-disk.img"

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

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "[ERROR] Required command not found: $1" >&2
        exit 1
    fi
}

extract_image_text_file() {
    local image_path="$1"
    local image_file="$2"
    local host_path="$3"
    rm -f "$host_path"
    if mcopy -n -i "$image_path" "::/${image_file}" "$host_path" >/dev/null 2>&1; then
        return 0
    fi
    return 1
}

append_line_if_missing() {
    local host_path="$1"
    local line="$2"
    if [ -f "$host_path" ] && grep -Fqi "$line" "$host_path"; then
        return 0
    fi
    printf '%s\r\n' "$line" >> "$host_path"
}

prepare_existing_startup_file() {
    local image_path="$1"
    local image_file="$2"
    local host_path="$3"
    extract_image_text_file "$image_path" "$image_file" "$host_path" || :
    if [ ! -f "$host_path" ]; then
        : > "$host_path"
    fi
}

ensure_freedos_assets() {
    mkdir -p "$FREEDOS_CACHE_DIR"
    require_cmd mcopy
    require_cmd mdel
    require_cmd mdir
    ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)" \
    BUILD_DIR="$BUILD_DIR" \
    FREEDOS_CACHE_DIR="$FREEDOS_CACHE_DIR" \
    FREEDOS_MEDIA_NAME="${FREEDOS_MEDIA_NAME:-fd-x86.img}" \
    FREEDOS_BOOT_MODE="${FREEDOS_BOOT_MODE:-liteusb}" \
    FREEDOS_REQUIRE_CD_DRIVERS=0 \
    . "$(cd "$(dirname "$0")" && pwd)/prepare-freedos-source-assets.sh"
}

prepare_freedos_disk_image() {
    local template_size
    template_size=$(stat -c%s "$FREEDOS_MEDIA_IMAGE")
    local required_size=$((IMAGE_SIZE_MB * 1024 * 1024))
    if [ "$template_size" -lt "$required_size" ]; then
        echo "[ERROR] FreeDOS source image is too small: $FREEDOS_MEDIA_IMAGE" >&2
        echo "        Template size: ${template_size} bytes" >&2
        echo "        Required size: ${required_size} bytes" >&2
        exit 1
    fi

    cp "$FREEDOS_MEDIA_IMAGE" "$FREEDOS_BOOT_IMAGE"

    cat > "${FREEDOS_CACHE_DIR}/OS8AUTO.BAT" <<'EOF'
@ECHO OFF
CLS
ECHO.
ECHO   OS8 Setup
ECHO   Powered by FreeDOS
ECHO.
IF EXIST OSINST.COM GOTO RUN
ECHO OSINST.COM is missing from this installer disk.
GOTO DONE
:RUN
OSINST.COM
:DONE
ECHO.
ECHO Type EXIT to return to the FreeDOS shell.
EOF

    prepare_existing_startup_file "$FREEDOS_BOOT_IMAGE" "FDAUTO.BAT" "${FREEDOS_CACHE_DIR}/FDAUTO.BAT"
    prepare_existing_startup_file "$FREEDOS_BOOT_IMAGE" "AUTOEXEC.BAT" "${FREEDOS_CACHE_DIR}/AUTOEXEC.BAT"
    prepare_existing_startup_file "$FREEDOS_BOOT_IMAGE" "FDCONFIG.SYS" "${FREEDOS_CACHE_DIR}/FDCONFIG.SYS"
    prepare_existing_startup_file "$FREEDOS_BOOT_IMAGE" "CONFIG.SYS" "${FREEDOS_CACHE_DIR}/CONFIG.SYS"

    append_line_if_missing "${FREEDOS_CACHE_DIR}/FDAUTO.BAT" "CALL OS8AUTO.BAT"
    append_line_if_missing "${FREEDOS_CACHE_DIR}/AUTOEXEC.BAT" "CALL OS8AUTO.BAT"
    append_line_if_missing "${FREEDOS_CACHE_DIR}/FDCONFIG.SYS" "LASTDRIVE=Z"
    append_line_if_missing "${FREEDOS_CACHE_DIR}/CONFIG.SYS" "LASTDRIVE=Z"

    mdel -i "$FREEDOS_BOOT_IMAGE" ::/OS8AUTO.BAT >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/FDAUTO.BAT >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/AUTOEXEC.BAT >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/FDCONFIG.SYS >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/CONFIG.SYS >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/OSINST.COM >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/OSSYS.IMG >/dev/null 2>&1 || true
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "${FREEDOS_CACHE_DIR}/OS8AUTO.BAT" ::/OS8AUTO.BAT
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "${FREEDOS_CACHE_DIR}/FDAUTO.BAT" ::/FDAUTO.BAT
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "${FREEDOS_CACHE_DIR}/AUTOEXEC.BAT" ::/AUTOEXEC.BAT
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "${FREEDOS_CACHE_DIR}/FDCONFIG.SYS" ::/FDCONFIG.SYS
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "${FREEDOS_CACHE_DIR}/CONFIG.SYS" ::/CONFIG.SYS
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "$DOS_INSTALLER_COM" ::/OSINST.COM
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "$DOS_SYSTEM_IMAGE" ::/OSSYS.IMG

    if command -v mlabel >/dev/null 2>&1; then
        mlabel -i "$FREEDOS_BOOT_IMAGE" ::OS8FD14 >/dev/null 2>&1 || true
    fi
}

require_file "$DOS_INSTALLER_COM"
require_file "$DOS_SYSTEM_IMAGE"

mkdir -p "$IMAGE_DIR"
ensure_freedos_assets
prepare_freedos_disk_image

IMAGE_PATH="$IMAGE_DIR/$IMAGE_NAME"
cp "$FREEDOS_BOOT_IMAGE" "$IMAGE_PATH"

log "FreeDOS installer image contents:"
mdir -i "$IMAGE_PATH" ::/
log "Image created successfully: $IMAGE_PATH"
ls -lh "$IMAGE_PATH"
echo ""
log "QEMU test: qemu-system-i386 -drive format=raw,file=$IMAGE_PATH"
log "QEMU x86_64 BIOS test: qemu-system-x86_64 -drive format=raw,file=$IMAGE_PATH"
