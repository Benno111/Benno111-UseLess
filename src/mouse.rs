use alloc::vec::Vec;

use crate::usb::device::{UsbDevice, UsbEndpoint, EndpointType};
use crate::vga_buffer;
use crate::usb::xhci;

/// A single USB mouse attached via xHCI (boot protocol).
pub struct UsbMouse {
    pub dev: UsbDevice,
    pub iface_index: usize,
    pub ep_index: usize,
    pub ep_address: u8,
    last_buttons: u8,
}

/// Mouse buttons, deltas, and wheel.
///
/// Deltas are in device units (boot protocol: signed 8-bit, we promote to i16).
#[derive(Clone, Copy, Debug)]
pub struct MouseEvent {
    pub dx: i16,
    pub dy: i16,
    pub wheel: i8,
    pub buttons: u8,       // current bitfield
    pub pressed: u8,       // bits that transitioned 0 -> 1 this frame
    pub released: u8,      // bits that transitioned 1 -> 0 this frame
}

/// Initialize mice from enumerated USB devices by scanning
/// configuration/interface/endpoint descriptors for HID mouse interfaces.
pub fn init_mice_from_devices(devs: &[UsbDevice]) -> Vec<UsbMouse> {
    let mut out = Vec::new();

    for dev in devs {
        let ms = find_mouse_endpoints(dev);
        for (iface_idx, ep_idx, ep) in ms {
            vga_buffer::log_line(&alloc::format!(
                "[USB-HID] Mouse on port {} slot {} if={} ep=0x{:02x}",
                dev.port,
                dev.slot_id,
                iface_idx,
                ep.address
            ));

            out.push(UsbMouse {
                dev: dev.clone(),
                iface_index: iface_idx,
                ep_index: ep_idx,
                ep_address: ep.address,
                last_buttons: 0,
            });
        }
    }

    if out.is_empty() {
        vga_buffer::log_line("[USB-HID] No mice found via HID descriptors.");
    }

    out
}

fn find_mouse_endpoints(dev: &UsbDevice) -> Vec<(usize, usize, UsbEndpoint)> {
    let mut out = Vec::new();

    if dev.configurations.is_empty() {
        return out;
    }
    let cfg = &dev.configurations[0];

    for (iface_idx, iface) in cfg.interfaces.iter().enumerate() {
        // HID mouse: class=0x03, protocol=0x02
        if iface.interface_class == 0x03 && iface.interface_protocol == 0x02 {
            for (ep_idx, ep) in iface.endpoints.iter().enumerate() {
                if let EndpointType::Interrupt = ep.ep_type {
                    if (ep.address & 0x80) != 0 {
                        out.push((iface_idx, ep_idx, *ep));
                    }
                }
            }
        }
    }

    out
}

/// Poll all mice once and return a list of MouseEvents.
pub fn poll_mice(mice: &mut [UsbMouse]) -> Vec<MouseEvent> {
    let mut events = Vec::new();

    for m in mice.iter_mut() {
        let mut buf = [0u8; 8];

        let ep_id = m.ep_address & 0x0F;

        let res = xhci::interrupt_in(m.dev.slot_id, ep_id, &mut buf);
        let len = match res {
            Ok(n) if n >= 3 => n,
            Ok(_) => continue,
            Err(e) => {
                vga_buffer::log_line(e);
                continue;
            }
        };

        let evt = parse_mouse_report(m.last_buttons, &buf[..len.min(8)]);
        m.last_buttons = evt.buttons;
        events.push(evt);
    }

    events
}

/// Parse a boot-protocol mouse report (3–4 bytes).
///
/// report[0] = buttons bitfield
/// report[1] = X delta (i8)
/// report[2] = Y delta (i8)
/// report[3] = wheel (i8, optional)
fn parse_mouse_report(prev_buttons: u8, report: &[u8]) -> MouseEvent {
    let buttons = report[0];
    let dx = report.get(1).copied().unwrap_or(0) as i8 as i16;
    let dy = report.get(2).copied().unwrap_or(0) as i8 as i16;
    let wheel = report.get(3).copied().unwrap_or(0) as i8;

    let changed = prev_buttons ^ buttons;
    let pressed = changed & buttons;
    let released = changed & prev_buttons;

    MouseEvent {
        dx,
        dy,
        wheel,
        buttons,
        pressed,
        released,
    }
}
