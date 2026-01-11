#![allow(dead_code)]

use alloc::{boxed::Box, vec};
use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use core::str;

use bootloader_api::info::{FrameBuffer, FrameBufferInfo, PixelFormat};
use spin::Mutex;

use crate::vga_buffer;
use crate::fonts::{self, FontConfig, FONT_W};

/* ============================================================
   CONFIG
============================================================ */

pub const COLOR_BG: (u8, u8, u8) = (16, 18, 24);
pub const COLOR_CARD: (u8, u8, u8) = (30, 33, 40);
pub const COLOR_ACCENT: (u8, u8, u8) = (82, 156, 255);

const LOG_FB: bool = false;
const FONT_H_EST: usize = 8;

/* ============================================================
   FRAMEBUFFER STATE
============================================================ */

struct FbState {
    /// VRAM
    front_buffer: &'static mut [u8],
    /// Backbuffer
    back_buffer: &'static mut [u8],
    info: FrameBufferInfo,
}

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
        Self { x1: 0, y1: 0, x2: 0, y2: 0, valid: false }
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

static TEXT_OPACITY: AtomicUsize = AtomicUsize::new(255);
static INVALIDATED: AtomicBool = AtomicBool::new(true);
static REFRESH_LOCK: AtomicBool = AtomicBool::new(false);
static FORCE_SWAP: AtomicBool = AtomicBool::new(false);
static DESKTOP_MODE: AtomicBool = AtomicBool::new(false);

static FRAME_COUNT: AtomicUsize = AtomicUsize::new(0);

/* ============================================================
   RENDER CALLBACK (WINDOW MANAGER)
============================================================ */

static RENDERER: Mutex<Option<fn()>> = Mutex::new(None);

pub fn set_renderer(renderer: fn()) {
    *RENDERER.lock() = Some(renderer);
}

pub fn render_registered() {
    if let Some(renderer) = *RENDERER.lock() {
        renderer();
    }
}

pub fn set_text_opacity(opacity: u8) {
    TEXT_OPACITY.store(opacity as usize, Ordering::SeqCst);
}

pub fn set_font_config(cfg: FontConfig) {
    *FONT_CFG.lock() = cfg;
}

/* ============================================================
   INIT
============================================================ */

pub fn init(fb: Option<&mut FrameBuffer>) {
    if let Some(fb) = fb {
        let info = fb.info();
        let front: &'static mut [u8] = unsafe { core::mem::transmute(fb.buffer_mut()) };

        let back_box: Box<[u8]> = vec![0u8; front.len()].into_boxed_slice();
        let back: &'static mut [u8] = Box::leak(back_box);

        *FB.lock() = Some(FbState {
            front_buffer: front,
            back_buffer: back,
            info,
        });

        fill_screen(COLOR_BG);
        draw_text("Desktop Initialized", 2, 2);

        render_frame();
        vga_buffer::log_line("[FB] Framebuffer initialized");
    } else {
        vga_buffer::log_line("[FB] No framebuffer");
    }

    INVALIDATED.store(true, Ordering::SeqCst);
}

/* ============================================================
   PRESENT PIPELINE
============================================================ */

pub fn render_frame() {
    let dirty = INVALIDATED.swap(false, Ordering::SeqCst);
    let force = FORCE_SWAP.load(Ordering::Relaxed);

    if !dirty && !force {
        return;
    }

    if REFRESH_LOCK.load(Ordering::SeqCst) {
        INVALIDATED.store(true, Ordering::SeqCst);
        return;
    }

    let mut fb_guard = FB.lock();
    let Some(fb) = fb_guard.as_mut() else { return };

    let mut dr = DIRTY.lock();
    if !dr.valid {
        dr.x1 = 0;
        dr.y1 = 0;
        dr.x2 = fb.info.width;
        dr.y2 = fb.info.height;
        dr.valid = true;
    }

    let bpp = fb.info.bytes_per_pixel;
    let stride = fb.info.stride * bpp;

    for y in dr.y1..dr.y2 {
        let row = y * stride;
        let start = row + dr.x1 * bpp;
        let end = row + dr.x2 * bpp;
        if end <= fb.front_buffer.len() {
            fb.front_buffer[start..end].copy_from_slice(&fb.back_buffer[start..end]);
        }
    }

    dr.valid = false;
    FRAME_COUNT.fetch_add(1, Ordering::Relaxed);

    if DESKTOP_MODE.load(Ordering::Relaxed) {
        if LOG_FB {
            vga_buffer::log_line("[FB] swap");
        }
    }
}

/* ============================================================
   CONTROL
============================================================ */

pub fn invalidate() {
    INVALIDATED.store(true, Ordering::SeqCst);
}

pub fn set_force_swap(enabled: bool) {
    FORCE_SWAP.store(enabled, Ordering::SeqCst);
}

pub fn set_desktop_mode(enabled: bool) {
    DESKTOP_MODE.store(enabled, Ordering::SeqCst);
}

pub fn lock_refresh() {
    REFRESH_LOCK.store(true, Ordering::SeqCst);
}

pub fn unlock_refresh() {
    REFRESH_LOCK.store(false, Ordering::SeqCst);
}

pub fn framebuffer_available() -> bool {
    FB.lock().is_some()
}

pub fn framebuffer_size() -> Option<(usize, usize)> {
    FB.lock().as_ref().map(|fb| (fb.info.width, fb.info.height))
}

