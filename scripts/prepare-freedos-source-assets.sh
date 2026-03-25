#!/bin/bash
# Resolve source-built FreeDOS media and CD driver assets for OS8 DOS setup.

set -e

ROOT_DIR="${ROOT_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"
BUILD_DIR="${BUILD_DIR:-build/x86_64}"
FREEDOS_SOURCE_ROOT="${FREEDOS_SOURCE_ROOT:-${ROOT_DIR}/boot/bios/freedos/source}"
FREEDOS_OUTPUT_DIR="${FREEDOS_OUTPUT_DIR:-${ROOT_DIR}/boot/bios/freedos/out}"
FREEDOS_BUILD_COMMAND="${FREEDOS_BUILD_COMMAND:-}"
FREEDOS_MEDIA_NAME="${FREEDOS_MEDIA_NAME:-fd-lite.img}"

log_freedos() {
    printf '[FREEDOS-SRC] %s\n' "$1"
}

require_file_src() {
    if [ ! -f "$1" ]; then
        echo "[ERROR] Required file not found: $1" >&2
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

resolve_assets_once

if { [ -z "${FREEDOS_MEDIA_IMAGE:-}" ] || [ -z "${FREEDOS_SHSUCDX_COM:-}" ] || [ -z "${FREEDOS_UDVD2_SYS:-}" ]; } && \
   [ -d "$FREEDOS_SOURCE_ROOT" ] && [ -n "$FREEDOS_BUILD_COMMAND" ]; then
    log_freedos "Building FreeDOS assets from source in $FREEDOS_SOURCE_ROOT"
    (
        cd "$FREEDOS_SOURCE_ROOT"
        bash -lc "$FREEDOS_BUILD_COMMAND"
    )
    resolve_assets_once
fi

if [ -z "${FREEDOS_MEDIA_IMAGE:-}" ] || [ -z "${FREEDOS_SHSUCDX_COM:-}" ] || [ -z "${FREEDOS_UDVD2_SYS:-}" ]; then
    echo "[ERROR] Source-built FreeDOS assets were not found." >&2
    echo "        Expected output directory: $FREEDOS_OUTPUT_DIR" >&2
    echo "        Missing media image name:  $FREEDOS_MEDIA_NAME" >&2
    echo "        Required files: media image, SHSUCDX.COM, UDVD2.SYS" >&2
    echo "        Provide them in $FREEDOS_OUTPUT_DIR or set:" >&2
    echo "          FREEDOS_MEDIA_IMAGE=/path/to/media.img" >&2
    echo "          FREEDOS_SHSUCDX_COM=/path/to/SHSUCDX.COM" >&2
    echo "          FREEDOS_UDVD2_SYS=/path/to/UDVD2.SYS" >&2
    echo "        To build automatically from source, also set:" >&2
    echo "          FREEDOS_SOURCE_ROOT=/path/to/freedos/source" >&2
    echo "          FREEDOS_BUILD_COMMAND='...'" >&2
    exit 1
fi

require_file_src "$FREEDOS_MEDIA_IMAGE"
require_file_src "$FREEDOS_SHSUCDX_COM"
require_file_src "$FREEDOS_UDVD2_SYS"

export FREEDOS_MEDIA_IMAGE
export FREEDOS_SHSUCDX_COM
export FREEDOS_UDVD2_SYS
