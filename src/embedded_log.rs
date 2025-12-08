//! Embed a small text log directly into the boot image from within the kernel.

extern crate alloc;

use alloc::format;
use bootloader_api::BootInfo;

use crate::fs;
use crate::vga_buffer;

pub const LOG_VIRTUAL_PATH: &str = "logs/embedded.log";

/// Capture a short boot snapshot and write it into the embedded log slot.
pub fn embed_boot_log(boot_info: &BootInfo, heap_label: &str) {
    let fb_line = if let Some(fb) = boot_info.framebuffer.as_ref() {
        let info = fb.info();
        format!(
            "framebuffer: {}x{} {:?} stride={}\n",
            info.width, info.height, info.pixel_format, info.stride
        )
    } else {
        "framebuffer: unavailable\n".into()
    };

    let log_text = format!(
        "OS-Kernel-Edition boot log\n\
        heap: {}\n\
        {}\n\
        note: embedded by kernel into {}\n",
        heap_label,
        fb_line,
        LOG_VIRTUAL_PATH
    );

    match fs::embed_log(LOG_VIRTUAL_PATH, log_text.as_bytes()) {
        Ok(_) => vga_buffer::log_line("[log] embedded boot log into image payload"),
        Err(e) => vga_buffer::log_line(e),
    }
}
