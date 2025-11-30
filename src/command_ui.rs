use alloc::format;
use crate::{commands, framebuffer, fonts, vga_buffer};

/// Render a simple command input screen and execute a scripted set of commands.
pub fn show_command_screen(cmds: &[&str]) {
    framebuffer::clear_screen();
    framebuffer::draw_text("== Command Input ==", 2, 2);
    framebuffer::draw_text("Inputs (scripted demo)", 2, 10);

    let line_h = (fonts::FONT_H as usize * 2) + 4;
    let mut y = 18;
    for cmd in cmds {
        // Show the "typed" input line before executing.
        framebuffer::draw_text(&format!("> {cmd}"), 2, y);
        framebuffer::render_frame();
        y += line_h;
        vga_buffer::log_line(&format!("[cmd-ui] running: {cmd}"));
        commands::run_command(cmd);
    }
    framebuffer::render_frame();
}
