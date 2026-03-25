#!/bin/bash
# Create a FreeDOS-based DOS installer disk image.

set -e

BUILD_DIR="${1:-build/x86_64}"
IMAGE_DIR="${2:-image}"
IMAGE_NAME="${IMAGE_NAME:-os-x86_64-dos-installer.img}"
IMAGE_SIZE_MB="${IMAGE_SIZE_MB:-16}"
DOS_SYSTEM_IMAGE="${DOS_SYSTEM_IMAGE:-${IMAGE_DIR}/os-x86_64-system.img}"
DOS_INSTALLER_COM="${DOS_INSTALLER_COM:-${BUILD_DIR}/boot/OSINST.COM}"
FREEDOS_CACHE_DIR="${BUILD_DIR}/freedos"
FREEDOS_ARCHIVE_URL="${FREEDOS_ARCHIVE_URL:-https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/FD14-FloppyEdition.zip}"
FREEDOS_ARCHIVE_PATH="${FREEDOS_CACHE_DIR}/FD14-FloppyEdition.zip"
FREEDOS_SOURCE_IMAGE="${FREEDOS_CACHE_DIR}/fd-x86.img"
FREEDOS_BOOT_IMAGE="${FREEDOS_CACHE_DIR}/os8-freedos-disk.img"
FREEDOS_SHSUCDX_URL="${FREEDOS_SHSUCDX_URL:-https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/shsucdx.zip}"
FREEDOS_SHSUCDX_ZIP="${FREEDOS_CACHE_DIR}/shsucdx.zip"
FREEDOS_SHSUCDX_COM="${FREEDOS_CACHE_DIR}/SHSUCDX.COM"
FREEDOS_UDVD2_URL="${FREEDOS_UDVD2_URL:-https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/drivers/udvd2.zip}"
FREEDOS_UDVD2_ZIP="${FREEDOS_CACHE_DIR}/udvd2.zip"
FREEDOS_UDVD2_SYS="${FREEDOS_CACHE_DIR}/UDVD2.SYS"

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

download_if_missing() {
    local url="$1"
    local path="$2"

    if [ -f "$path" ]; then
        return 0
    fi

    log "Downloading $(basename "$path") from official FreeDOS sources"
    curl -L --fail --retry 3 "$url" -o "$path"
}

extract_zip_member() {
    local zip_path="$1"
    local pattern="$2"
    local output_path="$3"

    if [ -f "$output_path" ]; then
        return 0
    fi

    mkdir -p "$(dirname "$output_path")"
    local tmp_dir
    tmp_dir="$(mktemp -d)"
    unzip -qq "$zip_path" -d "$tmp_dir"
    local member
    member="$(find "$tmp_dir" -type f \( -iname "$pattern" -o -ipath "*/$pattern" \) | head -n 1)"
    if [ -z "$member" ]; then
        rm -rf "$tmp_dir"
        echo "[ERROR] Could not extract $pattern from $zip_path" >&2
        exit 1
    fi
    mv "$member" "$output_path"
    rm -rf "$tmp_dir"
}

ensure_freedos_assets() {
    mkdir -p "$FREEDOS_CACHE_DIR"
    require_cmd curl
    require_cmd unzip
    require_cmd mcopy
    require_cmd mdel
    require_cmd mdir

    download_if_missing "$FREEDOS_ARCHIVE_URL" "$FREEDOS_ARCHIVE_PATH"
    extract_zip_member "$FREEDOS_ARCHIVE_PATH" 'fd-x86.img' "$FREEDOS_SOURCE_IMAGE"

    download_if_missing "$FREEDOS_SHSUCDX_URL" "$FREEDOS_SHSUCDX_ZIP"
    extract_zip_member "$FREEDOS_SHSUCDX_ZIP" '*SHSUCDX.COM' "$FREEDOS_SHSUCDX_COM"

    download_if_missing "$FREEDOS_UDVD2_URL" "$FREEDOS_UDVD2_ZIP"
    extract_zip_member "$FREEDOS_UDVD2_ZIP" '*UDVD2.SYS' "$FREEDOS_UDVD2_SYS"
}

prepare_freedos_disk_image() {
    local template_size
    template_size=$(stat -c%s "$FREEDOS_SOURCE_IMAGE")
    local required_size=$((IMAGE_SIZE_MB * 1024 * 1024))
    if [ "$template_size" -lt "$required_size" ]; then
        echo "[ERROR] FreeDOS source image is too small: $FREEDOS_SOURCE_IMAGE" >&2
        echo "        Template size: ${template_size} bytes" >&2
        echo "        Required size: ${required_size} bytes" >&2
        exit 1
    fi

    cp "$FREEDOS_SOURCE_IMAGE" "$FREEDOS_BOOT_IMAGE"

    cat > "${FREEDOS_CACHE_DIR}/FDAUTO.BAT" <<'EOF'
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

    cat > "${FREEDOS_CACHE_DIR}/FDCONFIG.SYS" <<'EOF'
LASTDRIVE=Z
SHELL=C:\FREEDOS\BIN\COMMAND.COM C:\FREEDOS\BIN /P=C:\FDAUTO.BAT
EOF

    mdel -i "$FREEDOS_BOOT_IMAGE" ::/FDAUTO.BAT >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/FDCONFIG.SYS >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/OSINST.COM >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/OSSYS.IMG >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/SHSUCDX.COM >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/UDVD2.SYS >/dev/null 2>&1 || true

    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "${FREEDOS_CACHE_DIR}/FDAUTO.BAT" ::/FDAUTO.BAT
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "${FREEDOS_CACHE_DIR}/FDCONFIG.SYS" ::/FDCONFIG.SYS
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "$DOS_INSTALLER_COM" ::/OSINST.COM
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "$DOS_SYSTEM_IMAGE" ::/OSSYS.IMG
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "$FREEDOS_SHSUCDX_COM" ::/SHSUCDX.COM
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "$FREEDOS_UDVD2_SYS" ::/UDVD2.SYS

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
