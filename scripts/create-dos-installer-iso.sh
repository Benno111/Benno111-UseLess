#!/bin/bash
# Create a standalone BIOS-bootable ISO for the DOS-based setup using FreeDOS.

set -e

BUILD_DIR="${1:-build/x86_64}"
IMAGE_DIR="${2:-image}"
ISO_NAME="${ISO_NAME:-os8-x86_64-dos-installer.iso}"
ISO_ROOT="${BUILD_DIR}/dos_installer_iso_root"
DOS_INSTALLER_COM="${DOS_INSTALLER_COM:-${BUILD_DIR}/boot/OSINST.COM}"
DOS_SYSTEM_IMAGE="${DOS_SYSTEM_IMAGE:-${IMAGE_DIR}/os8-x86_64-system.img}"
FREEDOS_CACHE_DIR="${BUILD_DIR}/freedos"
FREEDOS_BOOT_IMAGE="${FREEDOS_CACHE_DIR}/os8-freedos-boot.img"

GREEN='\033[0;32m'
NC='\033[0m'

log() {
    echo -e "${GREEN}[DOS-INSTALLER-ISO]${NC} $1"
}

link_or_copy() {
    local src="$1"
    local dst="$2"
    rm -f "$dst"
    if ! ln "$src" "$dst" 2>/dev/null; then
        cp "$src" "$dst"
    fi
}

require_file() {
    if [ ! -f "$1" ]; then
        echo "[ERROR] Required file not found: $1" >&2
        exit 1
    fi
}

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "[ERROR] Required command not found: $1" >&2
        exit 1
    fi
}

ensure_freedos_assets() {
    mkdir -p "$FREEDOS_CACHE_DIR"
    require_cmd mcopy
    require_cmd mdel
    require_cmd xorriso
    ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)" \
    BUILD_DIR="$BUILD_DIR" \
    FREEDOS_CACHE_DIR="$FREEDOS_CACHE_DIR" \
    FREEDOS_MEDIA_NAME="${FREEDOS_MEDIA_NAME:-fd-lite.img}" \
    FREEDOS_BOOT_MODE="${FREEDOS_BOOT_MODE:-legacycd}" \
    FREEDOS_REQUIRE_CD_DRIVERS=1 \
    . "$(cd "$(dirname "$0")" && pwd)/prepare-freedos-source-assets.sh"
}

prepare_freedos_boot_image() {
    cp "$FREEDOS_MEDIA_IMAGE" "$FREEDOS_BOOT_IMAGE"

    cat > "${FREEDOS_CACHE_DIR}/OS8AUTO.BAT" <<'EOF'
@ECHO OFF
CLS
ECHO.
ECHO   OS8 Setup
ECHO   Powered by FreeDOS
ECHO.
SHSUCDX /Q /D:OS8CD001 /L:R
IF EXIST R:\DOS\OSINST.COM GOTO RUN
ECHO The OS8 setup CD was not detected.
ECHO Attach the installer ISO and reboot.
GOTO DONE
:RUN
R:
CD \DOS
OSINST.COM
:DONE
ECHO.
ECHO Type EXIT to return to the FreeDOS shell.
EOF

    cat > "${FREEDOS_CACHE_DIR}/FDAUTO.BAT" <<'EOF'
@ECHO OFF
CALL OS8AUTO.BAT
EOF

    cat > "${FREEDOS_CACHE_DIR}/AUTOEXEC.BAT" <<'EOF'
@ECHO OFF
CALL OS8AUTO.BAT
EOF

    cat > "${FREEDOS_CACHE_DIR}/FDCONFIG.SYS" <<'EOF'
LASTDRIVE=Z
FILES=30
BUFFERS=20
DOS=HIGH
DEVICE=UDVD2.SYS /D:OS8CD001
EOF

    cat > "${FREEDOS_CACHE_DIR}/CONFIG.SYS" <<'EOF'
LASTDRIVE=Z
FILES=30
BUFFERS=20
DOS=HIGH
DEVICE=UDVD2.SYS /D:OS8CD001
EOF

    mdel -i "$FREEDOS_BOOT_IMAGE" ::/OS8AUTO.BAT >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/FDAUTO.BAT >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/AUTOEXEC.BAT >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/FDCONFIG.SYS >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/CONFIG.SYS >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/SHSUCDX.COM >/dev/null 2>&1 || true
    mdel -i "$FREEDOS_BOOT_IMAGE" ::/UDVD2.SYS >/dev/null 2>&1 || true

    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "${FREEDOS_CACHE_DIR}/OS8AUTO.BAT" ::/OS8AUTO.BAT
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "${FREEDOS_CACHE_DIR}/FDAUTO.BAT" ::/FDAUTO.BAT
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "${FREEDOS_CACHE_DIR}/AUTOEXEC.BAT" ::/AUTOEXEC.BAT
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "${FREEDOS_CACHE_DIR}/FDCONFIG.SYS" ::/FDCONFIG.SYS
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "${FREEDOS_CACHE_DIR}/CONFIG.SYS" ::/CONFIG.SYS
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "$FREEDOS_SHSUCDX_COM" ::/SHSUCDX.COM
    mcopy -o -i "$FREEDOS_BOOT_IMAGE" "$FREEDOS_UDVD2_SYS" ::/UDVD2.SYS

    if command -v mlabel >/dev/null 2>&1; then
        mlabel -i "$FREEDOS_BOOT_IMAGE" ::OS8FD14 >/dev/null 2>&1 || true
    fi
}

