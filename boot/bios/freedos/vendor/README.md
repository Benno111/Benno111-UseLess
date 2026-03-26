# Vendored FreeDOS Assets

This directory stores repo-local FreeDOS inputs so the DOS setup path does not
need to download packages on every build.

Layout:

- `packages/` holds vendored upstream package archives already checked into the repo
- `media/` is where you place the local bootable FreeDOS media images used by the build

Current vendored packages:

- `FD14-LiteUSB.zip`
- `FD14-LegacyCD.zip`

Build behavior:

- `scripts/prepare-freedos-source-assets.sh` can extract `FD14LITE.img` and
  `FD14BOOT.img` from the vendored FreeDOS release-media ZIPs.
- FreeDOS source code lives in `../source/` and is used for local tool builds.
- You can still provide your own local image such as `fd-lite.img` or
  `fd-x86.img` under `media/`, or point the build at one with
  `FREEDOS_MEDIA_IMAGE`.
- If you maintain a local FreeDOS source tree that can build the required media
  image, set `FREEDOS_SOURCE_ROOT` and `FREEDOS_BUILD_COMMAND` so the build can
  generate fresh outputs from local code instead of downloading anything.
