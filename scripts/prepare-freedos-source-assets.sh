#!/bin/bash
# Resolve FreeDOS media and optional CD driver assets for OS8 DOS setup.
# Prefer source-built assets, but bootstrap from official release media when
# unattended builds need a default path.

set -e

ROOT_DIR="${ROOT_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"
BUILD_DIR="${BUILD_DIR:-build/x86_64}"
FREEDOS_SOURCE_ROOT="${FREEDOS_SOURCE_ROOT:-${ROOT_DIR}/boot/bios/freedos/source}"
FREEDOS_OUTPUT_DIR="${FREEDOS_OUTPUT_DIR:-${ROOT_DIR}/boot/bios/freedos/out}"
FREEDOS_CACHE_DIR="${FREEDOS_CACHE_DIR:-${BUILD_DIR}/freedos}"
FREEDOS_BUILD_COMMAND="${FREEDOS_BUILD_COMMAND:-}"
FREEDOS_MEDIA_NAME="${FREEDOS_MEDIA_NAME:-fd-lite.img}"
FREEDOS_BOOT_MODE="${FREEDOS_BOOT_MODE:-liteusb}"
FREEDOS_REQUIRE_CD_DRIVERS="${FREEDOS_REQUIRE_CD_DRIVERS:-0}"
FREEDOS_ALLOW_OFFICIAL_BOOTSTRAP="${FREEDOS_ALLOW_OFFICIAL_BOOTSTRAP:-1}"

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

