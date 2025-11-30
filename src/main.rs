//! Minimal bare-metal entry for `os-kernel-edition`.
//! Built as a real bootable x86_64 kernel using `bootloader_api`.

#![no_std]
#![no_main]

use bootloader_api::{entry_point, BootInfo};
use core::panic::PanicInfo;

mod vga_buffer;

entry_point!(kernel_main);

fn kernel_main(_boot_info: &'static mut BootInfo) -> ! {
    use core::fmt::Write;

    {
        let mut writer = vga_buffer::writer();
        let _ = write!(
            writer,
            "OS-Kernel-Edition booted via bootloader\n                 --------------------------------------\n"
        );
        let _ = write!(
            writer,
            "Welcome, Benno111!\n                 This is a real bare-metal kernel now.\n"
        );
    }

    // Main kernel loop – halt until next interrupt.
    loop {
        x86_64::instructions::hlt();
    }
}

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    use core::fmt::Write;

    {
        let mut writer = vga_buffer::writer();
        let _ = writeln!(writer, "");
        let _ = writeln!(writer, "================ KERNEL PANIC ================");
        let _ = writeln!(writer, "{info}");
    }

    loop {
        x86_64::instructions::hlt();
    }
}
