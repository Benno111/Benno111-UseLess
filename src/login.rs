use crate::{accounts, framebuffer, vga_buffer};

const DEFAULT_USER: &str = "admin";
const DEFAULT_PASS: &str = "pass123";

pub fn run_login_screen() -> bool {
    framebuffer::clear_screen();
    framebuffer::draw_text("== Login ==", 2, 2);
    framebuffer::draw_text("User: admin", 2, 6);
    framebuffer::draw_text("Pass: ******", 2, 10);
    framebuffer::draw_text("(auto-login demo)", 2, 14);
    framebuffer::render_frame();

    vga_buffer::log_line("[login] attempting auto-login as admin");
    if !accounts::authenticate(DEFAULT_USER, DEFAULT_PASS) {
        vga_buffer::log_line("[login] credentials invalid");
        framebuffer::draw_text("Login failed", 2, 18);
        framebuffer::render_frame();
        return false;
    }

    framebuffer::draw_text("Login successful", 2, 18);
    framebuffer::render_frame();
    vga_buffer::log_line("[login] login successful");
    true
}
