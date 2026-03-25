The standalone DOS installer ISO now boots through a rebranded FreeDOS path
instead of the legacy custom stage1/stage2 loader.

Build behavior:
- `scripts/create-dos-installer-iso.sh` downloads the official FreeDOS 1.4
  Floppy Edition archive and extracts `fd-x86.img`.
- The script patches that image with OS8-specific `FDAUTO.BAT` and
  `FDCONFIG.SYS`.
- The boot flow loads FreeDOS CD support through `UDVD2.SYS` and
  `SHSUCDX.COM`, then auto-runs `OSINST.COM` from the ISO's `/DOS` folder.

The installer utility and OS image stay on the ISO:
- `/DOS/OSINST.COM`
- `/DOS/OSSYS.IMG`

Official FreeDOS sources used by the build are recorded in the ISO at
`/FREEDOS.TXT`.
