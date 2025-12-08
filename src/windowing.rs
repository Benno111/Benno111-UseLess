use crate::framebuffer;
use crate::task_manager;
use crate::crash_predictor;
use crate::vga_buffer;
use spin::Mutex;
use core::sync::atomic::{AtomicBool, Ordering};

#[derive(Clone, Copy)]
pub struct Window {
    pub title: &'static str,
    pub x: usize,
    pub y: usize,
    pub w: usize,
    pub h: usize,
}

pub const MAX_WINDOWS: usize = 4;

type WindowRender = fn(&Window);

#[derive(Clone, Copy)]
struct WindowEntry {
    window: Window,
    render: WindowRender,
}

struct State {
    windows: [Option<WindowEntry>; MAX_WINDOWS],
    mouse: (usize, usize),
    bounds: (usize, usize),
}

static STATE: Mutex<State> = Mutex::new(State {
    windows: [None; MAX_WINDOWS],
    mouse: (20, 20),
    bounds: (640, 480),
});
static RENDERING: AtomicBool = AtomicBool::new(false);
static FB_WARNED: AtomicBool = AtomicBool::new(false);

struct RenderGuard;

impl RenderGuard {
    fn new(flag: &'static AtomicBool) -> Option<Self> {
        if flag.swap(true, Ordering::SeqCst) {
            None
        } else {
            Some(Self)
        }
    }
}

impl Drop for RenderGuard {
    fn drop(&mut self) {
        RENDERING.store(false, Ordering::SeqCst);
    }
}

struct RefreshGuard(bool);

impl RefreshGuard {
    fn new() -> Self {
        framebuffer::lock_refresh();
        Self(true)
    }
    fn release(&mut self) {
        if self.0 {
            framebuffer::unlock_refresh();
            self.0 = false;
        }
    }
}

impl Drop for RefreshGuard {
    fn drop(&mut self) {
        if self.0 {
            framebuffer::unlock_refresh();
            self.0 = false;
        }
    }
}

const CURSOR_W: usize = 20;
const CURSOR_H: usize = 20;
const CURSOR_SPRITE: [u8; CURSOR_W * CURSOR_H * 4] = generate_cursor_sprite();

pub fn init_default_windows() {
    clear_windows();
    let _ = register_window(
        Window {
            title: "Console",
            x: 10,
            y: 30,
            w: 240,
            h: 120,
        },
        render_console,
    );
    let _ = register_window(
        Window {
            title: "Apps",
            x: 270,
            y: 40,
            w: 220,
            h: 120,
        },
        render_apps,
    );
    let _ = register_window(
        Window {
            title: "Logs",
            x: 60,
            y: 170,
            w: 360,
            h: 120,
        },
        render_logs,
    );
    let _ = register_window(
        Window {
            title: "Task Manager",
            x: 420,
            y: 40,
            w: 260,
            h: 180,
        },
        render_task_manager,
    );
    framebuffer::invalidate();
}

