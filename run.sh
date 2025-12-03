qemu-system-x86_64 \
    -drive format=raw,file="build/bios.img" \
    -serial mon:stdio \
    -qmp unix:/tmp/qmp-socket,server,nowait \
    -no-reboot \
    -no-shutdown
