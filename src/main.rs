//! Minimal bare-metal entry for `os-kernel-edition`.
//! Built as a real bootable x86_64 kernel using `bootloader_api`.

#![no_std]
#![no_main]

extern crate alloc;

use bootloader_api::{
    config::{BootloaderConfig, Mapping},
    entry_point, BootInfo,
};
use core::{panic::PanicInfo, str};

mod allocator;
mod accounts;
mod crash;
mod fonts;
mod commands;
mod framebuffer;
mod input;
mod windowing;
mod acpi;
mod qemu_drivers;
mod fs;
mod serial;
mod vga_buffer;
mod drivers;
mod task_manager;
mod crash_predictor;
mod pci;
mod usb;
mod embedded_log;

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
    let heap_result = unsafe { allocator::init_heap(boot_info) };
    let mut boot_step = 0usize;
    const BOOT_STEPS: usize = 9;
    boot_step += 1;
    let heap_label = if heap_result.used_fallback {
        "Heap initialized (fallback 64 KiB)"
    } else if heap_result.regions_added > 0 {
        "Heap initialized from memory map"
    } else {
        "Heap already initialized"
    };
    log_boot_progress(boot_step, BOOT_STEPS, heap_label);

    log_boot_info(boot_info);
    boot_step += 1;
    log_boot_progress(boot_step, BOOT_STEPS, "Boot info captured");

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
    boot_step += 1;
    log_boot_progress(boot_step, BOOT_STEPS, "Framebuffer configured");

    qemu_drivers::init();
    boot_step += 1;
    log_boot_progress(boot_step, BOOT_STEPS, "Virtual devices initialized");

    fs::init_main_volume();
    boot_step += 1;
    log_boot_progress(boot_step, BOOT_STEPS, "Main volume mounted");

    acpi::init();
    boot_step += 1;
    log_boot_progress(boot_step, BOOT_STEPS, "ACPI tables parsed");

    drivers::init();
    boot_step += 1;
    log_boot_progress(boot_step, BOOT_STEPS, "Device drivers ready");

    accounts::ensure_user("admin", "pass123");
    boot_step += 1;
    log_boot_progress(boot_step, BOOT_STEPS, "Accounts service initialized");

    // Straight to desktop render loop.
    windowing::init_default_windows();
    boot_step += 1;
    log_boot_progress(boot_step, BOOT_STEPS, "Desktop ready; entering render loop");
    embedded_log::embed_boot_log(boot_info, heap_label);
    run_main_loop();
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

fn log_boot_progress(step: usize, total_steps: usize, label: &str) {
    const BAR_WIDTH: usize = 24;
    let clamped_total = total_steps.max(1);
    let pct = (step.min(clamped_total) * 100) / clamped_total;
    let filled = (pct * BAR_WIDTH) / 100;
    let mut bar = [b'.'; BAR_WIDTH];
    for i in 0..filled.min(BAR_WIDTH) {
        bar[i] = b'#';
    }
    let bar_str = unsafe { str::from_utf8_unchecked(&bar) };
    let _ = framebuffer::draw_boot_splash(pct, label);
    vga_buffer::log(format_args!(
        "[boot {pct:3}%] [{bar_str}] {label}"
    ));
}

#[inline(never)]
fn run_main_loop() -> ! {
    loop {
        let frame = drivers::poll_devices();
        input::enqueue_events(frame.events);
        input::poll_input_events();
        windowing::render();
    }
}

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    crash::show_crash(info);
    loop {
        x86_64::instructions::hlt();
    }
}