/// Replace the current window list with a tiled set derived from app names.
#[allow(dead_code)]
pub fn set_app_windows(names: &[&'static str]) {
    clear_windows();
    const FLOAT_POS: &[(usize, usize)] = &[(40, 50), (220, 80), (120, 220), (300, 180)];
    for (i, name) in names.iter().take(MAX_WINDOWS).enumerate() {
        let (x, y) = FLOAT_POS.get(i).copied().unwrap_or((30 + i * 40, 40 + i * 30));
        let _ = register_window(
            Window {
                title: name,
                x,
                y,
                w: 220,
                h: 140,
            },
            render_placeholder,
        );
    }
    framebuffer::invalidate();
}

#[allow(dead_code)]
pub fn set_mouse_position(x: usize, y: usize) {
    let mut st = STATE.lock();
    let (w, h) = st.bounds;
    st.mouse = (x.min(w.saturating_sub(1)), y.min(h.saturating_sub(1)));
    framebuffer::invalidate();
}

pub fn move_mouse(dx: isize, dy: isize) {
    let mut st = STATE.lock();
    let (cur_x, cur_y) = st.mouse;
    let (bw, bh) = st.bounds;
    let new_x = clamp(cur_x as isize + dx, 0, bw.saturating_sub(1) as isize) as usize;
    let new_y = clamp(cur_y as isize + dy, 0, bh.saturating_sub(1) as isize) as usize;
    st.mouse = (new_x, new_y);
    framebuffer::invalidate();
}

pub fn mouse_click_left() {
    framebuffer::invalidate();
}

pub fn mouse_click_right() {
    framebuffer::invalidate();
}

pub fn set_bounds(w: usize, h: usize) {
    STATE.lock().bounds = (w, h);
    framebuffer::invalidate();
}

/// Move a window by index by the given delta (floating manager behavior).
#[allow(dead_code)]
pub fn move_window(idx: usize, dx: isize, dy: isize) {
    let mut st = STATE.lock();
    let (bw, bh) = st.bounds;
    if let Some(entry) = st.windows.get_mut(idx).and_then(|w| w.as_mut()) {
        let win = &mut entry.window;
        let new_x = clamp(win.x as isize + dx, 0, bw.saturating_sub(win.w) as isize);
        let new_y = clamp(win.y as isize + dy, 0, bh.saturating_sub(win.h) as isize);
        win.x = new_x as usize;
        win.y = new_y as usize;
        framebuffer::invalidate();
    }
}

fn clamp(v: isize, min: isize, max: isize) -> isize {
    if v < min {
        min
    } else if v > max {
        max
    } else {
        v
    }
}

pub fn render() {
    let Some(_render_guard) = RenderGuard::new(&RENDERING) else {
        return;
    };
    if !framebuffer::framebuffer_available() {
        if !FB_WARNED.swap(true, Ordering::SeqCst) {
            vga_buffer::log_line("[windowing] framebuffer unavailable; redraw skipped");
        }
        crash_predictor::note_framebuffer_unavailable();
        return;
    }
    // Always render; if nothing marked dirty, force a refresh so UI stays live.
    if !framebuffer::is_invalidated() {
        framebuffer::invalidate();
    }
    framebuffer::fill_screen(crate::framebuffer::COLOR_BG);
    let mut refresh_guard = RefreshGuard::new();
    let st = STATE.lock();
    let windows_snapshot = st.windows;
    let (mx, my) = st.mouse;
    for entry in windows_snapshot.iter().flatten() {
        let win = entry.window;
        framebuffer::draw_rect(win.x, win.y, win.w, win.h, (50, 120, 200));
        framebuffer::draw_rect(win.x + 2, win.y + 16, win.w - 4, win.h - 18, (20, 20, 20));
        framebuffer::draw_text(win.title, win.x + 6, win.y + 2);
        (entry.render)(&win);
    }
    draw_cursor(mx, my);
    refresh_guard.release();
    framebuffer::render_frame();
}

fn draw_cursor(x: usize, y: usize) {
    framebuffer::blit_rgba(x, y, CURSOR_W, CURSOR_H, &CURSOR_SPRITE);
}

// ===== Window app API =====

/// Clear all windows.
pub fn clear_windows() {
    STATE.lock().windows = [None; MAX_WINDOWS];
}

/// Register a window with a render callback. Returns its slot index if inserted.
pub fn register_window(win: Window, render: WindowRender) -> Option<usize> {
    let mut st = STATE.lock();
    if let Some((idx, slot)) = st.windows.iter_mut().enumerate().find(|(_, s)| s.is_none()) {
        *slot = Some(WindowEntry { window: win, render });
        framebuffer::invalidate();
        Some(idx)
    } else {
        None
    }
}

fn render_console(win: &Window) {
    framebuffer::draw_text("Console ready", win.x + 8, win.y + 28);
}

fn render_apps(win: &Window) {
    framebuffer::draw_text("Apps placeholder", win.x + 8, win.y + 28);
}

fn render_logs(win: &Window) {
    framebuffer::draw_text("Logs placeholder", win.x + 8, win.y + 28);
}

fn render_task_manager(win: &Window) {
    task_manager::render(
        win.x + 6,
        win.y + 20,
        win.w.saturating_sub(12),
        win.h.saturating_sub(26),
    );
}

fn render_placeholder(win: &Window) {
    framebuffer::draw_text(win.title, win.x + 8, win.y + 28);
}

const fn generate_cursor_sprite() -> [u8; CURSOR_W * CURSOR_H * 4] {
    let mut data = [0u8; CURSOR_W * CURSOR_H * 4];
    let mut y = 0;
    while y < CURSOR_H {
        let mut x = 0;
        while x < CURSOR_W {
            let idx = (y * CURSOR_W + x) * 4;
            // Simple arrow with a faint shadow.
            let shadow = x + 2 <= y + 2 && x + 2 >= y.saturating_sub(6) && y + 2 < CURSOR_H;
            let filled = x <= y && x >= y.saturating_sub(6);
            let border = x == y || x + 1 == y;
            if border {
                data[idx] = 0;
                data[idx + 1] = 0;
                data[idx + 2] = 0;
                data[idx + 3] = 255;
            } else if filled {
                data[idx] = 245;
                data[idx + 1] = 245;
                data[idx + 2] = 245;
                data[idx + 3] = 255;
            } else if shadow {
                data[idx] = 0;
                data[idx + 1] = 0;
                data[idx + 2] = 0;
                data[idx + 3] = 90;
            } else {
                data[idx + 3] = 0;
            }
            x += 1;
        }
        y += 1;
    }
    data
}
