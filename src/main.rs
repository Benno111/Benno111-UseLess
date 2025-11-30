//! Minimal bare-metal entry for `os-kernel-edition`.
//! Built as a real bootable x86_64 kernel using `bootloader_api`.

#![no_std]
#![no_main]

extern crate alloc;

use bootloader_api::{
    config::{BootloaderConfig, Mapping},
    entry_point, BootInfo,
};
use core::panic::PanicInfo;

mod allocator;
mod accounts;
mod crash;
mod fonts;
mod commands;
mod command_ui;
mod framebuffer;
mod input;
mod apps_launcher;
mod login;
mod windowing;
mod serial;
mod vga_buffer;

const KERNEL_CONFIG: BootloaderConfig = {
    let mut cfg = BootloaderConfig::new_default();
    // Map physical memory so we can reach the legacy VGA buffer at 0xb8000 through the offset.
    cfg.mappings.physical_memory = Some(Mapping::Dynamic);
    cfg
};

entry_point!(kernel_main, config = &KERNEL_CONFIG);

fn kernel_main(_boot_info: &'static mut BootInfo) -> ! {
    use core::fmt::Write;
    let boot_info = _boot_info;
    crash::set_boot_info_ptr(boot_info as *mut _ as usize);

    // Initialize VGA writer with the physical memory offset provided by the bootloader.
    vga_buffer::init(boot_info);
    serial::init();
    unsafe { allocator::init_heap() };
    vga_buffer::log_line("[boot] heap initialized (static 64 KiB)");

    log_boot_info(boot_info);

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

    // Initialize simple "desktop" rendering and run demo input/commands.
    let fb_opt: Option<&'static mut bootloader_api::info::FrameBuffer> =
        unsafe { core::mem::transmute(boot_info.framebuffer.as_mut()) };
    framebuffer::init(fb_opt);
    framebuffer::set_font_config(fonts::FontConfig {
        fg: (255, 255, 255),
        bg: Some((0, 0, 0)),
        scale: 2,
        letter_spacing: 2,
    });
    if let Some((w, h)) = framebuffer::framebuffer_size() {
        windowing::set_bounds(w, h);
    }
    accounts::ensure_user("admin", "pass123");

    if login::run_login_screen() {
        windowing::init_default_windows();
        input::enqueue_demo_inputs();
        // Show a simple command input screen and run scripted commands.
        command_ui::show_command_screen(&[
            "listusers",
            "login admin pass123",
            "useradd guest guest",
            "listusers",
        ]);
        apps_launcher::start();
        // Simple render loop to process inputs and keep the desktop refreshed.
        for _ in 0..8 {
            input::poll_input_events();
            windowing::render();
        }
    } else {
        vga_buffer::log_line("[kernel] login failed; halting");
    }

    // Main kernel loop – halt until next interrupt.
    loop {
        x86_64::instructions::hlt();
    }
}

fn log_boot_info(boot_info: &BootInfo) {
    vga_buffer::log_line("[boot] kernel entry");

    match boot_info.physical_memory_offset.into_option() {
        Some(off) => vga_buffer::log(format_args!(
            "[boot] physical memory mapped at offset 0x{off:016x}"
        )),
        None => vga_buffer::log_line("[boot] physical memory offset not provided"),
    }

    vga_buffer::log(format_args!(
        "[boot] memory regions provided: {}",
        boot_info.memory_regions.len()
    ));

    if let Some(fb) = boot_info.framebuffer.as_ref() {
        let info = fb.info();
        vga_buffer::log(format_args!(
            "[boot] framebuffer {}x{} {:?}, {} bytes_per_pixel, stride {}",
            info.width,
            info.height,
            info.pixel_format,
            info.bytes_per_pixel,
            info.stride
        ));
    } else {
        vga_buffer::log_line("[boot] framebuffer not available");
    }

    vga_buffer::log(format_args!(
        "[boot] kernel image: addr=0x{:x}, len={} bytes",
        boot_info.kernel_addr,
        boot_info.kernel_len
    ));
}

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    crash::show_crash(info);
    loop {
        x86_64::instructions::hlt();
    }
}
