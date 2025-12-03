
use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use core::str;

use bootloader_api::info::{FrameBuffer, FrameBufferInfo};
use spin::Mutex;

use crate::vga_buffer;
use crate::fonts::{self, FontConfig, FONT_W};

// Simple flat palette for UI.
pub const COLOR_BG: (u8, u8, u8) = (16, 18, 24);
pub const COLOR_CARD: (u8, u8, u8) = (30, 33, 40);
pub const COLOR_ACCENT: (u8, u8, u8) = (82, 156, 255);
const WALL_W: usize = 960;
const WALL_H: usize = 720;
const WALLPAPER: &[u8] = include_bytes!("../assets/ui/wallpaper.raw");
const LOG_FB: bool = false;

struct FbState {
    buffer: &'static mut [u8],
    info: FrameBufferInfo,
}

static FB: Mutex<Option<FbState>> = Mutex::new(None);
static FONT_CFG: Mutex<FontConfig> = Mutex::new(FontConfig {
    fg: (255, 255, 255),
    bg: None,
    scale: 1,
    letter_spacing: 1,
});
static INVALIDATED: AtomicBool = AtomicBool::new(true);
static REFRESH_LOCK: AtomicBool = AtomicBool::new(false);
static FRAME_COUNT: AtomicUsize = AtomicUsize::new(0);

/// Mark the framebuffer as dirty so the renderer will present on the next cycle.
pub fn invalidate() {
    INVALIDATED.store(true, Ordering::SeqCst);
}

pub fn init(fb: Option<&mut FrameBuffer>) {
    if let Some(fb) = fb {
        let info = fb.info();
        // SAFETY: bootloader-provided framebuffer lives for the whole kernel lifetime.
        let buffer: &'static mut [u8] = unsafe { core::mem::transmute(fb.buffer_mut()) };
        *FB.lock() = Some(FbState { buffer, info });
        draw_wallpaper();
        draw_text("Desktop Initialized", 2, 2);
        vga_buffer::log_line("[FB] Framebuffer initialized.");
    } else {
        vga_buffer::log_line("[FB] No framebuffer provided; rendering disabled");
    }
    INVALIDATED.store(true, Ordering::SeqCst);
}

pub fn clear_screen() {
    if with_fb(|buf, info| {
        let bg = FONT_CFG.lock().bg;
        let bpp = info.bytes_per_pixel;
        for chunk in buf.chunks_exact_mut(bpp) {
            if let Some((r, g, b)) = bg {
                set_pixel_bytes(chunk, bpp, r, g, b, info);
            } else {
                // default to black if no bg configured
                set_pixel_bytes(chunk, bpp, 0, 0, 0, info);
            }
        }
    }) {
        // ok
    } else {
        let mut w = vga_buffer::writer();
        w.clear();
    }
    vga_buffer::log_line("[FB] Clear screen");
}

pub fn draw_text(text: &str, x: usize, y: usize) {
    // Crude rectangle-per-character renderer to avoid font data.
    if with_fb(|buf, info| {
        let cfg = *FONT_CFG.lock();
        for (i, ch) in text.bytes().enumerate() {
            let x_offset = x + i * ((FONT_W as u8 * cfg.scale + cfg.letter_spacing) as usize);
            draw_glyph(buf, info, x_offset, y, ch, &cfg);
        }
    }) {
        // ok
    } else {
        let mut w = vga_buffer::writer();
        w.write_at(x, y, text);
    }
    if LOG_FB {
        vga_buffer::log_line("[FB] Draw text");
    }
}

/// Draw text without marking the framebuffer invalid (for overlays after other draws).
pub fn draw_text_no_invalidate(text: &str, x: usize, y: usize) {
    let _ = with_fb(|buf, info| {
        let cfg = *FONT_CFG.lock();
        for (i, ch) in text.bytes().enumerate() {
            let x_offset = x + i * ((FONT_W as u8 * cfg.scale + cfg.letter_spacing) as usize);
            draw_glyph(buf, info, x_offset, y, ch, &cfg);
        }
    });
}

pub fn render_frame() {
    // Centralized present: only act when something marked the buffer dirty.
    if !INVALIDATED.swap(false, Ordering::SeqCst) {
        return;
    }
    if REFRESH_LOCK.load(Ordering::SeqCst) {
        // Re-mark as dirty so we try again after unlock.
        INVALIDATED.store(true, Ordering::SeqCst);
        return;
    }
    draw_frame_counter(4, 4);
    if LOG_FB {
        vga_buffer::log_line("[FB] Frame rendered.");
    }
}

/// Increment and fetch the next frame count for overlays.
pub fn next_frame_count() -> usize {
    FRAME_COUNT.fetch_add(1, Ordering::SeqCst) + 1
}

