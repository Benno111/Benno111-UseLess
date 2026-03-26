#!/bin/bash
# Resolve FreeDOS media and optional CD driver assets for OS8 DOS setup
# using local assets only.

set -e

ROOT_DIR="${ROOT_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"
BUILD_DIR="${BUILD_DIR:-build/x86_64}"
FREEDOS_SOURCE_ROOT="${FREEDOS_SOURCE_ROOT:-${ROOT_DIR}/boot/bios/freedos/source}"
FREEDOS_OUTPUT_DIR="${FREEDOS_OUTPUT_DIR:-${ROOT_DIR}/boot/bios/freedos/out}"
FREEDOS_VENDOR_DIR="${FREEDOS_VENDOR_DIR:-${ROOT_DIR}/boot/bios/freedos/vendor}"
FREEDOS_VENDOR_PACKAGES_DIR="${FREEDOS_VENDOR_PACKAGES_DIR:-${FREEDOS_VENDOR_DIR}/packages}"
FREEDOS_VENDOR_MEDIA_DIR="${FREEDOS_VENDOR_MEDIA_DIR:-${FREEDOS_VENDOR_DIR}/media}"
FREEDOS_CACHE_DIR="${FREEDOS_CACHE_DIR:-${BUILD_DIR}/freedos}"
FREEDOS_BUILD_COMMAND="${FREEDOS_BUILD_COMMAND:-}"
FREEDOS_MEDIA_NAME="${FREEDOS_MEDIA_NAME:-fd-lite.img}"
FREEDOS_BOOT_MODE="${FREEDOS_BOOT_MODE:-liteusb}"
FREEDOS_REQUIRE_CD_DRIVERS="${FREEDOS_REQUIRE_CD_DRIVERS:-0}"

log_freedos() {
    printf '[FREEDOS-SRC] %s\n' "$1"
}

require_file_src() {
    if [ ! -f "$1" ]; then
        echo "[ERROR] Required file not found: $1" >&2
        exit 1
    fi
}

require_cmd_src() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "[ERROR] Required command not found: $1" >&2
        exit 1
    fi
}

find_first_matching_file() {
    local base_dir="$1"
    shift
    local pattern
    for pattern in "$@"; do
        local hit
        hit="$(find "$base_dir" -type f \( -iname "$pattern" -o -ipath "*/$pattern" \) | head -n 1)"
        if [ -n "$hit" ]; then
            printf '%s\n' "$hit"
            return 0
        fi
    done
    return 1
}

find_zip_member() {
    local archive="$1"
    local wanted="$2"
    require_cmd_src unzip
    unzip -Z1 "$archive" | tr -d '\r' | while IFS= read -r entry; do
        local base="${entry##*/}"
        if [ "${base,,}" = "${wanted,,}" ]; then
            printf '%s\n' "$entry"
            break
        fi
    done
}

extract_zip_member() {
    local archive="$1"
    local wanted="$2"
    local dst="$3"
    local entry
    entry="$(find_zip_member "$archive" "$wanted")"
    if [ -z "$entry" ]; then
        return 1
    fi
    mkdir -p "$(dirname "$dst")"
    unzip -qq -o "$archive" "$entry" -d "$(dirname "$dst")"
    if [ "$entry" != "$(basename "$dst")" ]; then
        mv -f "$(dirname "$dst")/$entry" "$dst"
        rmdir --ignore-fail-on-non-empty -p "$(dirname "$dst")/$(dirname "$entry")" 2>/dev/null || true
    fi
    return 0
}

extract_vendor_member_if_needed() {
    local archive="$1"
    local member="$2"
    local dst="$3"

    if [ -f "$dst" ] || [ ! -f "$archive" ]; then
        return 0
    fi

    mkdir -p "$(dirname "$dst")"
    extract_zip_member "$archive" "$member" "$dst" || {
        echo "[ERROR] Could not extract ${member} from ${archive}" >&2
        exit 1
    }
}

