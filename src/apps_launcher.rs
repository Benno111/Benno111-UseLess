use crate::framebuffer;
use crate::vga_buffer;
use alloc::format;

pub fn start() {
    let options = ["Calculator", "Settings", "Logs", "Power Off"];
    framebuffer::draw_text("== App Selector ==", 2, 4);
    let mut y = 10;
    for (i, app) in options.iter().enumerate() {
        framebuffer::draw_text(&format!("[{}] {}", i + 1, app), 4, y);
        y += 20;
    }
    framebuffer::draw_text("Input (scripted): 1,3", 2, y + 4);
    framebuffer::render_frame();

    // Scripted selections to show input handling.
    let selections = [1usize, 3usize];
    for sel in selections {
        vga_buffer::log_line(&format!("[apps] selecting option {sel}"));
        framebuffer::draw_text(&format!("> {sel}"), 2, y + 24);
        framebuffer::render_frame();
    }
}
