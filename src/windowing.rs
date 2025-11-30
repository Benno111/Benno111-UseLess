use crate::framebuffer;
use crate::vga_buffer;

#[derive(Clone, Copy)]
pub struct Window {
    pub title: &'static str,
    pub x: usize,
    pub y: usize,
    pub w: usize,
    pub h: usize,
}

pub const MAX_WINDOWS: usize = 4;

static mut WINDOWS: [Option<Window>; MAX_WINDOWS] = [None; MAX_WINDOWS];
#[allow(static_mut_refs)]
static mut MOUSE_POS: (usize, usize) = (20, 20);
#[allow(static_mut_refs)]
static mut FB_BOUNDS: (usize, usize) = (640, 480);
const CURSOR_W: usize = 16;
const CURSOR_H: usize = 16;
// Simple arrow cursor sprite (white with black border), RGBA.
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
    unsafe {
        WINDOWS = defaults;
    }
}

/// Replace the current window list with a tiled set derived from app names.
pub fn set_app_windows(names: &[&'static str]) {
    let mut windows: [Option<Window>; MAX_WINDOWS] = [None; MAX_WINDOWS];
    // Use framebuffer bounds to scale tiling a bit.
    let (w, h) = unsafe { FB_BOUNDS };
    let col_width = (w / 2).max(120);
    let row_height = (h / 2).max(100);
    for (i, name) in names.iter().take(MAX_WINDOWS).enumerate() {
        let col = i % 2;
        let row = i / 2;
        let x = 10 + col * (col_width + 10);
        let y = 20 + row * (row_height + 10);
        windows[i] = Some(Window {
            title: name,
            x,
            y,
            w: col_width.saturating_sub(20),
            h: row_height.saturating_sub(20),
        });
    }
    unsafe {
        WINDOWS = windows;
    }
}

pub fn set_mouse_position(x: usize, y: usize) {
    unsafe {
        let (w, h) = FB_BOUNDS;
        let clamped_x = x.min(w.saturating_sub(1));
        let clamped_y = y.min(h.saturating_sub(1));
        MOUSE_POS = (clamped_x, clamped_y);
    }
}

pub fn move_mouse(dx: isize, dy: isize) {
    unsafe {
        let (cur_x, cur_y) = MOUSE_POS;
        let new_x = cur_x as isize + dx;
        let new_y = cur_y as isize + dy;
        set_mouse_position(new_x.max(0) as usize, new_y.max(0) as usize);
    }
}

pub fn set_bounds(w: usize, h: usize) {
    unsafe {
        FB_BOUNDS = (w, h);
    }
}

pub fn render() {
    // Draw wallpaper + windows + cursor.
    framebuffer::draw_wallpaper();

    // Draw windows as outlined rectangles with titles.
    let windows_snapshot = unsafe { WINDOWS };
    for win in windows_snapshot.iter().flatten() {
        framebuffer::draw_rect(win.x, win.y, win.w, win.h, (50, 120, 200));
        framebuffer::draw_rect(win.x + 2, win.y + 16, win.w - 4, win.h - 18, (20, 20, 20));
        framebuffer::draw_text(win.title, win.x + 6, win.y + 2);
    }
    // Draw mouse cursor.
    let (mx, my) = unsafe { MOUSE_POS };
    draw_cursor_sprite(mx, my);
    framebuffer::render_frame();
    vga_buffer::log_line("[windowing] rendered windows and cursor");
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
            // Simple arrow: filled triangle with border.
            let filled = x <= y && x >= y.saturating_sub(4);
            let border = x == y || x == y.saturating_sub(1);
            if border {
                data[idx] = 0;
                data[idx + 1] = 0;
                data[idx + 2] = 0;
                data[idx + 3] = 255;
            } else if filled {
                data[idx] = 255;
                data[idx + 1] = 255;
                data[idx + 2] = 255;
                data[idx + 3] = 255;
            } else {
                data[idx + 3] = 0;
            }
            x += 1;
        }
        y += 1;
    }
    data
}
