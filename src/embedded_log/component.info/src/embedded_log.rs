//! Embed a small text log directly into the boot image from within the kernel.

extern crate alloc;

use alloc::format;
use crate::bootinfo::BootInfo;

use crate::fs;
use crate::serial;

pub const LOG_VIRTUAL_PATH: &str = "logs/embedded.log";

/// Capture a short boot snapshot and write it into the embedded log slot.
pub fn embed_boot_log(boot_info: &BootInfo, heap_label: &str) {
    let fb_line = "framebuffer: unavailable\n";
    let cmdline = if !boot_info.cmdline_ptr.is_null() && boot_info.cmdline_len > 0 {
        let bytes = unsafe { core::slice::from_raw_parts(boot_info.cmdline_ptr, boot_info.cmdline_len) };
        core::str::from_utf8(bytes).unwrap_or("")
    } else {
        ""
    };

    let log_text = format!(
        "OS-Kernel-Edition boot log\n\
        heap: {}\n\
        {}\n\
        cmdline: {}\n\
        note: embedded by kernel into {}\n",
        heap_label,
        fb_line,
        cmdline,
        LOG_VIRTUAL_PATH
    );

    match fs::embed_log(LOG_VIRTUAL_PATH, log_text.as_bytes()) {
        Ok(_) => serial::log_line("[log] embedded boot log into image payload"),
        Err(e) => serial::log_line(e),
    }
}
