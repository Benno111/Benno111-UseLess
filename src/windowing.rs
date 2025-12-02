use crate::framebuffer;
use spin::Mutex;

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
const CURSOR_W: usize = 20;
const CURSOR_H: usize = 20;
// Arrow cursor sprite with a small shadow, RGBA.
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
    let mut st = STATE.lock();
    st.windows = defaults;
}

/// Replace the current window list with a tiled set derived from app names.
pub fn set_app_windows(names: &[&'static str]) {
    let mut windows: [Option<Window>; MAX_WINDOWS] = [None; MAX_WINDOWS];
    // Floating layout: drop windows at a handful of positions and leave them there
    // until explicitly moved.
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
}

pub fn set_mouse_position(x: usize, y: usize) {
    let mut st = STATE.lock();
    let (w, h) = st.bounds;
    let clamped_x = x.min(w.saturating_sub(1));
    let clamped_y = y.min(h.saturating_sub(1));
    st.mouse = (clamped_x, clamped_y);
    framebuffer::invalidate();
}

pub fn move_mouse(dx: isize, dy: isize) {
    let mut st = STATE.lock();
    let (cur_x, cur_y) = st.mouse;
    // Speed up movement so small deltas move the cursor visibly.
    let accel = 4;
    // Smooth out jitter: apply a minimum of 1px but cap to keep visuals stable.
    let step_x = (dx * accel).clamp(-16, 16);
    let step_y = (dy * accel).clamp(-16, 16);
    let new_x = cur_x as isize + step_x;
    let new_y = cur_y as isize + step_y;
    let (bw, bh) = st.bounds;
    st.mouse = (
        clamp(new_x, 0, bw.saturating_sub(1) as isize) as usize,
        clamp(new_y, 0, bh.saturating_sub(1) as isize) as usize,
    );
    framebuffer::invalidate();
}

pub fn set_bounds(w: usize, h: usize) {
    STATE.lock().bounds = (w, h);
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
    if !crate::framebuffer::is_invalidated() {
        return;
    }
    // Draw background + windows + cursor.
    framebuffer::fill_screen(crate::framebuffer::COLOR_BG);

    framebuffer::lock_refresh();
    // Draw windows as outlined rectangles with titles.
    let st = STATE.lock();
    let windows_snapshot = st.windows;
    let (mx, my) = st.mouse;
    let frame_no = crate::framebuffer::next_frame_count();
    for win in windows_snapshot.iter().flatten() {
        framebuffer::draw_rect(win.x, win.y, win.w, win.h, (50, 120, 200));
        framebuffer::draw_rect(win.x + 2, win.y + 16, win.w - 4, win.h - 18, (20, 20, 20));
        framebuffer::draw_text(win.title, win.x + 6, win.y + 2);
    }
    // Overlay frame counter without triggering extra invalidation.
    framebuffer::draw_text_no_invalidate(&alloc::format!("Frame: {}", frame_no), 4, 4);
    // Draw mouse cursor.
    draw_cursor_sprite(mx, my);
    framebuffer::unlock_refresh();
    framebuffer::render_frame();
}

fn draw_cursor_sprite(x: usize, y: usize) {
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