require_file "$DOS_INSTALLER_COM"
require_file "$DOS_SYSTEM_IMAGE"

ensure_freedos_assets
prepare_freedos_boot_image

mkdir -p "$IMAGE_DIR"
rm -rf "$ISO_ROOT"

log "Preparing FreeDOS-based installer ISO root at $ISO_ROOT"
mkdir -p "$ISO_ROOT/boot"
mkdir -p "$ISO_ROOT/dos"

link_or_copy "$FREEDOS_BOOT_IMAGE" "$ISO_ROOT/boot/os8-freedos.img"
link_or_copy "$DOS_INSTALLER_COM" "$ISO_ROOT/dos/OSINST.COM"
link_or_copy "$DOS_SYSTEM_IMAGE" "$ISO_ROOT/dos/OSSYS.IMG"

cat > "$ISO_ROOT/README.TXT" <<EOF
OS8 DOS Installer ISO

This ISO boots a rebranded FreeDOS environment and launches the OS8 setup
utility from the CD after loading FreeDOS CD-ROM support.

Included files:
- /boot/os8-freedos.img          : patched FreeDOS floppy boot image used for BIOS boot
- /dos/OSINST.COM                : OS8 DOS installer utility
- /dos/OSSYS.IMG                 : raw OS8 system image written by the installer

FreeDOS components supplied from source-built assets:
- $(basename "$FREEDOS_MEDIA_IMAGE")
- $(basename "$FREEDOS_SHSUCDX_COM")
- $(basename "$FREEDOS_UDVD2_SYS")
EOF

cat > "$ISO_ROOT/FREEDOS.TXT" <<EOF
FreeDOS Integration Notes

This installer ISO boots through source-built FreeDOS media and uses the
source-built FreeDOS CD-ROM stack:
- UDVD2.SYS
- SHSUCDX.COM

Resolved local assets:
- $FREEDOS_MEDIA_IMAGE
- $FREEDOS_SHSUCDX_COM
- $FREEDOS_UDVD2_SYS
EOF

ISO_PATH="${IMAGE_DIR}/${ISO_NAME}"
rm -f "$ISO_PATH"

log "Creating standalone BIOS FreeDOS installer ISO: $ISO_PATH"
xorriso -as mkisofs \
    -V "OS8-FD-SETUP" \
    -c boot/boot.cat \
    -b boot/os8-freedos.img \
    "$ISO_ROOT" \
    -o "$ISO_PATH"

log "Validating ISO contents..."
require_iso_path() {
    local iso_path="$1"
    local output
    output=$(xorriso -indev "$ISO_PATH" -find "$iso_path" -exec lsdl 2>/dev/null || true)
    if [ -z "$output" ]; then
        echo "[ERROR] Missing required ISO path: $iso_path" >&2
        exit 1
    fi
}

require_iso_path "/boot/os8-freedos.img"
require_iso_path "/dos/OSINST.COM"
require_iso_path "/dos/OSSYS.IMG"
require_iso_path "/README.TXT"
require_iso_path "/FREEDOS.TXT"

log "DOS installer ISO created successfully: $ISO_PATH"
ls -lh "$ISO_PATH"
echo ""
log "BIOS VM test: qemu-system-x86_64 -M q35 -m 512M -cdrom $ISO_PATH -serial stdio"
