#!/usr/bin/env bash
set -e

PROJECT_NAME="os-kernel-edition"
TARGET="x86_64-unknown-none"
KERNEL_ELF="target/$TARGET/debug/$PROJECT_NAME"
IMAGE_OUT="build/bios.img"
LOG_OUT="build/serial.log"

# Ensure cargo/rustup-installed tools are on PATH (bootloader_linker, etc.)
export PATH="$HOME/.cargo/bin:$HOME/.rustup/toolchains/nightly-x86_64-unknown-linux-gnu/bin:$PATH"

echo "==============================="
echo "  OS-Kernel-Edition Build"
echo "==============================="
echo ""

# 1. CHECK RUST TOOLCHAIN
echo "[1/6] Installing nightly toolchain if missing..."
rustup toolchain install nightly >/dev/null 2>&1 || true

echo "    Adding target $TARGET..."
rustup target add $TARGET --toolchain nightly >/dev/null 2>&1 || true

echo "    Adding LLVM tools..."
rustup component add llvm-tools-preview --toolchain nightly >/dev/null 2>&1 || true

# 2. INSTALL BOOTLOADER_LINKER
echo "[2/6] Checking bootloader_linker..."
if ! command -v bootloader_linker >/dev/null 2>&1; then
    echo "    bootloader_linker not found. Installing..."
    cargo install bootloader_linker
else
    echo "    bootloader_linker already installed."
fi

# 3. BUILD KERNEL ELF
echo "[3/6] Building kernel ELF..."
cargo +nightly build --target "$TARGET"

if [ ! -f "$KERNEL_ELF" ]; then
    echo "ERROR: Kernel ELF not found: $KERNEL_ELF"
    exit 1
fi
echo "    Kernel built successfully!"

# 4. PREP OUTPUT DIR
echo "[4/6] Preparing build directory..."
mkdir -p build
rm -f "$LOG_OUT"

# 5. BUILD DISK IMAGE
echo "[5/6] Building bootable disk image..."
bootloader_linker build "$KERNEL_ELF" --out-dir build

if [ -f "$IMAGE_OUT" ]; then
    :
elif [ -f "build/uefi.img" ]; then
    IMAGE_OUT="build/uefi.img"
else
    echo "ERROR: bootloader_linker did not produce an image in ./build"
    exit 1
fi

echo "    Image generated: $IMAGE_OUT"

# 6. RUN IN QEMU
echo "[6/6] Launching QEMU..."
echo "    Serial log -> $LOG_OUT"
qemu-system-x86_64 \
    -drive format=raw,file="$IMAGE_OUT" \
    -serial file:"$LOG_OUT" \
    -serial mon:stdio \
    -no-reboot \
    -no-shutdown

echo ""
echo "Build complete."