fn draw_frame_counter(x: usize, y: usize) {
    let n = next_frame_count();
    let mut buf = [0u8; 32];
    let mut idx = buf.len();
    let mut val = n;
    if val == 0 {
        idx -= 1;
        buf[idx] = b'0';
    } else {
        while val > 0 && idx > 0 {
            let digit = (val % 10) as u8;
            idx -= 1;
            buf[idx] = b'0' + digit;
            val /= 10;
        }
    }
    // prefix "Frame: "
    let prefix = b"Frame: ";
    let mut text = [0u8; 32];
    let mut tlen = 0;
    for &b in prefix.iter() {
        text[tlen] = b;
        tlen += 1;
    }
    let digits = &buf[idx..];
    for &b in digits {
        text[tlen] = b;
        tlen += 1;
    }
    let s = unsafe { str::from_utf8_unchecked(&text[..tlen]) };
    draw_text_no_invalidate(s, x, y);
}

/// Check if the framebuffer has pending changes.
pub fn is_invalidated() -> bool {
    INVALIDATED.load(Ordering::SeqCst)
}

/// Prevent refresh from presenting while long operations are running.
pub fn lock_refresh() {
    REFRESH_LOCK.store(true, Ordering::SeqCst);
}

/// Allow refresh again.
pub fn unlock_refresh() {
    REFRESH_LOCK.store(false, Ordering::SeqCst);
}

pub fn set_font_config(cfg: FontConfig) {
    *FONT_CFG.lock() = cfg;
    INVALIDATED.store(true, Ordering::SeqCst);
}

/// Current framebuffer dimensions, if initialized.
pub fn framebuffer_size() -> Option<(usize, usize)> {
    FB.lock().as_ref().map(|fb| (fb.info.width, fb.info.height))
}

/// Fill the entire framebuffer with a solid color.
pub fn fill_screen(color: (u8, u8, u8)) {
    let _ = with_fb(|buf, info| {
        let bpp = info.bytes_per_pixel;
        let stride = info.stride * bpp;
        for chunk in buf.chunks_exact_mut(stride) {
            for px in chunk.chunks_exact_mut(bpp) {
                write_pixel(px, bpp, color.0, color.1, color.2, info);
            }
        }
    });
    INVALIDATED.store(true, Ordering::SeqCst);
}

pub fn read_pixel(px: &[u8], info: &FrameBufferInfo) -> (u8, u8, u8) {
    match info.pixel_format {
        bootloader_api::info::PixelFormat::Rgb => {
            if px.len() >= 3 {
                (px[0], px[1], px[2])
            } else {
                (0, 0, 0)
            }
        }
        bootloader_api::info::PixelFormat::Bgr => {
            if px.len() >= 3 {
                (px[2], px[1], px[0])
            } else {
                (0, 0, 0)
            }
        }
        _ => (px.get(0).copied().unwrap_or(0), 0, 0),
    }
}

pub fn write_pixel(px: &mut [u8], bpp: usize, r: u8, g: u8, b: u8, info: &FrameBufferInfo) {
    set_pixel_bytes(px, bpp, r, g, b, info);
}

pub fn draw_rect(x: usize, y: usize, w: usize, h: usize, color: (u8, u8, u8)) {
    let _ = with_fb(|buf, info| {
        // top/bottom
        for yy in y..y.saturating_add(h) {
            if yy >= info.height {
                break;
            }
            for xx in x..x.saturating_add(w) {
                if xx >= info.width {
                    break;
                }
                let alpha = if yy == y || yy + 1 == y + h || xx == x || xx + 1 == x + w { 255 } else { 128 };
                let bpp = info.bytes_per_pixel;
                let pix_idx = yy * info.stride * bpp + xx * bpp;
                if let Some(px) = buf.get_mut(pix_idx..pix_idx + bpp) {
                    let (pr, pg, pb) = read_pixel(px, info);
                    let inv = 255 - alpha as u16;
                    let nr = ((color.0 as u16 * alpha as u16 + pr as u16 * inv) / 255) as u8;
                    let ng = ((color.1 as u16 * alpha as u16 + pg as u16 * inv) / 255) as u8;
                    let nb = ((color.2 as u16 * alpha as u16 + pb as u16 * inv) / 255) as u8;
                    write_pixel(px, bpp, nr, ng, nb, info);
                }
            }
        }
    });
    INVALIDATED.store(true, Ordering::SeqCst);
}

#[allow(dead_code)]
pub fn draw_cursor(x: usize, y: usize, color: (u8, u8, u8)) {
    let size = 6;
    let _ = with_fb(|buf, info| {
        for dx in 0..size {
            draw_pixel(buf, info, x + dx, y, color);
        }
        for dy in 0..size {
            draw_pixel(buf, info, x, y + dy, color);
        }
    });
    INVALIDATED.store(true, Ordering::SeqCst);
}

