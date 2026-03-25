Place a bootable FreeDOS HDD image here as `fdboot.img` to make the DOS
installer boot through FreeDOS instead of the legacy custom stage1/stage2 path.

Requirements:
- The image must already boot into FreeDOS on its own.
- The image must be large enough to hold `OSINST.COM`, `OSSYS.IMG`,
  `AUTOEXEC.BAT`, and `CONFIG.SYS`.
- The build scripts will copy those files into the image root and let
  `AUTOEXEC.BAT` launch `OSINST.COM` automatically.

If `fdboot.img` is absent, the build falls back to the existing custom
real-mode DOS installer boot flow.
