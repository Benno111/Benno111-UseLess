
use bootloader_api::info::{FrameBuffer, FrameBufferInfo};
use spin::Mutex;

use crate::vga_buffer;
use crate::fonts::{self, FontConfig, FONT_W};

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

pub fn init(fb: Option<&mut FrameBuffer>) {
    if let Some(fb) = fb {
        let info = fb.info();
        // SAFETY: bootloader-provided framebuffer lives for the whole kernel lifetime.
        let buffer: &'static mut [u8] = unsafe { core::mem::transmute(fb.buffer_mut()) };
        *FB.lock() = Some(FbState { buffer, info });
        clear_screen();
        draw_text("Desktop Initialized", 2, 2);
        vga_buffer::log_line("[FB] Framebuffer initialized.");
    } else {
        vga_buffer::log_line("[FB] No framebuffer provided; rendering disabled");
    }
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
    vga_buffer::log_line("[FB] Draw text");
}

pub fn render_frame() {
    vga_buffer::log_line("[FB] Frame rendered.");
}

pub fn set_font_config(cfg: FontConfig) {
    *FONT_CFG.lock() = cfg;
}

fn with_fb<F: FnOnce(&mut [u8], &FrameBufferInfo)>(f: F) -> bool {
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
