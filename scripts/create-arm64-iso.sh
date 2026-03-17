#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:?missing build dir}"
IMAGE_DIR="${2:?missing image dir}"

ARCH_DIR="${BUILD_DIR}/kernel"
KERNEL_ELF="${ARCH_DIR}/vibos-arm64.elf"
ISO_PATH="${IMAGE_DIR}/vibos-arm64.iso"

STAGING_DIR="${BUILD_DIR}/arm64-iso"
EFI_DIR="${STAGING_DIR}/EFI/BOOT"
BOOTAA64_EFI="${EFI_DIR}/BOOTAA64.EFI"

mkdir -p "${IMAGE_DIR}"
rm -rf "${STAGING_DIR}"
mkdir -p "${EFI_DIR}"

if [ ! -f "${KERNEL_ELF}" ]; then
    echo "[ERROR] ARM64 kernel not found: ${KERNEL_ELF}"
    exit 1
fi

if ! command -v llvm-objcopy >/dev/null 2>&1; then
    echo "[ERROR] llvm-objcopy not found"
    exit 1
fi

if ! command -v xorriso >/dev/null 2>&1; then
    echo "[ERROR] xorriso not found"
    exit 1
fi

echo "[IMAGE] Staging ARM64 UEFI ISO tree..."
cp "${KERNEL_ELF}" "${STAGING_DIR}/vibos-arm64.elf"

echo "[IMAGE] Converting kernel ELF to BOOTAA64.EFI..."
llvm-objcopy \
    -O efi-app-aarch64 \
    "${KERNEL_ELF}" \
    "${BOOTAA64_EFI}"

echo "[IMAGE] Creating ARM64 ISO: ${ISO_PATH}"
xorriso -as mkisofs \
    -R -r -J \
    -V "VIBOS_ARM64" \
    -o "${ISO_PATH}" \
    -efi-boot EFI/BOOT/BOOTAA64.EFI \
    -no-emul-boot \
    "${STAGING_DIR}"

echo "[IMAGE] ARM64 ISO created: ${ISO_PATH}"