download_file() {
    local url="$1"
    local dst="$2"
    require_cmd_src curl
    if [ ! -f "$dst" ]; then
        mkdir -p "$(dirname "$dst")"
        log_freedos "Downloading $(basename "$dst") from official FreeDOS sources"
        curl -L --fail --retry 3 --output "$dst" "$url"
    fi
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

extract_iso_member() {
    local iso_path="$1"
    local wanted="$2"
    local dst="$3"
    local iso_member
    require_cmd_src xorriso
    iso_member="$(xorriso -indev "$iso_path" -find / -type f -name "$wanted" -print 2>/dev/null | awk 'NF { print; exit }')"
    if [ -z "$iso_member" ]; then
        return 1
    fi
    mkdir -p "$(dirname "$dst")"
    xorriso -indev "$iso_path" -osirrox on -extract "$iso_member" "$dst" >/dev/null 2>&1
}

bootstrap_package_member() {
    local archive_url="$1"
    local archive_name="$2"
    local member_name="$3"
    local dst="$4"
    local archive_path="${FREEDOS_CACHE_DIR}/${archive_name}"

    download_file "$archive_url" "$archive_path"
    if [ ! -f "$dst" ]; then
        extract_zip_member "$archive_path" "$member_name" "$dst" || {
            echo "[ERROR] Could not extract ${member_name} from ${archive_path}" >&2
            exit 1
        }
    fi
}

resolve_assets_once() {
    if [ -n "${FREEDOS_MEDIA_IMAGE:-}" ] && [ -f "${FREEDOS_MEDIA_IMAGE}" ]; then
        :
    elif [ -d "$FREEDOS_OUTPUT_DIR" ]; then
        FREEDOS_MEDIA_IMAGE="$(find_first_matching_file "$FREEDOS_OUTPUT_DIR" "$FREEDOS_MEDIA_NAME" 'fd-lite.img' 'fd-full.img' 'boot-standard.img' 'fd-x86.img' || true)"
    fi

    if [ -n "${FREEDOS_SHSUCDX_COM:-}" ] && [ -f "${FREEDOS_SHSUCDX_COM}" ]; then
        :
    elif [ -d "$FREEDOS_OUTPUT_DIR" ]; then
        FREEDOS_SHSUCDX_COM="$(find_first_matching_file "$FREEDOS_OUTPUT_DIR" 'SHSUCDX.COM' || true)"
    fi

    if [ -n "${FREEDOS_UDVD2_SYS:-}" ] && [ -f "${FREEDOS_UDVD2_SYS}" ]; then
        :
    elif [ -d "$FREEDOS_OUTPUT_DIR" ]; then
        FREEDOS_UDVD2_SYS="$(find_first_matching_file "$FREEDOS_OUTPUT_DIR" 'UDVD2.SYS' || true)"
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

bootstrap_official_assets() {
    mkdir -p "$FREEDOS_CACHE_DIR"

    case "$FREEDOS_BOOT_MODE" in
        legacycd)
            local legacy_zip="${FREEDOS_CACHE_DIR}/FD14-LegacyCD.zip"
            local boot_img="${FREEDOS_CACHE_DIR}/FD14BOOT.img"
            local legacy_iso="${FREEDOS_CACHE_DIR}/FD14LGCY.iso"
            download_file "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/FD14-LegacyCD.zip" "$legacy_zip"
            if [ ! -f "$boot_img" ]; then
                extract_zip_member "$legacy_zip" "FD14BOOT.img" "$boot_img" || {
                    echo "[ERROR] Could not extract FD14BOOT.img from $legacy_zip" >&2
                    exit 1
                }
            fi
            if [ ! -f "$legacy_iso" ]; then
                extract_zip_member "$legacy_zip" "FD14LGCY.iso" "$legacy_iso" || {
                    echo "[ERROR] Could not extract FD14LGCY.iso from $legacy_zip" >&2
                    exit 1
                }
            fi
            FREEDOS_MEDIA_IMAGE="$boot_img"
            if [ "$FREEDOS_REQUIRE_CD_DRIVERS" = "1" ]; then
                if [ -z "${FREEDOS_SHSUCDX_COM:-}" ] || [ ! -f "${FREEDOS_SHSUCDX_COM:-}" ]; then
                    FREEDOS_SHSUCDX_COM="${FREEDOS_CACHE_DIR}/SHSUCDX.COM"
                    bootstrap_package_member \
                        "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/dos/shsucdx/shcdx308.zip" \
                        "shcdx308.zip" \
                        "shsucdx.com" \
                        "$FREEDOS_SHSUCDX_COM"
                fi
                if [ -z "${FREEDOS_UDVD2_SYS:-}" ] || [ ! -f "${FREEDOS_UDVD2_SYS:-}" ]; then
                    FREEDOS_UDVD2_SYS="${FREEDOS_CACHE_DIR}/UDVD2.SYS"
                    bootstrap_package_member \
                        "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.2/drivers/udvd2/20220217.0/udvd2.zip" \
                        "udvd2.zip" \
                        "UDVD2.SYS" \
                        "$FREEDOS_UDVD2_SYS"
                fi
            fi
            ;;
        liteusb)
            local lite_zip="${FREEDOS_CACHE_DIR}/FD14-LiteUSB.zip"
            local lite_img="${FREEDOS_CACHE_DIR}/FD14LITE.img"
            download_file "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/FD14-LiteUSB.zip" "$lite_zip"
            if [ ! -f "$lite_img" ]; then
                extract_zip_member "$lite_zip" "FD14LITE.img" "$lite_img" || {
                    echo "[ERROR] Could not extract FD14LITE.img from $lite_zip" >&2
                    exit 1
                }
            fi
            FREEDOS_MEDIA_IMAGE="$lite_img"
            ;;
        *)
            echo "[ERROR] Unsupported FREEDOS_BOOT_MODE: $FREEDOS_BOOT_MODE" >&2
            exit 1
            ;;
    esac
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

if ! have_required_assets && [ "$FREEDOS_ALLOW_OFFICIAL_BOOTSTRAP" = "1" ]; then
    bootstrap_official_assets
    resolve_assets_once
fi

if ! have_required_assets; then
    echo "[ERROR] FreeDOS assets were not found." >&2
    echo "        Expected output directory: $FREEDOS_OUTPUT_DIR" >&2
    echo "        Missing media image name:  $FREEDOS_MEDIA_NAME" >&2
    if [ "$FREEDOS_REQUIRE_CD_DRIVERS" = "1" ]; then
        echo "        Required files: media image, SHSUCDX.COM, UDVD2.SYS" >&2
    else
        echo "        Required files: media image" >&2
    fi
    echo "        Provide them in $FREEDOS_OUTPUT_DIR or set:" >&2
    echo "          FREEDOS_MEDIA_IMAGE=/path/to/media.img" >&2
    if [ "$FREEDOS_REQUIRE_CD_DRIVERS" = "1" ]; then
        echo "          FREEDOS_SHSUCDX_COM=/path/to/SHSUCDX.COM" >&2
        echo "          FREEDOS_UDVD2_SYS=/path/to/UDVD2.SYS" >&2
    fi
    echo "        To build automatically from source, also set:" >&2
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