resolve_assets_once() {
    mkdir -p "$FREEDOS_CACHE_DIR"

    extract_vendor_member_if_needed "${FREEDOS_VENDOR_PACKAGES_DIR}/shcdx308.zip" "shsucdx.com" "${FREEDOS_CACHE_DIR}/SHSUCDX.COM"
    extract_vendor_member_if_needed "${FREEDOS_VENDOR_PACKAGES_DIR}/udvd2.zip" "UDVD2.SYS" "${FREEDOS_CACHE_DIR}/UDVD2.SYS"

    if [ -n "${FREEDOS_MEDIA_IMAGE:-}" ] && [ -f "${FREEDOS_MEDIA_IMAGE}" ]; then
        :
    elif [ -d "$FREEDOS_OUTPUT_DIR" ]; then
        FREEDOS_MEDIA_IMAGE="$(find_first_matching_file "$FREEDOS_OUTPUT_DIR" "$FREEDOS_MEDIA_NAME" 'fd-lite.img' 'fd-full.img' 'boot-standard.img' 'fd-x86.img' || true)"
    elif [ -d "$FREEDOS_VENDOR_MEDIA_DIR" ]; then
        FREEDOS_MEDIA_IMAGE="$(find_first_matching_file "$FREEDOS_VENDOR_MEDIA_DIR" "$FREEDOS_MEDIA_NAME" 'fd-lite.img' 'fd-full.img' 'boot-standard.img' 'fd-x86.img' || true)"
    fi

    if [ -n "${FREEDOS_SHSUCDX_COM:-}" ] && [ -f "${FREEDOS_SHSUCDX_COM}" ]; then
        :
    elif [ -d "$FREEDOS_OUTPUT_DIR" ]; then
        FREEDOS_SHSUCDX_COM="$(find_first_matching_file "$FREEDOS_OUTPUT_DIR" 'SHSUCDX.COM' || true)"
    elif [ -f "${FREEDOS_CACHE_DIR}/SHSUCDX.COM" ]; then
        FREEDOS_SHSUCDX_COM="${FREEDOS_CACHE_DIR}/SHSUCDX.COM"
    fi

    if [ -n "${FREEDOS_UDVD2_SYS:-}" ] && [ -f "${FREEDOS_UDVD2_SYS}" ]; then
        :
    elif [ -d "$FREEDOS_OUTPUT_DIR" ]; then
        FREEDOS_UDVD2_SYS="$(find_first_matching_file "$FREEDOS_OUTPUT_DIR" 'UDVD2.SYS' || true)"
    elif [ -f "${FREEDOS_CACHE_DIR}/UDVD2.SYS" ]; then
        FREEDOS_UDVD2_SYS="${FREEDOS_CACHE_DIR}/UDVD2.SYS"
    fi
}

have_required_assets() {
    if [ -z "${FREEDOS_MEDIA_IMAGE:-}" ] || [ ! -f "${FREEDOS_MEDIA_IMAGE:-}" ]; then
        return 1
    fi
    if [ "$FREEDOS_REQUIRE_CD_DRIVERS" = "1" ]; then
        if [ -z "${FREEDOS_SHSUCDX_COM:-}" ] || [ ! -f "${FREEDOS_SHSUCDX_COM:-}" ]; then
            return 1
        fi
        if [ -z "${FREEDOS_UDVD2_SYS:-}" ] || [ ! -f "${FREEDOS_UDVD2_SYS:-}" ]; then
            return 1
        fi
    fi
    return 0
}

resolve_assets_once

if ! have_required_assets && [ -d "$FREEDOS_SOURCE_ROOT" ] && [ -n "$FREEDOS_BUILD_COMMAND" ]; then
    log_freedos "Building FreeDOS assets from source in $FREEDOS_SOURCE_ROOT"
    (
        cd "$FREEDOS_SOURCE_ROOT"
        bash -lc "$FREEDOS_BUILD_COMMAND"
    )
    resolve_assets_once
fi

if ! have_required_assets; then
    echo "[ERROR] FreeDOS assets were not found." >&2
    echo "        Expected output directory: $FREEDOS_OUTPUT_DIR" >&2
    echo "        Vendored media directory: $FREEDOS_VENDOR_MEDIA_DIR" >&2
    echo "        Missing media image name:  $FREEDOS_MEDIA_NAME" >&2
    if [ "$FREEDOS_REQUIRE_CD_DRIVERS" = "1" ]; then
        echo "        Required files: media image, SHSUCDX.COM, UDVD2.SYS" >&2
    else
        echo "        Required files: media image" >&2
    fi
    echo "        This build no longer downloads FreeDOS assets automatically." >&2
    echo "        Provide them in $FREEDOS_OUTPUT_DIR, $FREEDOS_VENDOR_MEDIA_DIR, or set:" >&2
    echo "          FREEDOS_MEDIA_IMAGE=/path/to/media.img" >&2
    if [ "$FREEDOS_REQUIRE_CD_DRIVERS" = "1" ]; then
        echo "          FREEDOS_SHSUCDX_COM=/path/to/SHSUCDX.COM" >&2
        echo "          FREEDOS_UDVD2_SYS=/path/to/UDVD2.SYS" >&2
    fi
    echo "        Vendored packages are read from: $FREEDOS_VENDOR_PACKAGES_DIR" >&2
    echo "        To build automatically from a local source tree, also set:" >&2
    echo "          FREEDOS_SOURCE_ROOT=/path/to/freedos/source" >&2
    echo "          FREEDOS_BUILD_COMMAND='...'" >&2
    exit 1
fi

require_file_src "$FREEDOS_MEDIA_IMAGE"
if [ "$FREEDOS_REQUIRE_CD_DRIVERS" = "1" ]; then
    require_file_src "$FREEDOS_SHSUCDX_COM"
    require_file_src "$FREEDOS_UDVD2_SYS"
fi

export FREEDOS_MEDIA_IMAGE
export FREEDOS_SHSUCDX_COM
export FREEDOS_UDVD2_SYS
