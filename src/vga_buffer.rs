//! Super small VGA text-mode writer for early kernel output.
//! This talks directly to the 0xb8000 text buffer in BIOS text mode.

#![allow(dead_code)]

use core::fmt;
use core::fmt::Write as _;
use core::sync::atomic::{AtomicUsize, Ordering};
use core::ptr::{read_volatile, write_volatile};
use bootloader_api::BootInfo;
use crate::serial;
use spin::{Lazy, Mutex};

const VGA_BUFFER_ADDRESS: usize = 0xb8000;
const BUFFER_WIDTH: usize = 80;
const BUFFER_HEIGHT: usize = 25;

/// Packed VGA character: ASCII byte + color byte.
#[repr(C)]
#[derive(Clone, Copy)]
struct VgaChar {
    ascii: u8,
    color: u8,
}

#[repr(transparent)]
struct VgaBuffer {
    chars: [[VgaChar; BUFFER_WIDTH]; BUFFER_HEIGHT],
}

/// Simple 4-bit foreground + 4-bit background color.
#[repr(u8)]
#[allow(dead_code)]
#[derive(Clone, Copy)]
pub enum Color {
    Black = 0x0,
    Blue = 0x1,
    Green = 0x2,
    Cyan = 0x3,
    Red = 0x4,
    Magenta = 0x5,
    Brown = 0x6,
    LightGray = 0x7,
    DarkGray = 0x8,
    LightBlue = 0x9,
    LightGreen = 0xA,
    LightCyan = 0xB,
    LightRed = 0xC,
    Pink = 0xD,
    Yellow = 0xE,
    White = 0xF,
}

#[inline]
fn make_color(fg: Color, bg: Color) -> u8 {
    (bg as u8) << 4 | (fg as u8)
}

pub struct VgaWriter {
    col: usize,
    row: usize,
    color: u8,
    buffer: *mut VgaBuffer,
}

// Safe because the underlying memory is a fixed hardware buffer and access is guarded by a mutex.
unsafe impl Send for VgaWriter {}
unsafe impl Sync for VgaWriter {}

impl VgaWriter {
    fn new() -> Self {
        Self {
            col: 0,
            row: 0,
            color: make_color(Color::LightGray, Color::Black),
            buffer: current_buffer_ptr(),
        }
    }

    fn buffer(&mut self) -> &mut VgaBuffer {
        // SAFETY: Pointer is initialized during boot via `init`, mapped by bootloader.
        unsafe { &mut *self.buffer }
    }

    fn read_char(&self, row: usize, col: usize) -> VgaChar {
        // SAFETY: The VGA memory is always valid for the size of a single character.
        unsafe { read_volatile(&(*self.buffer).chars[row][col]) }
    }

    fn write_char(&mut self, row: usize, col: usize, value: VgaChar) {
        // SAFETY: The VGA memory is always valid for the size of a single character.
        unsafe { write_volatile(&mut (*self.buffer).chars[row][col], value) };
    }

    fn newline(&mut self) {
        self.col = 0;
        if self.row + 1 >= BUFFER_HEIGHT {
            // Scroll everything one line up.
            for y in 1..BUFFER_HEIGHT {
                for x in 0..BUFFER_WIDTH {
                    let ch = self.read_char(y, x);
                    self.write_char(y - 1, x, ch);
                }
            }
            // Clear last line.
            self.clear_row(BUFFER_HEIGHT - 1);
        } else {
            self.row += 1;
        }
    }

    fn clear_row(&mut self, row: usize) {
        let blank = VgaChar {
            ascii: b' ',
            color: self.color,
        };
        for x in 0..BUFFER_WIDTH {
            self.write_char(row, x, blank);
        }
    }

    fn write_byte(&mut self, byte: u8) {
        match byte {
            b'\n' => self.newline(),
            byte => {
                if self.col >= BUFFER_WIDTH {
                    self.newline();
                }
                let vchar = VgaChar {
                    ascii: byte,
                    color: self.color,
                };
                self.write_char(self.row, self.col, vchar);
                self.col += 1;
            }
        }
    }

    fn write_str(&mut self, s: &str) {
        for b in s.bytes() {
            self.write_byte(b);
        }
    }

    pub fn clear(&mut self) {
        for row in 0..BUFFER_HEIGHT {
            self.clear_row(row);
        }
        self.col = 0;
        self.row = 0;
    }

    pub fn write_at(&mut self, x: usize, y: usize, s: &str) {
        if y >= BUFFER_HEIGHT {
            return;
        }
        self.row = y;
        self.col = x.min(BUFFER_WIDTH.saturating_sub(1));
        for b in s.bytes() {
            self.write_byte(b);
        }
    }
}

impl fmt::Write for VgaWriter {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        self.write_str(s);
        Ok(())
    }
}

static GLOBAL_WRITER: Lazy<Mutex<VgaWriter>> = Lazy::new(|| Mutex::new(VgaWriter::new()));

// Virtual pointer to the VGA text buffer. Updated during init when we learn the physical
// memory offset from the bootloader.
static VGA_PTR: AtomicUsize = AtomicUsize::new(VGA_BUFFER_ADDRESS);

fn current_buffer_ptr() -> *mut VgaBuffer {
    VGA_PTR.load(Ordering::SeqCst) as *mut VgaBuffer
}

/// Initialize the VGA writer with the bootloader-provided physical memory mapping offset.
/// Must be called before using `writer()` to ensure the VGA memory is mapped.
pub fn init(boot_info: &BootInfo) {
    if let Some(offset) = boot_info.physical_memory_offset.into_option() {
        VGA_PTR.store((offset as usize) + VGA_BUFFER_ADDRESS, Ordering::SeqCst);
    }
}

/// Get a locked writer handle for printing to the VGA text buffer.
pub fn writer() -> impl core::ops::DerefMut<Target = VgaWriter> {
    GLOBAL_WRITER.lock()
}

/// Write a formatted line to the VGA console.
pub fn log(args: fmt::Arguments) {
    let mut w = writer();
    let _ = w.write_fmt(args);
    let _ = w.write_str("\n");
    serial::log(args);
}

/// Convenience helper to log a plain string.
pub fn log_line(msg: &str) {
    log(format_args!("{msg}"));
}

/// Print a raw string (no automatic newline) to VGA and mirror to serial.
pub fn print(msg: &str) {
    let mut w = writer();
    let _ = w.write_str(msg);
    serial::log(format_args!("{msg}"));
}
