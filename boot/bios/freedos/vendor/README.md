# Vendored FreeDOS Assets

This directory stores repo-local FreeDOS inputs so the DOS setup path does not
need to download packages on every build.

Layout:

- `packages/` holds small vendored local package archives used as tool fallbacks
- `media/` stores the local bootable FreeDOS media images used by the build

Current vendored packages:

- `shcdx308.zip`
- `udvd2.zip`

Current vendored media:

- `FD14BOOT.img`
- `FD14LITE.img`

Build behavior:

- `scripts/prepare-freedos-source-assets.sh` reads `FD14BOOT.img` and
  `FD14LITE.img` directly from `media/`.
- FreeDOS source code lives in `../source/` and is used for local tool builds.
- If a local source build of `SHSUCDX.COM` or `UDVD2.SYS` fails, the resolver
  falls back to the vendored local package ZIPs here.
- You can still provide your own local image such as `fd-lite.img` or
  `fd-x86.img` under `media/`, or point the build at one with
  `FREEDOS_MEDIA_IMAGE`.
- If you maintain a local FreeDOS source tree that can build the required media
  image, set `FREEDOS_SOURCE_ROOT` and `FREEDOS_BUILD_COMMAND` so the build can
  generate fresh outputs from local code instead of downloading anything.