/// Blit an RGBA buffer at the given position.
pub fn blit_rgba(x: usize, y: usize, w: usize, h: usize, data: &[u8]) {
    let expected = w.saturating_mul(h).saturating_mul(4);
    if data.len() < expected {
        return;
    }
    let _ = with_fb(|buf, info| {
        for row in 0..h {
            if y + row >= info.height {
                break;
            }
            for col in 0..w {
                if x + col >= info.width {
                    break;
                }
                let idx = (row * w + col) * 4;
                let r = data[idx];
                let g = data[idx + 1];
                let b = data[idx + 2];
                let a = data[idx + 3];
                if a == 0 {
                    continue;
                }
                let alpha = a as u16;
                let inv = 255 - a as u16;

                let bpp = info.bytes_per_pixel;
                let pix_idx = (y + row) * info.stride * bpp + (x + col) * bpp;
                if let Some(px) = buf.get_mut(pix_idx..pix_idx + bpp) {
                    let (pr, pg, pb) = read_pixel(px, info);
                    let nr = ((r as u16 * alpha + pr as u16 * inv) / 255) as u8;
                    let ng = ((g as u16 * alpha + pg as u16 * inv) / 255) as u8;
                    let nb = ((b as u16 * alpha + pb as u16 * inv) / 255) as u8;
                    write_pixel(px, bpp, nr, ng, nb, info);
                }
            }
        }
    });
    INVALIDATED.store(true, Ordering::SeqCst);
}

/// Paint the wallpaper scaled to the framebuffer.
pub fn draw_wallpaper() {
    let _ = with_fb(|buf, info| {
        if WALLPAPER.len() < WALL_W * WALL_H * 4 {
            return;
        }
        let w = info.width;
        let h = info.height;
        for y in 0..h {
            let sy = y * WALL_H / h;
            for x in 0..w {
                let sx = x * WALL_W / w;
                let idx = (sy * WALL_W + sx) * 4;
                let r = WALLPAPER[idx];
                let g = WALLPAPER[idx + 1];
                let b = WALLPAPER[idx + 2];
                draw_pixel(buf, info, x, y, (r, g, b));
            }
        }
    });
    INVALIDATED.store(true, Ordering::SeqCst);
}

pub(crate) fn with_fb<F: FnOnce(&mut [u8], &FrameBufferInfo)>(f: F) -> bool {
    if let Some(fb) = FB.lock().as_mut() {
        f(fb.buffer, &fb.info);
        true
    } else {
        false
    }
}

fn draw_glyph(buf: &mut [u8], info: &FrameBufferInfo, x: usize, y: usize, ch: u8, cfg: &FontConfig) {
    let rows = fonts::glyph_rows(ch);
    let scale = cfg.scale.max(1) as usize;
    for (row_idx, row_bits) in rows.iter().enumerate() {
        let row_top = y + row_idx * scale;
        if row_top >= info.height {
            break;
        }
        for col in 0..5 {
            if row_bits & (1 << (4 - col)) != 0 {
                for sy in 0..scale {
                    for sx in 0..scale {
                        draw_pixel(
                            buf,
                            info,
                            x + col * scale + sx,
                            row_top + sy,
                            cfg.fg,
                        );
                    }
                }
            } else if let Some(bg) = cfg.bg {
                for sy in 0..scale {
                    for sx in 0..scale {
                        draw_pixel(
                            buf,
                            info,
                            x + col * scale + sx,
                            row_top + sy,
                            bg,
                        );
                    }
                }
            }
        }
    }
}

fn draw_pixel(buf: &mut [u8], info: &FrameBufferInfo, x: usize, y: usize, color: (u8, u8, u8)) {
    if x >= info.width || y >= info.height {
        return;
    }
    let bpp = info.bytes_per_pixel;
    let idx = y * info.stride * bpp + x * bpp;
    if let Some(px) = buf.get_mut(idx..idx + bpp) {
        set_pixel_bytes(px, bpp, color.0, color.1, color.2, info);
    }
}

fn set_pixel_bytes(px: &mut [u8], bpp: usize, r: u8, g: u8, b: u8, info: &FrameBufferInfo) {
    match info.pixel_format {
        bootloader_api::info::PixelFormat::Rgb => {
            if bpp >= 3 {
                px[0] = r;
                px[1] = g;
                px[2] = b;
            }
        }
        bootloader_api::info::PixelFormat::Bgr => {
            if bpp >= 3 {
                px[0] = b;
                px[1] = g;
                px[2] = r;
            }
        }
        bootloader_api::info::PixelFormat::U8 => {
            if let Some(first) = px.first_mut() {
                *first = r;
            }
        }
        bootloader_api::info::PixelFormat::Unknown { .. } => {
            if !px.is_empty() {
                px[0] = r;
            }
        }
        _ => {
            if !px.is_empty() {
                px[0] = r;
            }
        }
    }
}