// Minimal bare-metal entry for `os-kernel-edition`.
// Real bootable x86_64 kernel using `bootloader_api`.

extern crate alloc;

use bootloader_api::{
    config::{BootloaderConfig, Mapping},
    entry_point, BootInfo,
};
use core::{panic::PanicInfo, str};
use core::sync::atomic::{AtomicUsize, Ordering};

#[path = "../../../allocator/component.info/src/allocator.rs"]
mod allocator;
#[path = "../../../accounts/component.info/src/accounts.rs"]
mod accounts;
#[path = "../../../crash/component.info/src/crash.rs"]
mod crash;
#[path = "../../../fonts/component.info/src/fonts.rs"]
mod fonts;
#[path = "../../../commands/component.info/src/commands.rs"]
mod commands;
#[path = "../../../framebuffer/component.info/src/framebuffer.rs"]
mod framebuffer;
#[path = "../../../input/component.info/src/input.rs"]
mod input;
#[path = "../../../windowing/component.info/src/windowing.rs"]
mod windowing;
#[path = "../../../acpi/component.info/src/acpi.rs"]
mod acpi;
#[path = "../../../drivers/component.info/src/qemu_drivers.rs"]
mod qemu_drivers;
#[path = "../../../fs/component.info/src/fs.rs"]
mod fs;
#[path = "../../../serial/component.info/src/serial.rs"]
mod serial;
#[path = "../../../vga_buffer/component.info/src/vga_buffer.rs"]
mod vga_buffer;
#[path = "../../../drivers/component.info/src/driver_api.rs"]
mod driver_api;
#[path = "../../../drivers/component.info/src/driver_ring.rs"]
mod driver_ring;
#[path = "../../../task_manager/component.info/src/task_manager.rs"]
mod task_manager;
#[path = "../../../crash_predictor/component.info/src/crash_predictor.rs"]
mod crash_predictor;
#[path = "../../../drivers/component.info/src/pci.rs"]
mod pci;
#[path = "../../../drivers/component.info/src/usb.rs"]
mod usb;
#[path = "../../../embedded_log/component.info/src/embedded_log.rs"]
mod embedded_log;

/* ============================================================
   BOOT CONFIG
============================================================ */

const KERNEL_CONFIG: BootloaderConfig = {
    let mut cfg = BootloaderConfig::new_default();
    cfg.mappings.physical_memory = Some(Mapping::Dynamic);
    cfg
};

entry_point!(kernel_main, config = &KERNEL_CONFIG);

/* ============================================================
   KERNEL ENTRY
============================================================ */

fn kernel_main(boot_info: &'static mut BootInfo) -> ! {
    use core::fmt::Write;

    crash::set_boot_info_ptr(boot_info as *mut _ as usize);

    // VGA + serial early logging
    vga_buffer::init(boot_info);
    serial::init();

    // Heap
    let heap_result = unsafe { allocator::init_heap(boot_info) };

    // Boot log
    let heap_label = if heap_result.used_fallback {
        "Heap initialized (fallback)"
    } else if heap_result.regions_added > 0 {
        "Heap initialized from memory map"
    } else {
        "Heap already initialized"
    };

    log_boot_info(boot_info);
    log_boot_progress(1, 10, heap_label);

    // Framebuffer
    let fb_opt: Option<&'static mut bootloader_api::info::FrameBuffer> =
        unsafe { core::mem::transmute(boot_info.framebuffer.as_mut()) };

    framebuffer::init(fb_opt);
    framebuffer::set_font_config(fonts::FontConfig {
        fg: (255, 255, 255),
        bg: Some((0, 0, 0)),
        scale: 2,
        letter_spacing: 2,
    });
    framebuffer::set_text_opacity(210);

    if let Some((w, h)) = framebuffer::framebuffer_size() {
        windowing::set_bounds(w, h);
    }

    log_boot_progress(2, 10, "Framebuffer initialized");

    // Devices
    qemu_drivers::init();
    log_boot_progress(3, 10, "Virtual devices ready");

    fs::init_main_volume();
    log_boot_progress(4, 10, "Filesystem mounted");

    acpi::init();
    log_boot_progress(5, 10, "ACPI initialized");

    driver_api::init();
    log_boot_progress(6, 10, "Driver layer ready");

    accounts::ensure_user("admin", "pass123");
    log_boot_progress(7, 10, "Accounts ready");

    // Desktop
    windowing::init_login_window();
    framebuffer::set_renderer(windowing::render);
    framebuffer::set_desktop_mode(true);
    framebuffer::set_force_swap(true);

    log_boot_progress(8, 10, "Desktop compositor ready");

    embedded_log::embed_boot_log(boot_info, heap_label);
    log_boot_progress(9, 10, "Boot log embedded");

    {
        let mut w = vga_buffer::writer();
        w.clear();
    }

    framebuffer::draw_desktop();
    framebuffer::render_frame();

    log_boot_progress(10, 10, "Entering desktop");

    run_main_loop();
}

/* ============================================================
   MAIN LOOP
============================================================ */

#[inline(never)]
fn run_main_loop() -> ! {
    static MAIN_TICK: AtomicUsize = AtomicUsize::new(0);

    loop {
        // Poll hardware
        let frame = driver_api::poll_devices();

        // Input pipeline
        input::enqueue_events(frame.events);
        input::poll_input_events();

        // Window manager render
        framebuffer::render_registered();

        // Heartbeat
        if windowing::desktop_active() {
            let tick = MAIN_TICK.fetch_add(1, Ordering::Relaxed) + 1;
            if tick % 600 == 0 {
                vga_buffer::log_line("[main] desktop running");
            }
        }
    }
}

/* ============================================================
   BOOT UI
============================================================ */

fn log_boot_info(boot_info: &BootInfo) {
    vga_buffer::log_line("[boot] kernel entry");

    match boot_info.physical_memory_offset.into_option() {
        Some(off) => vga_buffer::log(format_args!(
            "[boot] physical memory mapped at offset 0x{off:016x}"
        )),
        None => vga_buffer::log_line("[boot] physical memory offset not provided"),
    }

    vga_buffer::log(format_args!(
        "[boot] memory regions: {}",
        boot_info.memory_regions.len()
    ));

    if let Some(fb) = boot_info.framebuffer.as_ref() {
        let info = fb.info();
        vga_buffer::log(format_args!(
            "[boot] framebuffer {}x{} {:?}, {} bpp, stride {}",
            info.width,
            info.height,
            info.pixel_format,
            info.bytes_per_pixel,
            info.stride
        ));
    }

    vga_buffer::log(format_args!(
        "[boot] kernel image: addr=0x{:x}, len={} bytes",
        boot_info.kernel_addr,
        boot_info.kernel_len
    ));
}

fn log_boot_progress(step: usize, total: usize, label: &str) {
    const BAR_WIDTH: usize = 24;

    let pct = (step * 100) / total.max(1);
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

/* ============================================================
   PANIC HANDLER
============================================================ */

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    crash::show_crash(info);
    loop {
        x86_64::instructions::hlt();
    }
}
