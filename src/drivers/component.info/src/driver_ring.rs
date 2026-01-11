extern crate alloc;

use alloc::{format, vec::Vec};
use crate::vga_buffer;
use crate::input::{InputEvent, MouseButton};
use crate::usb;
use crate::usb::{hid, mouse};
use crate::usb::hid::KeyEventKind;
use core::sync::atomic::{AtomicBool, AtomicU8, AtomicUsize, Ordering};
use spin::Mutex;
use x86_64::instructions::port::Port;

#[derive(Clone, Copy)]
pub struct DriverStats {
    pub poll_skips: usize,
    pub poll_budget: usize,
}

pub enum DriverRequest {
    Init,
    PollInput,
    QueryStats,
}

pub enum DriverResponse {
    Ack,
    Input(DriverInputFrame),
    Stats(DriverStats),
}

pub fn handle_request(req: DriverRequest) -> DriverResponse {
    match req {
        DriverRequest::Init => {
            init();
            vga_buffer::log_line("[driver-ring] online");
            DriverResponse::Ack
        }
        DriverRequest::PollInput => DriverResponse::Input(poll_devices()),
        DriverRequest::QueryStats => DriverResponse::Stats(DriverStats {
            poll_skips: poll_skip_count(),
            poll_budget: current_poll_budget(),
        }),
    }
}

fn init() {
    init_ps2();
    init_usb();
}

pub fn init_ps2() {
    vga_buffer::log_line("[drivers] PS/2 controller stub initialized");
    detect_ps2_devices();
}

pub fn init_usb() {
    // Wire up the real USB stack (xHCI discovery). Stubbed PCI scan logs when absent.
    usb::init();
    refresh_usb_hid_devices();
    probe_usb_ports();
}

fn detect_ps2_devices() {
    // Stub: in real code we'd send PS/2 commands to detect keyboard/mouse.
    vga_buffer::log_line("[drivers] PS/2: keyboard detected (stub, i8042)");
    vga_buffer::log_line("[drivers] PS/2: mouse detected (stub, 3-byte standard packet)");
    init_ps2_mouse();
}

fn probe_usb_ports() {
    // Stub: in real code we'd enumerate EHCI/XHCI/OHCI controllers and devices.
    vga_buffer::log_line("[drivers] USB: port 1 device present (stub, class HID unknown)");
    vga_buffer::log_line("[drivers] USB: port 2 empty (stub)");
}

fn refresh_usb_hid_devices() {
    let devices = usb::enumerate_ports();
    let mice = mouse::init_mice_from_devices(&devices);
    let keyboards = hid::init_keyboards_from_devices(&devices);

    let mut state = USB_HID_STATE.lock();
    state.mice = mice;
    state.keyboards = keyboards;

    vga_buffer::log_line(&format!(
        "[drivers] USB HID: {} mice, {} keyboards ready",
        state.mice.len(),
        state.keyboards.len()
    ));
}

pub struct DriverInputFrame {
    pub events: Vec<InputEvent>,
}

impl DriverInputFrame {
    pub fn new() -> Self {
        Self { events: Vec::new() }
    }
}

struct MousePacket {
    buf: [u8; 3],
    idx: usize,
}

static MOUSE_PACKET: Mutex<MousePacket> = Mutex::new(MousePacket { buf: [0; 3], idx: 0 });
struct UsbHidState {
    mice: Vec<mouse::UsbMouse>,
    keyboards: Vec<hid::UsbKeyboard>,
}

static USB_HID_STATE: Mutex<UsbHidState> = Mutex::new(UsbHidState {
    mice: Vec::new(),
    keyboards: Vec::new(),
});
static PS2_MOUSE_ENABLED: AtomicBool = AtomicBool::new(true);
static PS2_KEYBOARD_ENABLED: AtomicBool = AtomicBool::new(true);
static PS2_BUDGET_BYTES: AtomicUsize = AtomicUsize::new(8);
static PS2_MOUSE_BUTTONS: AtomicU8 = AtomicU8::new(0);
static POLL_IN_PROGRESS: AtomicBool = AtomicBool::new(false);
static POLL_SKIPPED: AtomicUsize = AtomicUsize::new(0);

