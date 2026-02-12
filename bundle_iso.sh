#!/usr/bin/env bash
set -e

ISO_OUT="build/os-kernel-edition.iso"
BIOS_IMG="build/bios.img"

if ! command -v xorriso >/dev/null 2>&1; then
    echo "xorriso not found. Install it to build the ISO."
    exit 1
fi

if [ ! -f "$BIOS_IMG" ]; then
    echo "Missing BIOS image: $BIOS_IMG"
    exit 1
fi

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

cp "$BIOS_IMG" "$WORKDIR/bios.img"

xorriso -as mkisofs \
    -R -J \
    -o "$ISO_OUT" \
    -c "boot.cat" \
    -b "bios.img" \
    -no-emul-boot \
    -boot-load-size 4 \
    -boot-info-table \
    -eltorito-alt-boot \
    -no-emul-boot \
    -isohybrid-gpt-basdat \
    "$WORKDIR"

echo "ISO created: $ISO_OUT"
