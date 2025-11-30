use crate::framebuffer;
use crate::vga_buffer;
use crate::windowing;
use alloc::format;
use crate::framebuffer::{COLOR_CARD, COLOR_ACCENT, COLOR_TEXT};

pub fn start() {
    let options = ["Calculator", "Settings", "Logs", "Power Off"];
    windowing::set_app_windows(&options);
    framebuffer::draw_rect(20, 12, 320, 60, COLOR_CARD);
    framebuffer::draw_text("Desktop apps (scripted selects 1,3)", 28, 20);
    framebuffer::render_frame();

    // Scripted selections to show input handling.
    let selections = [1usize, 3usize];
    let mut y = 36;
    for sel in selections {
        vga_buffer::log_line(&format!("[apps] selecting option {sel}"));
        framebuffer::draw_text(&format!("> {sel}"), 28, y);
        framebuffer::render_frame();
        y += 14;
    }
}
