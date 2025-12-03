use alloc::format;
use crate::framebuffer;
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

struct State {
    windows: [Option<Window>; MAX_WINDOWS],
    mouse: (usize, usize),
    bounds: (usize, usize),
}

static STATE: Mutex<State> = Mutex::new(State {
    windows: [None; MAX_WINDOWS],
    mouse: (20, 20),
    bounds: (640, 480),
});
static RENDERING: AtomicBool = AtomicBool::new(false);

const CURSOR_W: usize = 20;
const CURSOR_H: usize = 20;
const CURSOR_SPRITE: [u8; CURSOR_W * CURSOR_H * 4] = generate_cursor_sprite();

pub fn init_default_windows() {
    let defaults = [
        Some(Window {
            title: "Console",
            x: 10,
            y: 30,
            w: 240,
            h: 120,
        }),
        Some(Window {
            title: "Apps",
            x: 270,
            y: 40,
            w: 220,
            h: 120,
        }),
        Some(Window {
            title: "Logs",
            x: 60,
            y: 170,
            w: 360,
            h: 120,
        }),
        None,
    ];
    STATE.lock().windows = defaults;
    framebuffer::invalidate();
}

/// Replace the current window list with a tiled set derived from app names.
pub fn set_app_windows(names: &[&'static str]) {
    let mut windows: [Option<Window>; MAX_WINDOWS] = [None; MAX_WINDOWS];
    const FLOAT_POS: &[(usize, usize)] = &[(40, 50), (220, 80), (120, 220), (300, 180)];
    for (i, name) in names.iter().take(MAX_WINDOWS).enumerate() {
        let (x, y) = FLOAT_POS.get(i).copied().unwrap_or((30 + i * 40, 40 + i * 30));
        windows[i] = Some(Window {
            title: name,
            x,
            y,
            w: 220,
            h: 140,
        });
    }
    STATE.lock().windows = windows;
    framebuffer::invalidate();
}

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
pub fn move_window(idx: usize, dx: isize, dy: isize) {
    let mut st = STATE.lock();
    let (bw, bh) = st.bounds;
    if let Some(win) = st.windows.get_mut(idx).and_then(|w| w.as_mut()) {
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
    if RENDERING.swap(true, Ordering::SeqCst) {
        return;
    }
    if !framebuffer::is_invalidated() {
        RENDERING.store(false, Ordering::SeqCst);
        return;
    }
    framebuffer::fill_screen(crate::framebuffer::COLOR_BG);
    framebuffer::lock_refresh();
    let st = STATE.lock();
    let windows_snapshot = st.windows;
    let (mx, my) = st.mouse;
    for win in windows_snapshot.iter().flatten() {
        framebuffer::draw_rect(win.x, win.y, win.w, win.h, (50, 120, 200));
        framebuffer::draw_rect(win.x + 2, win.y + 16, win.w - 4, win.h - 18, (20, 20, 20));
        framebuffer::draw_text(win.title, win.x + 6, win.y + 2);
    }
    let frame_no = crate::framebuffer::next_frame_count();
    framebuffer::draw_text_no_invalidate(&format!("Frame: {}", frame_no), 4, 4);
    draw_cursor(mx, my);
    framebuffer::unlock_refresh();
    framebuffer::render_frame();
    RENDERING.store(false, Ordering::SeqCst);
}

fn draw_cursor(x: usize, y: usize) {
    framebuffer::blit_rgba(x, y, CURSOR_W, CURSOR_H, &CURSOR_SPRITE);
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