fn wait_input_empty() {
    unsafe {
        let mut status_port: Port<u8> = Port::new(0x64);
        for _ in 0..1000 {
            if status_port.read() & 0x02 == 0 {
                break;
            }
            x86_64::instructions::nop();
        }
    }
}

fn wait_output_full() -> Option<u8> {
    unsafe {
        let mut status_port: Port<u8> = Port::new(0x64);
        let mut data_port: Port<u8> = Port::new(0x60);
        for _ in 0..1000 {
            if status_port.read() & 0x01 != 0 {
                return Some(data_port.read());
            }
            x86_64::instructions::nop();
        }
    }
    None
}

fn ps2_write_command(cmd: u8) {
    unsafe {
        let mut cmd_port: Port<u8> = Port::new(0x64);
        wait_input_empty();
        cmd_port.write(cmd);
    }
}

fn ps2_write_mouse(data: u8) {
    unsafe {
        let mut cmd_port: Port<u8> = Port::new(0x64);
        let mut data_port: Port<u8> = Port::new(0x60);
        wait_input_empty();
        cmd_port.write(0xD4); // write next byte to mouse
        wait_input_empty();
        data_port.write(data);
    }
}

/// Drain any pending bytes from the PS/2 data port to start with a clean buffer.
fn ps2_flush_output() {
    unsafe {
        let mut status_port: Port<u8> = Port::new(0x64);
        let mut data_port: Port<u8> = Port::new(0x60);
        for _ in 0..8 {
            if status_port.read() & 0x01 == 0 {
                break;
            }
            let _ = data_port.read();
        }
    }
}

/// Send a mouse command and confirm the 0xFA ACK.
fn ps2_mouse_command(cmd: u8) -> bool {
    ps2_write_mouse(cmd);
    matches!(wait_output_full(), Some(0xFA))
}

fn init_ps2_mouse() {
    ps2_flush_output();

    // Enable auxiliary device.
    ps2_write_command(0xA8);

    // Reset to defaults then enable streaming.
    let ok_defaults = ps2_mouse_command(0xF6); // Set Defaults
    let ok_stream = ps2_mouse_command(0xF4);   // Enable Data Reporting

    if ok_defaults && ok_stream {
        PS2_MOUSE_ENABLED.store(true, Ordering::Relaxed);
        vga_buffer::log_line("[drivers] PS/2 mouse streaming enabled");
    } else {
        PS2_MOUSE_ENABLED.store(false, Ordering::Relaxed);
        vga_buffer::log_line("[drivers] PS/2 mouse init failed (no ACK)");
    }
}

#[allow(dead_code)]
pub fn set_ps2_mouse_enabled(enabled: bool) {
    PS2_MOUSE_ENABLED.store(enabled, Ordering::Relaxed);
    vga_buffer::log_line(if enabled {
        "[drivers] PS/2 mouse enabled"
    } else {
        "[drivers] PS/2 mouse disabled"
    });
}

#[allow(dead_code)]
pub fn set_ps2_keyboard_enabled(enabled: bool) {
    PS2_KEYBOARD_ENABLED.store(enabled, Ordering::Relaxed);
    vga_buffer::log_line(if enabled {
        "[drivers] PS/2 keyboard enabled"
    } else {
        "[drivers] PS/2 keyboard disabled"
    });
}

#[allow(dead_code)]
pub fn set_ps2_poll_budget(bytes_per_tick: usize) {
    let clamped = bytes_per_tick.clamp(1, 64);
    PS2_BUDGET_BYTES.store(clamped, Ordering::Relaxed);
    vga_buffer::log_line(&alloc::format!(
        "[drivers] PS/2 poll budget set to {} bytes/tick",
        clamped
    ));
}

