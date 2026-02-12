#!/usr/bin/env bash
set -e

KERNEL_ELF="$1"
OUT="build/bios.img"
STAGE2_SECTORS=128

if [ ! -f "$KERNEL_ELF" ]; then
    echo "Kernel ELF not found: $KERNEL_ELF"
    exit 1
fi

mkdir -p build

ENTRY_HEX=$(readelf -h "$KERNEL_ELF" | awk '/Entry point address/ {print $4}')
if [ -z "$ENTRY_HEX" ]; then
    echo "Failed to read kernel entry point"
    exit 1
fi

objcopy -O binary "$KERNEL_ELF" build/kernel.bin
KERNEL_SIZE=$(stat -c%s build/kernel.bin)
KERNEL_SECTORS=$(( (KERNEL_SIZE + 511) / 512 ))
KERNEL_LBA=$(( 1 + STAGE2_SECTORS ))

nasm -f bin bootloader/bios/stage1.asm -o build/stage1.bin -D STAGE2_SECTORS=$STAGE2_SECTORS
nasm -f bin bootloader/bios/stage2.asm -o build/stage2.bin \
    -D KERNEL_LBA=$KERNEL_LBA \
    -D KERNEL_SECTORS=$KERNEL_SECTORS \
    -D KERNEL_SIZE=$KERNEL_SIZE \
    -D KERNEL_ENTRY=$ENTRY_HEX

STAGE2_SIZE=$(stat -c%s build/stage2.bin)
MAX_STAGE2=$(( STAGE2_SECTORS * 512 ))
if [ "$STAGE2_SIZE" -gt "$MAX_STAGE2" ]; then
    echo "Stage2 too large: $STAGE2_SIZE bytes (max $MAX_STAGE2)"
    exit 1
fi

truncate -s "$MAX_STAGE2" build/stage2.bin

dd if=/dev/zero of="$OUT" bs=512 count=1 status=none
dd if=build/stage1.bin of="$OUT" conv=notrunc status=none
dd if=build/stage2.bin of="$OUT" bs=512 seek=1 conv=notrunc status=none
dd if=build/kernel.bin of="$OUT" bs=512 seek=$KERNEL_LBA conv=notrunc status=none

echo "BIOS image built: $OUT"
