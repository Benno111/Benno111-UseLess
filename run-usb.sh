PROJECT_NAME="os-kernel-edition"
TARGET="x86_64-unknown-none"
KERNEL_ELF="target/$TARGET/release/$PROJECT_NAME"
IMAGE_OUT="build/bios.img"
LOG_OUT="build/serial.log"
QMP_SOCKET="/tmp/qmp-socket"
QMP_LOG="build/qmp-events.log"
clear
#!/bin/sh
echo "[6/6] Launching QEMU..."
echo "    Serial log -> $LOG_OUT"
echo "    QMP log    -> $QMP_LOG"

# Boot the BIOS image and attach a simple USB tablet + keyboard/mouse so
# the kernel can exercise USB/HID paths in QEMU.
qemu-system-x86_64 \
    -drive format=raw,file="build/bios.img" \
    -serial file:"$LOG_OUT" \
    -qmp unix:/tmp/qmp-socket,server,nowait \
    -device qemu-xhci,id=xhci \
    -device usb-tablet,bus=xhci.0 \
    -device usb-kbd,bus=xhci.0 \
    -no-reboot \
    -no-shutdown
