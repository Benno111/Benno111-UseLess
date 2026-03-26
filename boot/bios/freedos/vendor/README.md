# Vendored FreeDOS Assets

This directory stores repo-local FreeDOS inputs so the DOS setup path does not
need to download packages on every build.

Layout:

- `packages/` holds vendored upstream package archives already checked into the repo
- `media/` is where you place the local bootable FreeDOS media images used by the build

Current vendored packages:

- `kernel.zip`
- `freecom-source.zip`
- `shcdx308.zip`
- `udvd2.zip`

Build behavior:

- `scripts/prepare-freedos-source-assets.sh` automatically extracts
  `SHSUCDX.COM` and `UDVD2.SYS` from `packages/` into the build cache.
- The bootable DOS media image is not synthesized from those archives yet.
  You must provide a local image such as `fd-lite.img` or `fd-x86.img` under
  `media/`, or point the build at one with `FREEDOS_MEDIA_IMAGE`.
- If you maintain a local FreeDOS source tree that can build the required media
  image, set `FREEDOS_SOURCE_ROOT` and `FREEDOS_BUILD_COMMAND` so the build can
  generate fresh outputs from local code instead of downloading anything.
