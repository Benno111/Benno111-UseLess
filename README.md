# os-kernel-edition — real bootable Rust kernel

This is a **minimal x86_64 bare‑metal kernel** version of your `os-kernel-edition`
project. It uses the [`bootloader_api`] crate so it can be loaded by the
[`bootloader`] project (BIOS or UEFI) and booted like a real OS.

It currently:
- boots in 64‑bit long mode (handled by the bootloader)
- runs as a freestanding `#![no_std]` Rust binary
- prints a banner to the VGA text buffer at `0xb8000`
- loops forever, halting the CPU between interrupts

## 1. Prerequisites

On your dev machine (Ubuntu etc.):

```bash
rustup toolchain install nightly
rustup component add llvm-tools-preview --toolchain nightly
rustup target add x86_64-unknown-none --toolchain nightly
cargo install bootloader_linker
```

The `rust-toolchain.toml` in this repo pins the project to `nightly` and the
`x86_64-unknown-none` target.

## 2. Build the kernel ELF

From this directory:

```bash
cargo +nightly build
```

This produces a kernel binary at:

```text
target/x86_64-unknown-none/debug/os-kernel-edition
```

(or `release` if you use `--release`).

## 3. Create a bootable disk image

Use [`bootloader_linker`] to combine this kernel with the `bootloader` crate
into a BIOS/UEFI‑bootable disk image. Roughly:

```bash
bootloader_linker build         --kernel target/x86_64-unknown-none/debug/os-kernel-edition         --output os-kernel-edition.img
```

> ⚠ NOTE: Check `bootloader_linker`'s documentation (`bootloader_linker --help`
> or crates.io page) for the exact flags; they can change between versions.

Now you can boot it in QEMU:

```bash
qemu-system-x86_64 -drive format=raw,file=os-kernel-edition.img
```

You should see:

```text
OS-Kernel-Edition booted via bootloader
--------------------------------------
Welcome, Benno111!
This is a real bare-metal kernel now.
```

## 4. Next steps

From here you can:

- Add a proper interrupt descriptor table (IDT) and exception handlers
- Map the framebuffer from `BootInfo` and port your existing UI ideas
- Add a basic heap allocator and convert this into a micro‑kernel or monolith
- Implement a driver pack / module loader and IPC, like in your earlier designs

[`bootloader_api`]: https://docs.rs/bootloader_api
[`bootloader`]: https://github.com/rust-osdev/bootloader
[`bootloader_linker`]: https://lib.rs/crates/bootloader_linker
