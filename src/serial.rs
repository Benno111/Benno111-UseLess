//! Minimal serial (COM1) logger to mirror console output to QEMU without extra deps.

use core::fmt;
use spin::Mutex;
use core::sync::atomic::{AtomicUsize, Ordering};
use x86_64::instructions::port::Port;

struct SerialPort {
    data: Port<u8>,
    interrupt_enable: Port<u8>,
    fifo_control: Port<u8>,
    line_control: Port<u8>,
    modem_control: Port<u8>,
    line_status: Port<u8>,
    initialized: bool,
}

impl SerialPort {
    /// # Safety
    /// Caller must ensure the standard COM1 ports are available.
    const unsafe fn new() -> Self {
        Self {
            data: Port::new(0x3F8),
            interrupt_enable: Port::new(0x3F9),
            fifo_control: Port::new(0x3FA),
            line_control: Port::new(0x3FB),
            modem_control: Port::new(0x3FC),
            line_status: Port::new(0x3FD),
            initialized: false,
        }
    }

    unsafe fn init(&mut self) {
        if self.initialized {
            return;
        }
        // Disable interrupts
        self.interrupt_enable.write(0x00);
        // Enable DLAB
        self.line_control.write(0x80);
        // Set divisor to 3 (lo byte) 38400 baud
        self.data.write(0x03);
        //                  (hi byte)
        self.interrupt_enable.write(0x00);
        // 8 bits, no parity, one stop bit
        self.line_control.write(0x03);
        // Enable FIFO, clear them, with 14-byte threshold
        self.fifo_control.write(0xC7);
        // IRQs enabled, RTS/DSR set
        self.modem_control.write(0x0B);
        self.initialized = true;
    }

    unsafe fn write_byte(&mut self, byte: u8) {
        while self.line_status.read() & 0x20 == 0 {}
        self.data.write(byte);
    }

    unsafe fn write_bytes(&mut self, bytes: &[u8]) {
        for &b in bytes {
            self.write_byte(b);
        }
    }
}

// SAFETY: Access serialized through the mutex; ports are fixed hardware IO.
static SERIAL1: Mutex<SerialPort> = Mutex::new(unsafe { SerialPort::new() });
static SERIAL_CONTENDED: AtomicUsize = AtomicUsize::new(0);

pub fn init() {
    unsafe { SERIAL1.lock().init() };
}

pub fn log(args: fmt::Arguments) {
    if let Some(mut port) = SERIAL1.try_lock() {
        SERIAL_CONTENDED.store(0, Ordering::SeqCst);
        unsafe {
            port.init();
            let _ = fmt::write(&mut SerialWriter { port: &mut *port }, args);
            port.write_byte(b'\n');
        }
    } else {
        let skipped = SERIAL_CONTENDED.fetch_add(1, Ordering::SeqCst) + 1;
        if skipped <= 3 || skipped % 16 == 0 {
            // Drop silently; cannot log contention without the lock.
        }
    }
}

#[allow(dead_code)]
pub fn log_line(msg: &str) {
    log(format_args!("{msg}"));
}

struct SerialWriter<'a> {
    port: &'a mut SerialPort,
}

impl fmt::Write for SerialWriter<'_> {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        unsafe {
            self.port.write_bytes(s.as_bytes());
        }
        Ok(())
    }
}
