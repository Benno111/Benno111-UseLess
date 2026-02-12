// Minimal bare-metal entry for `os-kernel-edition`.
// Real bootable x86_64 kernel using a custom bootloader ABI.

extern crate alloc;

use core::panic::PanicInfo;
use core::sync::atomic::{AtomicUsize, Ordering};

#[path = "../../../bootinfo/component.info/src/bootinfo.rs"]
mod bootinfo;
#[path = "../../../allocator/component.info/src/allocator.rs"]
mod allocator;
#[path = "../../../accounts/component.info/src/accounts.rs"]
mod accounts;
#[path = "../../../crash/component.info/src/crash.rs"]
mod crash;
#[path = "../../../commands/component.info/src/commands.rs"]
mod commands;
#[path = "../../../acpi/component.info/src/acpi.rs"]
mod acpi;
#[path = "../../../drivers/component.info/src/qemu_drivers.rs"]
mod qemu_drivers;
#[path = "../../../fs/component.info/src/fs.rs"]
mod fs;
#[path = "../../../serial/component.info/src/serial.rs"]
mod serial;
#[path = "../../../drivers/component.info/src/driver_api.rs"]
mod driver_api;
#[path = "../../../drivers/component.info/src/driver_ring.rs"]
mod driver_ring;
#[path = "../../../drivers/component.info/src/pci.rs"]
mod pci;
#[path = "../../../drivers/component.info/src/usb.rs"]
mod usb;
#[path = "../../../embedded_log/component.info/src/embedded_log.rs"]
mod embedded_log;

/* ============================================================
   KERNEL ENTRY
============================================================ */

#[no_mangle]
pub extern "C" fn kernel_main(boot_info: *mut bootinfo::BootInfo) -> ! {
    let boot_info = unsafe { &mut *boot_info };
    crash::set_boot_info_ptr(boot_info as *mut _ as usize);

    // VGA + serial early logging

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
    log_boot_progress(1, 6, heap_label);

    // Devices
    qemu_drivers::init();
    log_boot_progress(2, 6, "Virtual devices ready");

    fs::init_main_volume();
    log_boot_progress(3, 6, "Filesystem mounted");

    acpi::init();
    log_boot_progress(4, 6, "ACPI initialized");

    driver_api::init();
    log_boot_progress(5, 6, "Driver layer ready");

    accounts::ensure_user("admin", "pass123");
    log_boot_progress(6, 6, "Accounts ready");

    embedded_log::embed_boot_log(boot_info, heap_label);
    serial::log_line("[boot] entering headless mode");

    run_main_loop();
}

/* ============================================================
   MAIN LOOP
============================================================ */

#[inline(never)]
fn run_main_loop() -> ! {
    static MAIN_TICK: AtomicUsize = AtomicUsize::new(0);

    loop {
        let _ = driver_api::poll_devices();

        let tick = MAIN_TICK.fetch_add(1, Ordering::Relaxed) + 1;
        if tick % 2000 == 0 {
            serial::log_line("[main] headless loop running");
        }

        core::hint::spin_loop();
    }
}

/* ============================================================
   BOOT UI
============================================================ */

fn log_boot_info(boot_info: &bootinfo::BootInfo) {
    if !boot_info.cmdline_ptr.is_null() && boot_info.cmdline_len > 0 {
        let bytes = unsafe { core::slice::from_raw_parts(boot_info.cmdline_ptr, boot_info.cmdline_len) };
        if let Ok(s) = core::str::from_utf8(bytes) {
            serial::log_line(&alloc::format!("[boot] cmdline: {}", s));
        }
    }
}

fn log_boot_progress(step: usize, total: usize, label: &str) {
    let _ = (step, total, label);
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
