#!/bin/bash
# Bootstrap the upstream FreeDOS source packages used by the OS8 DOS setup.

set -euo pipefail

ROOT_DIR="${ROOT_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"
FREEDOS_SOURCE_ROOT="${1:-${FREEDOS_SOURCE_ROOT:-${ROOT_DIR}/boot/bios/freedos/source}}"
FREEDOS_DOWNLOAD_DIR="${FREEDOS_SOURCE_ROOT}/downloads"
FREEDOS_VENDOR_DIR="${FREEDOS_SOURCE_ROOT}/vendor"

GREEN='\033[0;32m'
NC='\033[0m'

log() {
    echo -e "${GREEN}[FREEDOS-BOOTSTRAP]${NC} $1"
}

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "[ERROR] Required command not found: $1" >&2
        exit 1
    fi
}

fetch() {
    local url="$1"
    local dst="$2"
    if [ -f "$dst" ]; then
        return 0
    fi
    mkdir -p "$(dirname "$dst")"
    log "Downloading $(basename "$dst")"
    curl -L --fail --retry 3 --output "$dst" "$url"
}

extract_zip() {
    local archive="$1"
    local dst="$2"

    rm -rf "$dst"
    mkdir -p "$dst"

    if command -v unzip >/dev/null 2>&1; then
        unzip -qq -o "$archive" -d "$dst"
        return 0
    fi

    if tar -tf "$archive" >/dev/null 2>&1; then
        tar -xf "$archive" -C "$dst"
        return 0
    fi

    echo "[ERROR] Could not extract $archive. Install unzip or use a tar build with zip support." >&2
    exit 1
}

extract_nested_zip() {
    local archive="$1"
    local nested_member="$2"
    local dst="$3"
    local tmp_dir="${FREEDOS_SOURCE_ROOT}/.tmp-extract"

    rm -rf "$tmp_dir" "$dst"
    mkdir -p "$tmp_dir" "$dst"

    if command -v unzip >/dev/null 2>&1; then
        unzip -qq -o "$archive" "$nested_member" -d "$tmp_dir"
        unzip -qq -o "${tmp_dir}/${nested_member}" -d "$dst"
    else
        tar -xf "$archive" -C "$tmp_dir" "$nested_member"
        tar -xf "${tmp_dir}/${nested_member}" -C "$dst"
    fi

    rm -rf "$tmp_dir"
}

copy_tree_contents() {
    local src="$1"
    local dst="$2"
    mkdir -p "$dst"
    cp -R "$src"/. "$dst"/
}

write_manifest() {
    cat > "${FREEDOS_SOURCE_ROOT}/README.md" <<'EOF'
# FreeDOS Source Bootstrap

This directory is populated by `scripts/bootstrap-freedos-source.sh` and keeps
the upstream FreeDOS source packages used by the OS8 DOS installer flow.

Layout:

- `downloads/` caches the official upstream archives
- `vendor/kernel/` contains the extracted FreeDOS kernel source tree
- `vendor/freecom/` contains the extracted FreeCOM/COMMAND source tree
- `vendor/shsucdx/` contains the SHSUCDX package sources and docs
- `vendor/udvd2/` contains the extracted UDVD2 source package

The DOS build currently uses release media for boot images, but this checkout
now gives the repository a reproducible, local upstream source baseline for the
DOS-side stack it integrates and rebrands.
EOF

    cat > "${FREEDOS_SOURCE_ROOT}/SOURCE_INDEX.txt" <<'EOF'
Official upstream package URLs used by the bootstrap helper:

- FreeDOS kernel 2044:
  https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/dos/kernel/2044/kernel.zip
- FreeCOM / COMMAND 0.86 source:
  https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/dos/command/0.86/freecom-source.zip
- SHSUCDX 3.08:
  https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/dos/shsucdx/shcdx308.zip
- UDVD2 20220217.0:
  https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.2/drivers/udvd2/20220217.0/udvd2.zip
- FreeDOS 1.4 LiteUSB release media:
  https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/FD14-LiteUSB.zip
- FreeDOS 1.4 LegacyCD release media:
  https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/FD14-LegacyCD.zip
EOF
}

require_cmd curl

mkdir -p "$FREEDOS_DOWNLOAD_DIR" "$FREEDOS_VENDOR_DIR"

KERNEL_URL="https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/dos/kernel/2044/kernel.zip"
FREECOM_URL="https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/dos/command/0.86/freecom-source.zip"
SHSUCDX_URL="https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/dos/shsucdx/shcdx308.zip"
UDVD2_URL="https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.2/drivers/udvd2/20220217.0/udvd2.zip"
LITEUSB_URL="https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/FD14-LiteUSB.zip"
LEGACYCD_URL="https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/FD14-LegacyCD.zip"

fetch "$KERNEL_URL" "${FREEDOS_DOWNLOAD_DIR}/kernel.zip"
fetch "$FREECOM_URL" "${FREEDOS_DOWNLOAD_DIR}/freecom-source.zip"
fetch "$SHSUCDX_URL" "${FREEDOS_DOWNLOAD_DIR}/shcdx308.zip"
fetch "$UDVD2_URL" "${FREEDOS_DOWNLOAD_DIR}/udvd2.zip"
fetch "$LITEUSB_URL" "${FREEDOS_DOWNLOAD_DIR}/FD14-LiteUSB.zip"
fetch "$LEGACYCD_URL" "${FREEDOS_DOWNLOAD_DIR}/FD14-LegacyCD.zip"

tmp_kernel="${FREEDOS_SOURCE_ROOT}/.kernel"
tmp_freecom="${FREEDOS_SOURCE_ROOT}/.freecom"
tmp_shsucdx="${FREEDOS_SOURCE_ROOT}/.shsucdx"

extract_zip "${FREEDOS_DOWNLOAD_DIR}/kernel.zip" "$tmp_kernel"
rm -rf "${FREEDOS_VENDOR_DIR}/kernel"
copy_tree_contents "${tmp_kernel}/SOURCE/KERNEL" "${FREEDOS_VENDOR_DIR}/kernel"
rm -rf "$tmp_kernel"

extract_zip "${FREEDOS_DOWNLOAD_DIR}/freecom-source.zip" "$tmp_freecom"
rm -rf "${FREEDOS_VENDOR_DIR}/freecom"
copy_tree_contents "${tmp_freecom}/freecom" "${FREEDOS_VENDOR_DIR}/freecom"
rm -rf "$tmp_freecom"

extract_zip "${FREEDOS_DOWNLOAD_DIR}/shcdx308.zip" "$tmp_shsucdx"
rm -rf "${FREEDOS_VENDOR_DIR}/shsucdx"
copy_tree_contents "$tmp_shsucdx" "${FREEDOS_VENDOR_DIR}/shsucdx"
rm -rf "$tmp_shsucdx"

extract_nested_zip "${FREEDOS_DOWNLOAD_DIR}/udvd2.zip" "SOURCE/UDVD2/SOURCES.ZIP" "${FREEDOS_VENDOR_DIR}/udvd2"

write_manifest

log "FreeDOS source packages are available under ${FREEDOS_SOURCE_ROOT}"