/* ============================================================
   DIRTY RECT SYSTEM
============================================================ */

fn mark_dirty_rect(x: usize, y: usize, w: usize, h: usize) {
    let mut dr = DIRTY.lock();
    if !dr.valid {
        dr.x1 = x;
        dr.y1 = y;
        dr.x2 = x + w;
        dr.y2 = y + h;
        dr.valid = true;
    } else {
        dr.x1 = dr.x1.min(x);
        dr.y1 = dr.y1.min(y);
        dr.x2 = dr.x2.max(x + w);
        dr.y2 = dr.y2.max(y + h);
    }
}

fn mark_dirty_full() {
    if let Some((w, h)) = framebuffer_size() {
        mark_dirty_rect(0, 0, w, h);
    }
}

/* ============================================================
   DRAWING API
============================================================ */

pub fn fill_screen(color: (u8, u8, u8)) {
    let _ = with_fb(|buf, info| {
        let stride = info.stride * info.bytes_per_pixel;
        for y in 0..info.height {
            let row = y * stride;
            for x in 0..info.width {
                let idx = row + x * info.bytes_per_pixel;
                set_pixel(&mut buf[idx..], info, color);
            }
        }
    });
    mark_dirty_full();
    invalidate();
}

pub fn draw_rect(x: usize, y: usize, w: usize, h: usize, color: (u8, u8, u8)) {
    let _ = with_fb(|buf, info| {
        let stride = info.stride * info.bytes_per_pixel;
        for yy in y..(y + h) {
            if yy >= info.height { break; }
            for xx in x..(x + w) {
                if xx >= info.width { break; }
                let idx = yy * stride + xx * info.bytes_per_pixel;
                set_pixel(&mut buf[idx..], info, color);
            }
        }
    });
    mark_dirty_rect(x, y, w, h);
    invalidate();
}

pub fn draw_text(text: &str, x: usize, y: usize) {
    let _ = with_fb(|buf, info| {
        let cfg = *FONT_CFG.lock();
        let scale = cfg.scale as usize;
        let spacing = cfg.letter_spacing as usize;
        let step = (FONT_W as usize * scale) + spacing;

        for (i, ch) in text.bytes().enumerate() {
            draw_glyph(buf, info, x + i * step, y, ch, &cfg);
        }
    });

    let cfg = *FONT_CFG.lock();
    let scale = cfg.scale as usize;
    let width = text.len() * (FONT_W as usize * scale);
    let height = FONT_H_EST * scale;
    mark_dirty_rect(x, y, width, height);
    invalidate();
}

pub fn draw_text_no_invalidate(text: &str, x: usize, y: usize) {
    let _ = with_fb(|buf, info| {
        let cfg = *FONT_CFG.lock();
        let scale = cfg.scale as usize;
        let spacing = cfg.letter_spacing as usize;
        let step = (FONT_W as usize * scale) + spacing;
        for (i, ch) in text.bytes().enumerate() {
            draw_glyph(buf, info, x + i * step, y, ch, &cfg);
        }
    });
    let cfg = *FONT_CFG.lock();
    let scale = cfg.scale as usize;
    let width = text.len() * (FONT_W as usize * scale);
    let height = FONT_H_EST * scale;
    mark_dirty_rect(x, y, width, height);
}

pub fn fill_rect(x: usize, y: usize, w: usize, h: usize, color: (u8, u8, u8)) {
    draw_rect(x, y, w, h, color);
}

/* ============================================================
   LOW LEVEL
============================================================ */

fn set_pixel(px: &mut [u8], info: &FrameBufferInfo, color: (u8,u8,u8)) {
    match info.pixel_format {
        PixelFormat::Rgb => {
            px[0] = color.0;
            px[1] = color.1;
            px[2] = color.2;
        }
        PixelFormat::Bgr => {
            px[0] = color.2;
            px[1] = color.1;
            px[2] = color.0;
        }
        PixelFormat::U8 => {
            px[0] = color.0;
        }
        _ => px[0] = color.0,
    }
}

fn draw_glyph(buf: &mut [u8], info: &FrameBufferInfo, x: usize, y: usize, ch: u8, cfg: &FontConfig) {
    let rows = fonts::glyph_rows(ch);
    for (row, bits) in rows.iter().enumerate() {
        for col in 0..5 {
            if bits & (1 << (4 - col)) != 0 {
                let scale = cfg.scale as usize;
                let px = x + col * scale;
                let py = y + row * scale;
                draw_rect(px, py, scale, scale, cfg.fg);
            }
        }
    }
}

pub fn draw_desktop() {
    fill_screen(COLOR_BG);
}

pub fn draw_boot_splash(pct: usize, label: &str) -> Result<(), ()> {
    fill_screen((12, 14, 18));
    draw_text(label, 12, 12);
    let bar_w = 200usize;
    let filled = (bar_w * pct.min(100)) / 100;
    draw_rect(12, 28, bar_w, 6, (40, 44, 52));
    draw_rect(12, 28, filled, 6, COLOR_ACCENT);
    Ok(())
}

pub(crate) fn with_fb<F: FnOnce(&mut [u8], &FrameBufferInfo)>(f: F) -> bool {
    if let Some(fb) = FB.lock().as_mut() {
        f(fb.back_buffer, &fb.info);
        true
    } else {
        false
    }
}
