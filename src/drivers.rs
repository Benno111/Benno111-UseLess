use crate::vga_buffer;
use crate::input::{self, InputEvent};
use core::sync::atomic::{AtomicUsize, Ordering};

pub fn init() {
    init_ps2();
    init_usb();
}

pub fn init_ps2() {
    vga_buffer::log_line("[drivers] PS/2 controller stub initialized");
    detect_ps2_devices();
}

pub fn init_usb() {
    vga_buffer::log_line("[drivers] USB controller stub initialized");
    probe_usb_ports();
}

fn detect_ps2_devices() {
    // Stub: in real code we'd send PS/2 commands to detect keyboard/mouse.
    vga_buffer::log_line("[drivers] PS/2: keyboard detected (stub)");
    vga_buffer::log_line("[drivers] PS/2: mouse detected (stub)");
    // Seed a couple of input events to show the device path works.
    input::enqueue_event(InputEvent::Key('k'));
    input::enqueue_event(InputEvent::MouseMove { dx: 3, dy: 2 });
}

fn probe_usb_ports() {
    // Stub: in real code we'd enumerate EHCI/XHCI/OHCI controllers and devices.
    vga_buffer::log_line("[drivers] USB: port 1 device present (stub)");
    vga_buffer::log_line("[drivers] USB: port 2 empty (stub)");
    input::enqueue_event(InputEvent::Key('u'));
}

// Simple round-robin demo interactions so devices "stay alive".
static POLL_TICK: AtomicUsize = AtomicUsize::new(0);

pub fn poll_devices() {
    let tick = POLL_TICK.fetch_add(1, Ordering::SeqCst);
    match tick % 4 {
        0 => input::enqueue_event(InputEvent::Key('k')),
        1 => input::enqueue_event(InputEvent::MouseMove { dx: 1, dy: 0 }),
        2 => input::enqueue_event(InputEvent::Key('u')),
        _ => input::enqueue_event(InputEvent::MouseMove { dx: -1, dy: 1 }),
    }
}
