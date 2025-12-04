use alloc::{format, boxed::Box, vec, vec::Vec};
use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use core::str;

use bootloader_api::info::{FrameBuffer, FrameBufferInfo, PixelFormat};
use spin::Mutex;

use crate::vga_buffer;
use crate::fonts::{self, FontConfig, FONT_W};

// Simple flat palette for UI (no external assets).
pub const COLOR_BG: (u8, u8, u8) = (16, 18, 24);
pub const COLOR_CARD: (u8, u8, u8) = (30, 33, 40);
pub const COLOR_ACCENT: (u8, u8, u8) = (82, 156, 255);
const LOG_FB: bool = false;

struct FbState {
    /// Front buffer = actual VRAM from bootloader.
    front_buffer: &'static mut [u8],
    /// Back buffer = normal RAM; all drawing goes here.
    back_buffer: &'static mut [u8],
    info: FrameBufferInfo,
}

/// Simple dirty-rectangle accumulator (union of all changes).
#[derive(Clone, Copy)]
struct DirtyRect {
    x1: usize,
    y1: usize,
    x2: usize,
    y2: usize,
    valid: bool,
}

impl DirtyRect {
    const fn new() -> Self {
        Self {
            x1: 0,
            y1: 0,
            x2: 0,
            y2: 0,
            valid: false,
        }
    }
}

static FB: Mutex<Option<FbState>> = Mutex::new(None);
static DIRTY: Mutex<DirtyRect> = Mutex::new(DirtyRect::new());

static FONT_CFG: Mutex<FontConfig> = Mutex::new(FontConfig {
    fg: (255, 255, 255),
    bg: None,
    scale: 1,
    letter_spacing: 1,
});

static INVALIDATED: AtomicBool = AtomicBool::new(true);
static REFRESH_LOCK: AtomicBool = AtomicBool::new(false);
static FRAME_COUNT: AtomicUsize = AtomicUsize::new(0);

/// A rough glyph height estimate for dirty rects.
const FONT_H_EST: usize = 8;

/// Mark the framebuffer as dirty so the renderer will present on the next cycle.
pub fn invalidate() {
    INVALIDATED.store(true, Ordering::SeqCst);
}

/// Expand the global dirty rect to include (x, y, w, h).
fn mark_dirty_rect(x: usize, y: usize, w: usize, h: usize) {
    if w == 0 || h == 0 {
        return;
    }
    let (max_w, max_h) = match framebuffer_size() {
        Some(v) => v,
        None => return,
    };

    let x1 = x.min(max_w);
    let y1 = y.min(max_h);
    let x2 = (x.saturating_add(w)).min(max_w);
    let y2 = (y.saturating_add(h)).min(max_h);

    if x1 >= x2 || y1 >= y2 {
        return;
    }

    let mut dr = DIRTY.lock();
    if !dr.valid {
        dr.x1 = x1;
        dr.y1 = y1;
        dr.x2 = x2;
        dr.y2 = y2;
        dr.valid = true;
    } else {
        if x1 < dr.x1 {
            dr.x1 = x1;
        }
        if y1 < dr.y1 {
            dr.y1 = y1;
        }
        if x2 > dr.x2 {
            dr.x2 = x2;
        }
        if y2 > dr.y2 {
            dr.y2 = y2;
        }
    }
}

/// Mark the entire framebuffer as dirty.
fn mark_dirty_full() {
    if let Some((w, h)) = framebuffer_size() {
        mark_dirty_rect(0, 0, w, h);
    }
}

