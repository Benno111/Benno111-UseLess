use crate::{accounts, framebuffer, vga_buffer};
use crate::framebuffer::{COLOR_BG, COLOR_CARD, COLOR_ACCENT, COLOR_TEXT, COLOR_SUB};

const DEFAULT_USER: &str = "admin";
const DEFAULT_PASS: &str = "pass123";

pub fn run_login_screen() -> bool {
    framebuffer::draw_wallpaper();
    framebuffer::draw_rect(30, 30, 300, 180, COLOR_CARD);
    framebuffer::draw_rect(32, 32, 296, 32, COLOR_ACCENT);
    framebuffer::draw_text("Login", 40, 38);
    framebuffer::draw_text("User", 40, 80);
    framebuffer::draw_rect(40, 92, 240, 20, COLOR_BG);
    framebuffer::draw_text("admin", 46, 96);
    framebuffer::draw_text("Pass", 40, 120);
    framebuffer::draw_rect(40, 132, 240, 20, COLOR_BG);
    framebuffer::draw_text("********", 46, 136);
    framebuffer::draw_text("Auto-login demo", 40, 160);
    framebuffer::render_frame();

    vga_buffer::log_line("[login] attempting auto-login as admin");
    if !accounts::authenticate(DEFAULT_USER, DEFAULT_PASS) {
        vga_buffer::log_line("[login] credentials invalid");
        framebuffer::draw_rect(40, 190, 140, 18, COLOR_ACCENT);
        framebuffer::draw_text("Login failed", 44, 194);
        framebuffer::render_frame();
        return false;
    }

    framebuffer::draw_rect(40, 190, 180, 18, COLOR_ACCENT);
    framebuffer::draw_text("Login successful", 44, 194);
    framebuffer::render_frame();
    vga_buffer::log_line("[login] login successful");
    true
}
