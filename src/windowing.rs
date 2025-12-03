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
const CURSOR_MAX_BPP: usize = 4;
// Arrow cursor sprite with a small shadow, RGBA.
const CURSOR_SPRITE: [u8; CURSOR_W * CURSOR_H * 4] = generate_cursor_sprite();
struct CursorCache {
    pos: (usize, usize),
    w: usize,
    h: usize,
    bpp: usize,
    data: [u8; CURSOR_W * CURSOR_H * CURSOR_MAX_BPP],
}

static CURSOR_CACHE: Mutex<Option<CursorCache>> = Mutex::new(None);

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
    let new_mouse = (
        clamp(new_x, 0, bw.saturating_sub(1) as isize) as usize,
        clamp(new_y, 0, bh.saturating_sub(1) as isize) as usize,
    );
    st.mouse = new_mouse;
    drop(st);

    // Only invalidate the union of the old and new cursor rectangles.
    let min_x = cur_x.min(new_mouse.0);
    let min_y = cur_y.min(new_mouse.1);
    let max_x = cur_x
        .saturating_add(CURSOR_W)
        .max(new_mouse.0.saturating_add(CURSOR_W));
    let max_y = cur_y
        .saturating_add(CURSOR_H)
        .max(new_mouse.1.saturating_add(CURSOR_H));
    let w = max_x.saturating_sub(min_x);
    let h = max_y.saturating_sub(min_y);
    if w == 0 || h == 0 {
        framebuffer::invalidate();
    } else {
        framebuffer::invalidate_rect(min_x, min_y, w, h);
    }
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
    let dirty = match framebuffer::take_dirty_region() {
        Some(d) => d,
        None => return,
    };

    match dirty {
        framebuffer::DirtyRegion::Full => full_redraw(),
        framebuffer::DirtyRegion::Rect { .. } => {
            if !refresh_cursor_only() {
                full_redraw();
            }
        }
    }
}

fn full_redraw() {
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
    draw_cursor_with_cache(mx, my, false);
    framebuffer::unlock_refresh();
    framebuffer::render_frame();
}

fn refresh_cursor_only() -> bool {
    let (mx, my) = {
        let st = STATE.lock();
        st.mouse
    };
    draw_cursor_with_cache(mx, my, true)
}

fn draw_cursor_with_cache(x: usize, y: usize, restore_old: bool) -> bool {
    let mut cache = CURSOR_CACHE.lock();
    let mut ok = false;
    let _ = framebuffer::with_fb(|buf, info| {
        let bpp = info.bytes_per_pixel;
        if bpp == 0 || bpp > CURSOR_MAX_BPP {
            return;
        }
        // Compute stride in bytes defensively (bootloader stride is in pixels, but guard anyway).
        let mut stride = info.stride.saturating_mul(bpp);
        if stride == 0 {
            return;
        }
        let line_capacity = buf.len() / info.height.max(1);
        if stride > line_capacity && info.stride > 0 {
            stride = info.stride;
        }
        if stride == 0 {
            return;
        }

        if restore_old {
            if let Some(prev) = cache.as_ref() {
                if prev.bpp == bpp {
                    for row in 0..prev.h {
                        if prev.pos.1 + row >= info.height {
                            break;
                        }
                        let src_offset = row * prev.w * bpp;
                        let dst_idx = (prev.pos.1 + row) * stride + prev.pos.0 * bpp;
                        let dst_end = dst_idx + prev.w * bpp;
                        if dst_end <= buf.len() && src_offset + prev.w * bpp <= prev.data.len() {
                            let src = &prev.data[src_offset..src_offset + prev.w * bpp];
                            buf[dst_idx..dst_end].copy_from_slice(src);
                        }
                    }
                }
            }
        }

        let max_line_px = stride / bpp;
        if max_line_px == 0 {
            return;
        }
        let region_w = CURSOR_W.min(info.width.min(max_line_px).saturating_sub(x));
        let region_h = CURSOR_H.min(info.height.saturating_sub(y));
        if region_w == 0 || region_h == 0 {
            *cache = None;
            return;
        }

        let mut next_cache = CursorCache {
            pos: (x, y),
            w: region_w,
            h: region_h,
            bpp,
            data: [0; CURSOR_W * CURSOR_H * CURSOR_MAX_BPP],
        };

        // Capture background under cursor.
        for row in 0..region_h {
            let dst_idx = (y + row) * stride + x * bpp;
            let len = region_w * bpp;
            let src_start = row * CURSOR_W * bpp;
            if dst_idx + len <= buf.len() && src_start + len <= next_cache.data.len() {
                next_cache.data[src_start..src_start + len]
                    .copy_from_slice(&buf[dst_idx..dst_idx + len]);
            }
        }

        // Draw cursor sprite with alpha blending.
        for row in 0..region_h {
            for col in 0..region_w {
                let sprite_idx = (row * CURSOR_W + col) * 4;
                let a = CURSOR_SPRITE[sprite_idx + 3];
                if a == 0 {
                    continue;
                }
                let r = CURSOR_SPRITE[sprite_idx];
                let g = CURSOR_SPRITE[sprite_idx + 1];
                let b = CURSOR_SPRITE[sprite_idx + 2];
                let alpha = a as u16;
                let inv = 255 - a as u16;
                let dst_idx = (y + row) * stride + (x + col) * bpp;
                if let Some(px) = buf.get_mut(dst_idx..dst_idx + bpp) {
                    let (pr, pg, pb) = framebuffer::read_pixel(px, info);
                    let nr = ((r as u16 * alpha + pr as u16 * inv) / 255) as u8;
                    let ng = ((g as u16 * alpha + pg as u16 * inv) / 255) as u8;
                    let nb = ((b as u16 * alpha + pb as u16 * inv) / 255) as u8;
                    framebuffer::write_pixel(px, bpp, nr, ng, nb, info);
                }
            }
        }

        *cache = Some(next_cache);
        ok = true;
    });
    ok
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
