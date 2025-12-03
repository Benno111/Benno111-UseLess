OS Kernel Edition — Minimal Bootable Rust Kernel (x86_64)

A clean, modern, and fully bootable bare-metal Rust kernel for x86_64 PCs.
This project uses the bootloader ecosystem to generate real bootable disk images (BIOS or UEFI) and provides a minimal foundation for building a custom OS or microkernel.

This repository contains:

A #![no_std], #![no_main] Rust kernel

VGA text-mode output at 0xb8000

Entry into 64-bit long mode (via bootloader)

A stable halt loop that halts the CPU until the next interrupt


The design emphasizes clarity, minimal dependencies, and expandability so you can grow this into a larger OS architecture (microkernel, monolithic, module-based loader, etc.).


---

Features

✔ Bare-metal Rust kernel

Runs without the standard library, allocator, or OS services.

✔ Boots on real or virtual hardware

The bootloader crate produces a valid bootable image for:

BIOS (legacy)

UEFI

QEMU

Bare-metal devices


✔ Early VGA driver

Writes text directly to the VGA text buffer at 0xb8000.

✔ Minimal runtime

The CPU is halted safely using a hardware hlt loop.


---

1. Requirements

Install these on your development machine (Ubuntu, Debian, Arch, etc.):

Rust toolchain

rustup component add rust-src
rustup target add x86_64-unknown-none

Build utilities

sudo apt install qemu-system-x86 nasm llvm lld

Nightly compiler (required by bootloader)

rustup default nightly


---

2. Building the Kernel

Build the bootable image

cargo bootimage

This produces:

target/x86_64-unknown-none/debug/bootimage-kernel.bin

You can write it to a USB stick or boot it via QEMU.


---

3. Running in QEMU

Standard run:

qemu-system-x86_64 -drive format=raw,file=target/x86_64-unknown-none/debug/bootimage-kernel.bin

With hardware acceleration (KVM):

qemu-system-x86_64 \
  -enable-kvm \
  -cpu host \
  -drive format=raw,file=target/.../bootimage-kernel.bin


---

4. Project Structure

src/
 ├── main.rs       # Kernel entry point (no_std, no_main)
 ├── vga_buffer.rs # Low-level VGA writer implementation
 └── lib.rs        # Core modules (if expanded later)

Boot process:

1. bootloader loads your kernel image


2. CPU enters long mode


3. Rust kernel starts execution at _start


4. Kernel initializes VGA text mode


5. Kernel enters a safe halted loop




---

5. Extending the Kernel

Here are natural next steps you can implement:

Interrupts & Exceptions

Interrupt Descriptor Table (IDT)

Page fault, double fault, breakpoint handlers


Memory Management

Paging structures via BootInfo

Higher-half kernel mapping

Heap allocator


Framebuffer & UI

Map framebuffer from bootloader_api

Minimal graphics driver

Draw text and primitives


Tasking / Scheduling

Cooperative or preemptive scheduler

Kernel tasks and user-space model


Drivers & Modules

PCI enumeration

PS/2 or USB input

Modular driver loader


This repo is intentionally minimal so you can grow the architecture in any direction (microkernel, monolithic, hybrid, capability-based, etc.).


---

6. References

bootloader

bootloader_api

bootloader_linker

Philipp Oppermann’s Writing an OS in Rust series



---

License

MIT — free to use, modify, or integrate into your own kernel or OS project.


---

If you'd like, I can also:

✅ Rewrite this README in a more formal / academic tone
✅ Add graphics or architecture diagrams
✅ Add contributor guidelines, code-of-conduct, or architecture docs
✅ Break this into README.md, docs/boot.md, and docs/memory.md

Just tell me your preferred style!