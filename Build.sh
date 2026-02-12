#!/usr/bin/env bash
set -e

PROJECT_NAME="os-kernel-edition"
TARGET="x86_64-unknown-none"
KERNEL_ELF="target/$TARGET/release/$PROJECT_NAME"
LOG_OUT="build/serial.log"
QMP_SOCKET="/tmp/qmp-socket"
QMP_LOG="build/qmp-events.log"

# Ensure cargo/rustup-installed tools are on PATH (bootloader_linker, etc.)
export PATH="$HOME/.cargo/bin:$HOME/.rustup/toolchains/nightly-x86_64-unknown-linux-gnu/bin:$PATH"

echo "==============================="
echo "  OS-Kernel-Edition Build"
echo "==============================="
echo ""

# 1. CHECK RUST TOOLCHAIN
echo "[1/7] Ensuring nightly toolchain is available..."
if ! rustup toolchain list | rg -q "^nightly"; then
    echo "ERROR: nightly toolchain not installed. Run: rustup toolchain install nightly"
    exit 1
fi

echo "    Checking target $TARGET..."
if ! rustup target list --installed --toolchain nightly | rg -q "^${TARGET}$"; then
    echo "ERROR: target $TARGET missing. Run: rustup target add $TARGET --toolchain nightly"
    exit 1
fi

echo "    Checking LLVM tools..."
if ! rustup component list --installed --toolchain nightly | rg -q "^llvm-tools"; then
    echo "ERROR: llvm-tools-preview missing. Run: rustup component add llvm-tools-preview --toolchain nightly"
    exit 1
fi

# 2. BOOTLOADER
echo "[2/7] Skipping bootloader_linker (custom bootloader in use)"

# 3. BUILD KERNEL ELF
echo "[3/7] Building kernel ELF (release)..."
cargo +nightly build --target "$TARGET" --release

if [ ! -f "target/$TARGET/release/$PROJECT_NAME" ]; then
    echo "ERROR: Kernel ELF not found: $KERNEL_ELF"
    exit 1
fi
echo "    Kernel built successfully!"

# 4. PREP OUTPUT DIR
echo "[4/7] Preparing build directory..."
mkdir -p build
rm -f "$LOG_OUT" "$QMP_SOCKET" "$QMP_LOG"

# 5. BUILD BOOTLOADER IMAGES (custom)
echo "[5/7] Building custom bootloader images..."
if [ -x "bootloader/build_bios.sh" ]; then
    ./bootloader/build_bios.sh "$KERNEL_ELF"
else
    echo "    BIOS bootloader build script missing: bootloader/build_bios.sh"
fi
if [ -x "bootloader/build_uefi.sh" ]; then
    ./bootloader/build_uefi.sh "$KERNEL_ELF"
else
    echo "    UEFI bootloader build script missing: bootloader/build_uefi.sh"
fi

echo ""
echo "Build complete (kernel ELF + custom bootloader images if available)."
