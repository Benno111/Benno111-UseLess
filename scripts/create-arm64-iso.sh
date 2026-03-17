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

if [ ! -f "${K