pub fn init(fb: Option<&mut FrameBuffer>) {
    if let Some(fb) = fb {
        let info = fb.info();
        // SAFETY: bootloader-provided framebuffer lives for the whole kernel lifetime.
        let front: &'static mut [u8] = unsafe { core::mem::transmute(fb.buffer_mut()) };

        // Allocate a backbuffer of the same size in normal RAM.
        let back_box: Box<[u8]> = vec![0u8; front.len()].into_boxed_slice();
        let back: &'static mut [u8] = Box::leak(back_box);

        *FB.lock() = Some(FbState {
            front_buffer: front,
            back_buffer: back,
            info,
        });

        // Draw initial background and text into the backbuffer.
        fill_screen(COLOR_BG);
        draw_text("Desktop Initialized", 2, 2);
        vga_buffer::log_line("[FB] Framebuffer initialized.");

        // Present initial contents.
        render_frame();
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
    mark_dirty_full();
    vga_buffer::log_line("[FB] Clear screen");
}

pub fn draw_text(text: &str, x: usize, y: usize) {
    // Rectangle-per-character renderer; now with dirty rect tracking.
    if with_fb(|buf, info| {
        let cfg = *FONT_CFG.lock();
        let step = (FONT_W as u8 * cfg.scale + cfg.letter_spacing) as usize;
        for (i, ch) in text.bytes().enumerate() {
            let x_offset = x + i * step;
            draw_glyph(buf, info, x_offset, y, ch, &cfg);
        }
    }) {
        // ok
    } else {
        let mut w = vga_buffer::writer();
        w.write_at(x, y, text);
    }

    // Approximate text bounding box to update dirty rect.
    let cfg = *FONT_CFG.lock();
    let step = (FONT_W as u8 * cfg.scale + cfg.letter_spacing) as usize;
    let width = text.len().saturating_mul(step);
    let height = FONT_H_EST.saturating_mul(cfg.scale.max(1) as usize);
    mark_dirty_rect(x, y, width, height);

    if LOG_FB {
        vga_buffer::log_line("[FB] Draw text");
    }
}

/// Draw text without marking the framebuffer invalid (for overlays after other draws).
/// Still expands the dirty rect so the overlay will be copied if some other draw invalidated.
pub fn draw_text_no_invalidate(text: &str, x: usize, y: usize) {
    let _ = with_fb(|buf, info| {
        let cfg = *FONT_CFG.lock();
        let step = (FONT_W as u8 * cfg.scale + cfg.letter_spacing) as usize;
        for (i, ch) in text.bytes().enumerate() {
            let x_offset = x + i * step;
            draw_glyph(buf, info, x_offset, y, ch, &cfg);
        }
    });

    let cfg = *FONT_CFG.lock();
    let step = (FONT_W as u8 * cfg.scale + cfg.letter_spacing) as usize;
    let width = text.len().saturating_mul(step);
    let height = FONT_H_EST.saturating_mul(cfg.scale.max(1) as usize);
    mark_dirty_rect(x, y, width, height);
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

    let mut fb_guard = FB.lock();
    let Some(fb) = fb_guard.as_mut() else {
        return;
    };

    let mut dr = DIRTY.lock();
    if !dr.valid {
        // No specific region known; fall back to full screen.
        dr.x1 = 0;
        dr.y1 = 0;
        dr.x2 = fb.info.width;
        dr.y2 = fb.info.height;
        dr.valid = true;
    }

    let bpp = fb.info.bytes_per_pixel;
    let stride = fb.info.stride;
    let width = fb.info.width;
    let height = fb.info.height;

    let x1 = dr.x1.min(width);
    let y1 = dr.y1.min(height);
    let x2 = dr.x2.min(width);
    let y2 = dr.y2.min(height);

    if x1 < x2 && y1 < y2 {
        let copy_w = x2 - x1;
        let row_bytes = copy_w * bpp;

        for y in y1..y2 {
            let row_off = y * stride;
            let start = (row_off + x1) * bpp;
            let end = start + row_bytes;
            if end > fb.front_buffer.len() || end > fb.back_buffer.len() {
                break;
            }
            fb.front_buffer[start..end].copy_from_slice(&fb.back_buffer[start..end]);
        }
    }

    dr.valid = false;
    FRAME_COUNT.fetch_add(1, Ordering::Relaxed);

    if LOG_FB {
        vga_buffer::log_line("[FB] Frame rendered.");
    }
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
    mark_dirty_full();
    INVALIDATED.store(true, Ordering::SeqCst);
}

/// Current framebuffer dimensions, if initialized.
pub fn framebuffer_size() -> Option<(usize, usize)> {
    FB.lock().as_ref().map(|fb| (fb.info.width, fb.info.height))
}

/// Fill the entire framebuffer (backbuffer) with a solid color.
///
/// Uses:
/// - AVX2 / NEON + raw-pointer unrolled loops for 32-bpp
/// - Scalar pattern fill for other formats.
pub fn fill_screen(color: (u8, u8, u8)) {
    let _ = with_fb(|buf, info| {
        let len = buf.len();
        let (r, g, b) = color;

        match info.pixel_format {
            PixelFormat::Rgb => {
                match info.bytes_per_pixel {
                    4 => {
                        // [R, G, B, 0] in little-endian u32
                        let val = u32::from_le_bytes([r, g, b, 0]);
                        unsafe { fast_fill_u32(buf.as_mut_ptr(), len, val) };
                    }
                    3 => {
                        scalar_pattern_fill(buf, &[r, g, b]);
                    }
                    1 => {
                        unsafe { fast_fill_u8(buf.as_mut_ptr(), len, r) };
                    }
                    _ => {
                        scalar_pattern_fill(buf, &[r, g, b]);
                    }
                }
            }
            PixelFormat::Bgr => {
                match info.bytes_per_pixel {
                    4 => {
                        // [B, G, R, 0] in little-endian u32
                        let val = u32::from_le_bytes([b, g, r, 0]);
                        unsafe { fast_fill_u32(buf.as_mut_ptr(), len, val) };
                    }
                    3 => {
                        scalar_pattern_fill(buf, &[b, g, r]);
                    }
                    1 => {
                        unsafe { fast_fill_u8(buf.as_mut_ptr(), len, b) };
                    }
                    _ => {
                        scalar_pattern_fill(buf, &[b, g, r]);
                    }
                }
            }
            PixelFormat::U8 => {
                let v = r; // treat as grayscale-ish
                unsafe { fast_fill_u8(buf.as_mut_ptr(), len, v) };
            }
            _ => {
                // Unknown formats → at least blast something.
                unsafe { fast_fill_u8(buf.as_mut_ptr(), len, r) };
            }
        }
    });

    mark_dirty_full();
    INVALIDATED.store(true, Ordering::SeqCst);
}

pub fn read_pixel(px: &[u8], info: &FrameBufferInfo) -> (u8, u8, u8) {
    match info.pixel_format {
        PixelFormat::Rgb => {
            if px.len() >= 3 {
                (px[0], px[1], px[2])
            } else {
                (0, 0, 0)
            }
        }
        PixelFormat::Bgr => {
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
        // top/bottom + simple alpha blend on interior
        for yy in y..y.saturating_add(h) {
            if yy >= info.height {
                break;
            }
            for xx in x..x.saturating_add(w) {
                if xx >= info.width {
                    break;
                }
                let alpha = if yy == y || yy + 1 == y + h || xx == x || xx + 1 == x + w {
                    255
                } else {
                    128
                };
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
    mark_dirty_rect(x, y, w, h);
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
    mark_dirty_rect(x, y, size, size);
    INVALIDATED.store(true, Ordering::SeqCst);
}

/// Blit an RGBA buffer at the given position (into backbuffer).
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
    mark_dirty_rect(x, y, w, h);
    INVALIDATED.store(true, Ordering::SeqCst);
}

fn blit_rgba_locked(
    buf: &mut [u8],
    info: &FrameBufferInfo,
    x: usize,
    y: usize,
    w: usize,
    h: usize,
    data: &[u8],
) {
    let expected = w.saturating_mul(h).saturating_mul(4);
    if data.len() < expected {
        return;
    }
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
    mark_dirty_rect(x, y, w, h);
}

pub fn draw_boot_splash(progress_pct: usize, label: &str) -> bool {
    let mut drawn = false;
    let mut label_pos = (12usize, 12usize);
    let _ = with_fb(|buf, info| {
        drawn = true;
        let bpp = info.bytes_per_pixel;
        for px in buf.chunks_exact_mut(bpp) {
            set_pixel_bytes(px, bpp, COLOR_BG.0, COLOR_BG.1, COLOR_BG.2, info);
        }

        let bar_w = info.width.saturating_mul(3) / 5;
        let bar_h = 14usize;
        let bar_x = info.width.saturating_sub(bar_w) / 2;
        let bar_y = info.height.saturating_mul(2) / 3;
        fill_rect_raw(buf, info, bar_x, bar_y, bar_w, bar_h, (34, 39, 48));
        label_pos = (bar_x, bar_y.saturating_add(bar_h + 6));

        let pct = progress_pct.min(100);
        let filled = (bar_w.saturating_mul(pct)) / 100;
        if filled > 0 {
            fill_rect_raw(buf, info, bar_x, bar_y, filled, bar_h, COLOR_ACCENT);
        }
    });

    if drawn {
        draw_text(label, label_pos.0, label_pos.1);
        mark_dirty_full();
        INVALIDATED.store(true, Ordering::SeqCst);
    }
    drawn
}

pub(crate) fn with_fb<F: FnOnce(&mut [u8], &FrameBufferInfo)>(f: F) -> bool {
    if let Some(fb) = FB.lock().as_mut() {
        f(fb.back_buffer, &fb.info);
        true
    } else {
        false
    }
}

/// Check if framebuffer is available (for callers to short-circuit work).
pub fn framebuffer_available() -> bool {
    FB.lock().is_some()
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

pub fn fill_rect(x: usize, y: usize, w: usize, h: usize, color: (u8, u8, u8)) {
    let _ = with_fb(|buf, info| {
        fill_rect_raw(buf, info, x, y, w, h, color);
    });
    mark_dirty_rect(x, y, w, h);
    INVALIDATED.store(true, Ordering::SeqCst);
}

fn fill_rect_raw(
    buf: &mut [u8],
    info: &FrameBufferInfo,
    x: usize,
    y: usize,
    w: usize,
    h: usize,
    color: (u8, u8, u8),
) {
    for yy in y..y.saturating_add(h) {
        if yy >= info.height {
            break;
        }
        for xx in x..x.saturating_add(w) {
            if xx >= info.width {
                break;
            }
            let bpp = info.bytes_per_pixel;
            let idx = yy * info.stride * bpp + xx * bpp;
            if let Some(px) = buf.get_mut(idx..idx + bpp) {
                set_pixel_bytes(px, bpp, color.0, color.1, color.2, info);
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
        PixelFormat::Rgb => {
            if bpp >= 3 {
                px[0] = r;
                px[1] = g;
                px[2] = b;
            }
        }
        PixelFormat::Bgr => {
            if bpp >= 3 {
                px[0] = b;
                px[1] = g;
                px[2] = r;
            }
        }
        PixelFormat::U8 => {
            if let Some(first) = px.first_mut() {
                *first = r;
            }
        }
        PixelFormat::Unknown { .. } => {
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

/// =============================
///  LOW-LEVEL FILL PRIMITIVES
/// =============================

/// Scalar pattern fill (memset-like but for multi-byte patterns).
fn scalar_pattern_fill(buf: &mut [u8], pattern: &[u8]) {
    if pattern.is_empty() {
        return;
    }
    let pat_len = pattern.len();
    let len = buf.len();
    let mut i = 0usize;
    while i < len {
        let remain = len - i;
        let to_copy = pat_len.min(remain);
        buf[i..i + to_copy].copy_from_slice(&pattern[..to_copy]);
        i += to_copy;
    }
}

/// Fast fill for 32-bit pattern (e.g. 32-bpp RGBA/BGRA).
///
/// This chooses the best available implementation at compile time:
/// - AVX2 on x86_64
/// - NEON on AArch64
/// - Scalar unrolled fallback otherwise
unsafe fn fast_fill_u32(ptr: *mut u8, len_bytes: usize, value: u32) {
    #[cfg(all(target_arch = "x86_64", target_feature = "avx2"))]
    {
        fast_fill_u32_avx2(ptr, len_bytes, value);
        return;
    }

    #[cfg(all(target_arch = "aarch64", target_feature = "neon"))]
    {
        fast_fill_u32_neon(ptr, len_bytes, value);
        return;
    }

    // Fallback: scalar unrolled
    fast_fill_u32_unrolled(ptr, len_bytes, value);
}

/// Fast fill for single-byte value (e.g. 8-bpp or packed channels).
unsafe fn fast_fill_u8(ptr: *mut u8, len_bytes: usize, value: u8) {
    fast_fill_u8_unrolled(ptr, len_bytes, value);
}

/// AVX2 implementation: fills memory with a repeated 32-bit value.
#[cfg(all(target_arch = "x86_64", target_feature = "avx2"))]
unsafe fn fast_fill_u32_avx2(ptr: *mut u8, len_bytes: usize, value: u32) {
    use core::arch::x86_64::*;

    let count = len_bytes / 4;
    let p32 = ptr as *mut u32;
    let v = _mm256_set1_epi32(value as i32);

    let chunks = count / 8; // 8 * u32 per 256-bit register
    let vptr = p32 as *mut __m256i;

    for i in 0..chunks {
        _mm256_storeu_si256(vptr.add(i), v);
    }

    let mut idx = chunks * 8;
    while idx < count {
        *p32.add(idx) = value;
        idx += 1;
    }
}

/// NEON implementation: fills memory with a repeated 32-bit value.
#[cfg(all(target_arch = "aarch64", target_feature = "neon"))]
unsafe fn fast_fill_u32_neon(ptr: *mut u8, len_bytes: usize, value: u32) {
    use core::arch::aarch64::*;

    let count = len_bytes / 4;
    let p32 = ptr as *mut u32;
    let v = vdupq_n_u32(value);

    let chunks = count / 4; // 4 * u32 per 128-bit register
    let vptr = p32 as *mut uint32x4_t;

    for i in 0..chunks {
        vst1q_u32(vptr.add(i), v);
    }

    let mut idx = chunks * 4;
    while idx < count {
        *p32.add(idx) = value;
        idx += 1;
    }
}

/// Scalar 32-bit fill with 32× + 8× unrolling.
unsafe fn fast_fill_u32_unrolled(ptr: *mut u8, len_bytes: usize, value: u32) {
    let count = len_bytes / 4;
    let p32 = ptr as *mut u32;

    let mut i = 0usize;

    // 32× unrolled
    while i + 32 <= count {
        *p32.add(i + 0) = value;
        *p32.add(i + 1) = value;
        *p32.add(i + 2) = value;
        *p32.add(i + 3) = value;
        *p32.add(i + 4) = value;
        *p32.add(i + 5) = value;
        *p32.add(i + 6) = value;
        *p32.add(i + 7) = value;
        *p32.add(i + 8) = value;
        *p32.add(i + 9) = value;
        *p32.add(i + 10) = value;
        *p32.add(i + 11) = value;
        *p32.add(i + 12) = value;
        *p32.add(i + 13) = value;
        *p32.add(i + 14) = value;
        *p32.add(i + 15) = value;
        *p32.add(i + 16) = value;
        *p32.add(i + 17) = value;
        *p32.add(i + 18) = value;
        *p32.add(i + 19) = value;
        *p32.add(i + 20) = value;
        *p32.add(i + 21) = value;
        *p32.add(i + 22) = value;
        *p32.add(i + 23) = value;
        *p32.add(i + 24) = value;
        *p32.add(i + 25) = value;
        *p32.add(i + 26) = value;
        *p32.add(i + 27) = value;
        *p32.add(i + 28) = value;
        *p32.add(i + 29) = value;
        *p32.add(i + 30) = value;
        *p32.add(i + 31) = value;
        i += 32;
    }

    // 8× unrolled
    while i + 8 <= count {
        *p32.add(i + 0) = value;
        *p32.add(i + 1) = value;
        *p32.add(i + 2) = value;
        *p32.add(i + 3) = value;
        *p32.add(i + 4) = value;
        *p32.add(i + 5) = value;
        *p32.add(i + 6) = value;
        *p32.add(i + 7) = value;
        i += 8;
    }

    // Tail
    while i < count {
        *p32.add(i) = value;
        i += 1;
    }
}

/// Scalar u8 fill with 32× + 8× unrolling.
unsafe fn fast_fill_u8_unrolled(ptr: *mut u8, len_bytes: usize, value: u8) {
    let p = ptr;
    let len = len_bytes;
    let mut i = 0usize;

    // 32× unrolled
    while i + 32 <= len {
        *p.add(i + 0) = value;
        *p.add(i + 1) = value;
        *p.add(i + 2) = value;
        *p.add(i + 3) = value;
        *p.add(i + 4) = value;
        *p.add(i + 5) = value;
        *p.add(i + 6) = value;
        *p.add(i + 7) = value;
        *p.add(i + 8) = value;
        *p.add(i + 9) = value;
        *p.add(i + 10) = value;
        *p.add(i + 11) = value;
        *p.add(i + 12) = value;
        *p.add(i + 13) = value;
        *p.add(i + 14) = value;
        *p.add(i + 15) = value;
        *p.add(i + 16) = value;
        *p.add(i + 17) = value;
        *p.add(i + 18) = value;
        *p.add(i + 19) = value;
        *p.add(i + 20) = value;
        *p.add(i + 21) = value;
        *p.add(i + 22) = value;
        *p.add(i + 23) = value;
        *p.add(i + 24) = value;
        *p.add(i + 25) = value;
        *p.add(i + 26) = value;
        *p.add(i + 27) = value;
        *p.add(i + 28) = value;
        *p.add(i + 29) = value;
        *p.add(i + 30) = value;
        *p.add(i + 31) = value;
        i += 32;
    }

    // 8× unrolled
    while i + 8 <= len {
        *p.add(i + 0) = value;
        *p.add(i + 1) = value;
        *p.add(i + 2) = value;
        *p.add(i + 3) = value;
        *p.add(i + 4) = value;
        *p.add(i + 5) = value;
        *p.add(i + 6) = value;
        *p.add(i + 7) = value;
        i += 8;
    }

    // Tail
    while i < len {
        *p.add(i) = value;
        i += 1;
    }
}
