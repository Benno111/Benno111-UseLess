The DOS setup path prefers source-built FreeDOS assets, but unattended builds
can bootstrap from official FreeDOS release media when no local outputs are
present.

Build behavior:
- Place source-built FreeDOS outputs in `boot/bios/freedos/out/` or point the
  build at them with environment variables.
- Required assets are:
  `fd-lite.img` or `fd-x86.img` for disk-image builds, and `SHSUCDX.COM` plus
  `UDVD2.SYS` for the standalone ISO boot flow.
- `scripts/prepare-freedos-source-assets.sh` resolves those assets and can
  optionally invoke a source-tree build command via `FREEDOS_BUILD_COMMAND`.
- If source-built assets are absent, the helper falls back to official FreeDOS
  1.4 media:
  `FD14-LiteUSB.zip` for the disk-image path, `FD14-LegacyCD.zip` for the
  standalone ISO boot image, and the official `shcdx308.zip` / `udvd2.zip`
  packages for the CD-ROM drivers.
- The DOS image and ISO scripts patch the chosen FreeDOS image with
  OS8-specific `FDAUTO.BAT` and `FDCONFIG.SYS`.
- The ISO boot flow loads FreeDOS CD support through `UDVD2.SYS` and
  `SHSUCDX.COM`, then auto-runs `OSINST.COM` from the ISO's `/DOS` folder.

The installer utility and OS image stay on the ISO:
- `/DOS/OSINST.COM`
- `/DOS/OSSYS.IMG`

Relevant environment variables:
- `FREEDOS_OUTPUT_DIR`
- `FREEDOS_MEDIA_IMAGE`
- `FREEDOS_SHSUCDX_COM`
- `FREEDOS_UDVD2_SYS`
- `FREEDOS_SOURCE_ROOT`
- `FREEDOS_BUILD_COMMAND`
