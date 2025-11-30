use core::sync::atomic::{AtomicUsize, Ordering};
use alloc::format;
use alloc::string::String;
use alloc::string::ToString;

use crate::framebuffer::{self, COLOR_ACCENT, COLOR_CARD, COLOR_TEXT};
use crate::vga_buffer;
use bootloader_api::BootInfo;

static BOOT_INFO_PTR: AtomicUsize = AtomicUsize::new(0);

pub fn set_boot_info_ptr(ptr: usize) {
    BOOT_INFO_PTR.store(ptr, Ordering::SeqCst);
}

pub fn show_crash(info: &core::panic::PanicInfo) {
    vga_buffer::log_line("[crash] fatal panic, rendering crash screen");
    framebuffer::draw_wallpaper();
    framebuffer::draw_rect(20, 20, 360, 200, COLOR_CARD);
    framebuffer::draw_rect(22, 22, 356, 26, COLOR_ACCENT);
    framebuffer::draw_text("System Crash", 28, 26);

    let mut y = 56;
    framebuffer::draw_text("Reason:", 28, y);
    y += 14;
    framebuffer::draw_text(&truncate(&format!("{}", info), 50), 28, y);
    y += 20;

    if let Some(bi) = boot_info() {
        framebuffer::draw_text("Dump:", 28, y);
        y += 14;
        framebuffer::draw_text(
            &format!(
                "mem regions: {}  kernel: 0x{:x} len:{}",
                bi.memory_regions.len(),
                bi.kernel_addr,
                bi.kernel_len
            ),
            28,
            y,
        );
        y += 14;
        framebuffer::draw_text(
            &format!(
                "phys offset: {:x}  image off: {:x}",
                bi.physical_memory_offset.into_option().unwrap_or(0),
                bi.kernel_image_offset
            ),
            28,
            y,
        );
        y += 14;
        framebuffer::draw_text(
            &format!(
                "framebuffer: {}x{}",
                bi.framebuffer.as_ref().map(|f| f.info().width).unwrap_or(0),
                bi.framebuffer.as_ref().map(|f| f.info().height).unwrap_or(0)
            ),
            28,
            y,
        );
        y += 14;
    }

    framebuffer::draw_text("System halted. Check serial log for details.", 28, y + 8);
    framebuffer::render_frame();
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
