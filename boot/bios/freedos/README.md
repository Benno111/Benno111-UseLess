The DOS setup path uses local FreeDOS assets only.

Build behavior:
- Vendored source code lives under `boot/bios/freedos/source/`.
- Place source-built FreeDOS outputs in `boot/bios/freedos/out/` or point the
  build at them with environment variables.
- Vendored package copies live under `boot/bios/freedos/vendor/packages/`.
- Local bootable FreeDOS media should be stored under
  `boot/bios/freedos/vendor/media/`.
- Required assets are:
  `fd-lite.img` or `fd-x86.img` for disk-image builds, and `SHSUCDX.COM` plus
  `UDVD2.SYS` for the standalone ISO boot flow.
- `scripts/prepare-freedos-source-assets.sh` resolves those assets and can
  optionally invoke a source-tree build command via `FREEDOS_BUILD_COMMAND`.
- The resolver auto-builds `SHSUCDX.COM` and `UDVD2.SYS` from the vendored
  source tree when `nasm` is available locally.
- The resolver also auto-extracts `FD14LITE.img` and `FD14BOOT.img` from the
  vendored FreeDOS release-media ZIPs when those package copies are present.
- If source-built assets are absent, the helper fails and asks you to provide
  the required local files.
- The DOS image and ISO scripts patch the chosen FreeDOS image with
  OS8-specific startup hooks. They preserve the original FreeDOS startup
  files and append `CALL OS8AUTO.BAT` plus any required config lines, instead of
  replacing the shipped `FDAUTO.BAT`, `AUTOEXEC.BAT`, `FDCONFIG.SYS`, or
  `CONFIG.SYS` wholesale.
- The ISO boot flow loads FreeDOS CD support through `UDVD2.SYS` and
  `SHSUCDX.COM`, then auto-runs `OSINST.COM` from the ISO's `/DOS` folder.

The installer utility and OS image stay on the ISO:
- `/DOS/OSINST.COM`
- `/DOS/OSSYS.IMG`

Relevant environment variables:
- `FREEDOS_OUTPUT_DIR`
- `FREEDOS_VENDOR_DIR`
- `FREEDOS_VENDOR_PACKAGES_DIR`
- `FREEDOS_VENDOR_MEDIA_DIR`
- `FREEDOS_MEDIA_IMAGE`
- `FREEDOS_SHSUCDX_COM`
- `FREEDOS_UDVD2_SYS`
- `FREEDOS_SOURCE_ROOT`
- `FREEDOS_BUILD_COMMAND`
