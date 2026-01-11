use crate::framebuffer;
use crate::task_manager;

use super::Window;

pub fn render_console(win: &Window) {
    framebuffer::draw_text("Console ready", win.x + 8, win.y + 28);
}

pub fn render_apps(win: &Window) {
    framebuffer::draw_text("Apps placeholder", win.x + 8, win.y + 28);
}

pub fn render_logs(win: &Window) {
    framebuffer::draw_text("Logs placeholder", win.x + 8, win.y + 28);
}

pub fn render_task_manager(win: &Window) {
    task_manager::render(
        win.x + 6,
        win.y + 20,
        win.w.saturating_sub(12),
        win.h.saturating_sub(26),
    );
}