fn poll_ps2_ports(frame: &mut DriverInputFrame) {
    unsafe {
        let mut status_port: Port<u8> = Port::new(0x64);
        let mut data_port: Port<u8> = Port::new(0x60);
        let mut extended = false;

        let budget = PS2_BUDGET_BYTES.load(Ordering::Relaxed).clamp(1, 64);
        let mouse_on = PS2_MOUSE_ENABLED.load(Ordering::Relaxed);
        let kbd_on = PS2_KEYBOARD_ENABLED.load(Ordering::Relaxed);

        // Read up to a handful of bytes per tick to avoid starving.
        for _ in 0..budget {
            let status = status_port.read();
            if status & 0x01 == 0 {
                break;
            }
            let byte = data_port.read();
            if status & 0x20 != 0 {
                if !mouse_on {
                    continue;
                }
                // Mouse byte
                let mut pkt = MOUSE_PACKET.lock();
                // First byte must have sync bit set (bit 3).
                if pkt.idx == 0 && byte & 0x08 == 0 {
                    continue;
                }
                let idx = pkt.idx;
                pkt.buf[idx] = byte;
                pkt.idx = idx + 1;
                if pkt.idx == 3 {
                    let b0 = pkt.buf[0];
                    // Ignore overflowed packets.
                    if b0 & 0xC0 == 0 {
                        let dx = pkt.buf[1] as i8 as isize;
                        // PS/2 reports positive Y as up; screen coords grow down, so invert.
                        let dy = -(pkt.buf[2] as i8 as isize);
                        if dx != 0 || dy != 0 {
                            frame.events.push(InputEvent::MouseMove { dx, dy });
                        }
                        let buttons = b0 & 0x07;
                        let prev = PS2_MOUSE_BUTTONS.load(Ordering::Relaxed);
                        let changed = prev ^ buttons;
                        if changed != 0 {
                            PS2_MOUSE_BUTTONS.store(buttons, Ordering::Relaxed);
                            if changed & 0x01 != 0 {
                                frame.events.push(InputEvent::MouseButton {
                                    button: MouseButton::Left,
                                    pressed: buttons & 0x01 != 0,
                                });
                            }
                            if changed & 0x02 != 0 {
                                frame.events.push(InputEvent::MouseButton {
                                    button: MouseButton::Right,
                                    pressed: buttons & 0x02 != 0,
                                });
                            }
                            if changed & 0x04 != 0 {
                                frame.events.push(InputEvent::MouseButton {
                                    button: MouseButton::Middle,
                                    pressed: buttons & 0x04 != 0,
                                });
                            }
                        }
                    }
                    pkt.idx = 0;
                }
            } else {
                // Keyboard scancode set 1 (very partial map). Handle E0 prefix.
                if byte == 0xE0 {
                    extended = true;
                    continue;
                }
                if kbd_on && byte & 0x80 == 0 {
                    let sc = if extended { 0xE0_00 | byte as u16 } else { byte as u16 };
                    extended = false;
                    if let Some(ev) = match sc {
                        0xE0_48 => Some(InputEvent::MouseMove { dx: 0, dy: -5 }), // Up
                        0xE0_50 => Some(InputEvent::MouseMove { dx: 0, dy: 5 }),  // Down
                        0xE0_4B => Some(InputEvent::MouseMove { dx: -5, dy: 0 }), // Left
                        0xE0_4D => Some(InputEvent::MouseMove { dx: 5, dy: 0 }),  // Right
                        _ => scancode_to_input_events(byte),
                    } {
                        frame.events.push(ev);
                    }
                } else {
                    extended = false;
                }
            }
        }
    }
}

fn scancode_to_char(code: u8) -> Option<char> {
    match code {
        0x02 => Some('1'),
        0x03 => Some('2'),
        0x04 => Some('3'),
        0x05 => Some('4'),
        0x06 => Some('5'),
        0x07 => Some('6'),
        0x08 => Some('7'),
        0x09 => Some('8'),
        0x0A => Some('9'),
        0x0B => Some('0'),
        0x10 => Some('q'),
        0x11 => Some('w'),
        0x12 => Some('e'),
        0x13 => Some('r'),
        0x14 => Some('t'),
        0x15 => Some('y'),
        0x16 => Some('u'),
        0x17 => Some('i'),
        0x18 => Some('o'),
        0x19 => Some('p'),
        0x1E => Some('a'),
        0x1F => Some('s'),
        0x20 => Some('d'),
        0x21 => Some('f'),
        0x22 => Some('g'),
        0x23 => Some('h'),
        0x24 => Some('j'),
        0x25 => Some('k'),
        0x26 => Some('l'),
        0x2C => Some('z'),
        0x2D => Some('x'),
        0x2E => Some('c'),
        0x2F => Some('v'),
        0x30 => Some('b'),
        0x31 => Some('n'),
        0x32 => Some('m'),
        0x39 => Some(' '),
        _ => None,
    }
}

