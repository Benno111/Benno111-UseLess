# Vendored FreeDOS Source

This directory stores the repo-local FreeDOS source code used by the DOS setup
flow.

Included source trees/files:

- `kernel/`
- `freecom/`
- `shsucdx.nsm`
- `nasm.mac`
- `UDVD2.ASM`

Build behavior:

- `scripts/prepare-freedos-source-assets.sh` will build `SHSUCDX.COM` and
  `UDVD2.SYS` from this source tree automatically when `nasm` is available.
- The DOS boot media template still comes from the vendored FreeDOS release
  media archives in `../vendor/packages/`.
- If you have a broader local FreeDOS build workflow for producing `fd-lite.img`
  or `fd-x86.img`, point `FREEDOS_BUILD_COMMAND` at it.
