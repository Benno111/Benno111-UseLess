use core::sync::atomic::{AtomicUsize, Ordering};

use crate::vga_buffer;

static FRAMEBUFFER_MISSING: AtomicUsize = AtomicUsize::new(0);
static RENDER_SKIP: AtomicUsize = AtomicUsize::new(0);

/// Note that rendering was skipped because the framebuffer was unavailable.
/// Logs a prediction if this repeats, since sustained missing video often
/// precedes panic paths elsewhere.
pub fn note_framebuffer_unavailable() {
    let n = FRAMEBUFFER_MISSING.fetch_add(1, Ordering::SeqCst) + 1;
    if n == 1 {
        vga_buffer::log_line("[predict] framebuffer missing once; rendering paused");
    } else if n == 3 {
        vga_buffer::log_line("[predict] crash likely: framebuffer still missing after 3 attempts");
    }
}

/// Note a skipped render for any other reason; repeat skips hint at stalled UI.
pub fn note_render_skip(reason: &str) {
    let n = RENDER_SKIP.fetch_add(1, Ordering::SeqCst) + 1;
    if n == 1 {
        vga_buffer::log_line(&alloc::format!("[predict] render skipped once ({reason})"));
    } else if n == 5 {
        vga_buffer::log_line(&alloc::format!(
            "[predict] crash risk: render skipped {} times (last: {reason})",
            n
        ));
    }
}