fn scancode_to_input_events(code: u8) -> Option<InputEvent> {
    match code {
        // Arrow keys (extended 0xE0 prefix handled in poll)
        0x48 => Some(InputEvent::MouseMove { dx: 0, dy: -5 }), // Up
        0x50 => Some(InputEvent::MouseMove { dx: 0, dy: 5 }),  // Down
        0x4B => Some(InputEvent::MouseMove { dx: -5, dy: 0 }), // Left
        0x4D => Some(InputEvent::MouseMove { dx: 5, dy: 0 }),  // Right
        // Enter => left click (handled in input handler)
        0x1C => Some(InputEvent::Key('\n')),
        // Backspace => right click (handled in input handler)
        0x0E => Some(InputEvent::Key('\u{8}')),
        _ => scancode_to_char(code).map(InputEvent::Key),
    }
}

fn poll_devices() -> DriverInputFrame {
    struct PollGuard;
    impl Drop for PollGuard {
        fn drop(&mut self) {
            POLL_IN_PROGRESS.store(false, Ordering::SeqCst);
        }
    }

    if POLL_IN_PROGRESS.swap(true, Ordering::SeqCst) {
        let skips = POLL_SKIPPED.fetch_add(1, Ordering::SeqCst) + 1;
        if skips % 4 == 0 {
            vga_buffer::log_line("[drivers] poll skipped; previous poll still running");
        }
        return DriverInputFrame::new();
    }
    let _g = PollGuard;
    let mut frame = DriverInputFrame::new();
    poll_ps2_ports(&mut frame);
    poll_usb_devices(&mut frame);
    POLL_SKIPPED.store(0, Ordering::SeqCst);
    frame
}

fn poll_skip_count() -> usize {
    POLL_SKIPPED.load(Ordering::SeqCst)
}

fn current_poll_budget() -> usize {
    PS2_BUDGET_BYTES.load(Ordering::Relaxed)
}

fn poll_usb_devices(frame: &mut DriverInputFrame) {
    let mut state = USB_HID_STATE.lock();

    for evt in mouse::poll_mice(&mut state.mice) {
        let dx = evt.dx as isize;
        let dy = -(evt.dy as isize);
        if dx != 0 || dy != 0 {
            frame.events.push(InputEvent::MouseMove { dx, dy });
        }

        if evt.wheel != 0 {
            frame.events.push(InputEvent::MouseWheel {
                delta: evt.wheel as i16,
            });
        }

        // Button bits: 0 = left, 1 = right, 2 = middle.
        if evt.pressed & 0x01 != 0 {
            frame.events.push(InputEvent::MouseButton {
                button: MouseButton::Left,
                pressed: true,
            });
        }
        if evt.released & 0x01 != 0 {
            frame.events.push(InputEvent::MouseButton {
                button: MouseButton::Left,
                pressed: false,
            });
        }
        if evt.pressed & 0x02 != 0 {
            frame.events.push(InputEvent::MouseButton {
                button: MouseButton::Right,
                pressed: true,
            });
        }
        if evt.released & 0x02 != 0 {
            frame.events.push(InputEvent::MouseButton {
                button: MouseButton::Right,
                pressed: false,
            });
        }
    }

    for evt in hid::poll_keyboards(&mut state.keyboards) {
        if evt.kind != KeyEventKind::Press {
            continue;
        }
        if let Some(input) = map_keyboard_event(&evt) {
            frame.events.push(input);
        }
    }
}

fn map_keyboard_event(evt: &hid::KeyEvent) -> Option<InputEvent> {
    if let Some(ch) = evt.ch {
        return Some(InputEvent::Key(ch));
    }

    match evt.keycode {
        // Arrow keys (HID usage IDs) mapped to mouse nudges to keep desktop controllable.
        0x52 => Some(InputEvent::MouseMove { dx: 0, dy: -5 }), // Up
        0x51 => Some(InputEvent::MouseMove { dx: 0, dy: 5 }),  // Down
        0x50 => Some(InputEvent::MouseMove { dx: -5, dy: 0 }), // Left
        0x4F => Some(InputEvent::MouseMove { dx: 5, dy: 0 }),  // Right
        _ => None,
    }
}
