use core::sync::atomic::{AtomicUsize, Ordering};
use alloc::format;
use alloc::string::String;
use alloc::string::ToString;
use core::fmt::Write;
use core::sync::atomic::AtomicBool;

use crate::vga_buffer;
use bootloader_api::BootInfo;

static BOOT_INFO_PTR: AtomicUsize = AtomicUsize::new(0);
static CRASHED: AtomicBool = AtomicBool::new(false);

pub fn set_boot_info_ptr(ptr: usize) {
    BOOT_INFO_PTR.store(ptr, Ordering::SeqCst);
}

pub fn show_crash(info: &core::panic::PanicInfo) {
    if CRASHED.swap(true, Ordering::SeqCst) {
        // Already handling a crash; avoid recursive panic paths.
        loop {
            x86_64::instructions::hlt();
        }
    }
    let reason = truncate(&format!("{}", info), 200);
    vga_buffer::log_line(&format!("[crash] fatal panic: {}", reason));
    let mut w = vga_buffer::writer();
    w.clear();
    let _ = write!(
        w,
        "=== KERNEL PANIC ===\nReason: {}\n\n",
        truncate(&reason, 70)
    );

    if let Some(bi) = boot_info() {
        let _ = write!(
            w,
            "mem regions: {}  kernel: 0x{:x} len:{}\nphys offset: {:x}  image off: {:x}\nframebuffer: {}x{}\n\n",
            bi.memory_regions.len(),
            bi.kernel_addr,
            bi.kernel_len,
            bi.physical_memory_offset.into_option().unwrap_or(0),
            bi.kernel_image_offset,
            bi.framebuffer.as_ref().map(|f| f.info().width).unwrap_or(0),
            bi.framebuffer.as_ref().map(|f| f.info().height).unwrap_or(0),
        );
    }

    let _ = write!(
        w,
        "System halted. Check serial for full backtrace.\n\
Press Enter to reboot or power cycle the VM/device.\n> "
    );
}

fn boot_info() -> Option<&'static BootInfo> {
    let ptr = BOOT_INFO_PTR.load(Ordering::SeqCst);
    if ptr == 0 {
        None
    } else {
        Some(unsafe { &*(ptr as *const BootInfo) })
    }
}

fn truncate(s: &str, max: usize) -> String {
    if s.len() <= max {
        s.to_string()
    } else {
        let mut out = s[..max].to_string();
        out.push_str("...");
        out
    }
}
