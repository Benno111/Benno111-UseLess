IMAGE="build/bios.img"
if [ ! -f "$IMAGE" ]; then
    echo "Missing BIOS image: $IMAGE"
    exit 1
fi

qemu-system-x86_64 \
    -drive format=raw,file="$IMAGE" \
    -nographic \
    -serial mon:stdio \
    -qmp unix:/tmp/qmp-socket,server,nowait \
    -no-reboot \
    -no-shutdown
