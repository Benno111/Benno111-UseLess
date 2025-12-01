#!/usr/bin/env bash
set -euo pipefail

IMG_PATH="${1:-build/bios.img}"
DEV="${2:-/dev/sdd}"

if [ ! -f "$IMG_PATH" ]; then
    echo "Image not found: $IMG_PATH" >&2
    exit 1
fi

if [[ "$DEV" =~ [0-9]$ ]]; then
    echo "Refusing to write to a partition ($DEV). Pass the whole device (e.g. /dev/sdX)." >&2
    exit 1
fi

echo "About to wipe and write $IMG_PATH to $DEV"
echo "WARNING: This will erase all data on $DEV and remove any existing GRUB/MBR"
read -r -p "Type 'yes' to continue: " ans
if [ "$ans" != "yes" ]; then
    echo "Aborted."
    exit 1
fi
echo "Zeroing first megabyte to clear boot records..."
sudo dd if=/dev/zero of="$DEV" bs=1M count=1 conv=fsync
echo "Writing image..."

sudo dd if="$IMG_PATH" of="$DEV" bs=4M status=progress conv=fsync
sync
echo "Done. USB written to $DEV"
