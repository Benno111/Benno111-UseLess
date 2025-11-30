qemu-system-x86_64 \
    -drive format=raw,file="build/bios.img" \
    -serial mon:stdio \
    -no-reboot \
    -no-shutdown
