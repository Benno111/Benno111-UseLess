use alloc::vec::Vec;

use crate::usb::device::{UsbDevice, UsbEndpoint, EndpointType};
use crate::vga_buffer;
use crate::usb::xhci; // for xhci::interrupt_in(..)

/// A single USB keyboard attached via xHCI.
pub struct UsbKeyboard {
    /// Whole device reference (slot_id, config tree).
    pub dev: UsbDevice,
    /// Interface index in dev.configurations[0].interfaces
    pub iface_index: usize,
    /// Endpoint index within that interface's endpoints vec.
    pub ep_index: usize,
    /// Cached endpoint address (including direction bit).
    pub ep_address: u8,
    /// Last 8-byte HID report.
    last_report: [u8; 8],
}

/// A logical key event from a USB keyboard.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum KeyEventKind {
    Press,
    Release,
}

#[derive(Clone, Copy, Debug)]
pub struct KeyEvent {
    pub ch: Option<char>, // printable char if known
    pub keycode: u8,      // HID usage ID
    pub kind: KeyEventKind,
}

/// Initialize keyboards from enumerated USB devices by scanning
/// configuration/interface/endpoint descriptors for HID keyboard interfaces.
pub fn init_keyboards_from_devices(devs: &[UsbDevice]) -> Vec<UsbKeyboard> {
    let mut out = Vec::new();

    for dev in devs {
        let kbs = find_keyboard_endpoints(dev);
        for (iface_idx, ep_idx, ep) in kbs {
            vga_buffer::log_line(&alloc::format!(
                "[USB-HID] Keyboard on port {} slot {} if={} ep=0x{:02x}",
                dev.port,
                dev.slot_id,
                iface_idx,
                ep.address
            ));

            out.push(UsbKeyboard {
                dev: dev.clone(),
                iface_index: iface_idx,
                ep_index: ep_idx,
                ep_address: ep.address,
                last_report: [0u8; 8],
            });
        }
    }

    if out.is_empty() {
        vga_buffer::log_line("[USB-HID] No keyboards found via HID descriptors.");
    }

    out
}

fn find_keyboard_endpoints(dev: &UsbDevice) -> Vec<(usize, usize, UsbEndpoint)> {
    let mut out = Vec::new();

    // For now, use configuration 0 only.
    if dev.configurations.is_empty() {
        return out;
    }
    let cfg = &dev.configurations[0];

    for (iface_idx, iface) in cfg.interfaces.iter().enumerate() {
        // HID keyboard: class=0x03, protocol=0x01 is typical.
        if iface.interface_class == 0x03 && iface.interface_protocol == 0x01 {
            // Find an interrupt IN endpoint.
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

/// Poll all keyboards once and return a list of key events.
pub fn poll_keyboards(keyboards: &mut [UsbKeyboard]) -> Vec<KeyEvent> {
    let mut events = Vec::new();

    for kb in keyboards.iter_mut() {
        let mut buf = [0u8; 8];

        // Derive ep_id from endpoint address (address & 0x0F is common).
        let ep_id = kb.ep_address & 0x0F;

        let res = xhci::interrupt_in(kb.dev.slot_id, ep_id, &mut buf);

        let len = match res {
            Ok(n) if n >= 8 => 8,
            Ok(_) => continue, // short packet; ignore for now
            Err(e) => {
                vga_buffer::log_line(e);
                continue;
            }
        };

        if buf[..len] == kb.last_report[..len] {
            continue; // no change
        }

        let new_events = parse_keyboard_report(&kb.last_report, &buf);
        events.extend_from_slice(&new_events);

        kb.last_report.copy_from_slice(&buf[..8]);
    }

    events
}

/// Parse an 8-byte boot-protocol HID keyboard report.
fn parse_keyboard_report(prev: &[u8; 8], current: &[u8; 8]) -> Vec<KeyEvent> {
    let mut events = Vec::new();

    // Simple press/release detection for keycodes in bytes 2..8.
    let prev_keys = &prev[2..8];
    let cur_keys = &current[2..8];

    // Releases: keys present before, missing now.
    for &code in prev_keys.iter().filter(|&&c| c != 0) {
        if !cur_keys.contains(&code) {
            events.push(KeyEvent {
                ch: hid_usage_to_char(code, current[0]),
                keycode: code,
                kind: KeyEventKind::Release,
            });
        }
    }

    // Presses: keys present now, not present before.
    for &code in cur_keys.iter().filter(|&&c| c != 0) {
        if !prev_keys.contains(&code) {
            events.push(KeyEvent {
                ch: hid_usage_to_char(code, current[0]),
                keycode: code,
                kind: KeyEventKind::Press,
            });
        }
    }

    events
}

/// Very small US QWERTY HID usage → char map.
fn hid_usage_to_char(usage: u8, bm_modifier: u8) -> Option<char> {
    let shifted = (bm_modifier & 0b0010_0010) != 0;

    let ch = match usage {
        0x04 => if shifted { 'A' } else { 'a' },
        0x05 => if shifted { 'B' } else { 'b' },
        0x06 => if shifted { 'C' } else { 'c' },
        0x07 => if shifted { 'D' } else { 'd' },
        0x08 => if shifted { 'E' } else { 'e' },
        0x09 => if shifted { 'F' } else { 'f' },
        0x0A => if shifted { 'G' } else { 'g' },
        0x0B => if shifted { 'H' } else { 'h' },
        0x0C => if shifted { 'I' } else { 'i' },
        0x0D => if shifted { 'J' } else { 'j' },
        0x0E => if shifted { 'K' } else { 'k' },
        0x0F => if shifted { 'L' } else { 'l' },
        0x10 => if shifted { 'M' } else { 'm' },
        0x11 => if shifted { 'N' } else { 'n' },
        0x12 => if shifted { 'O' } else { 'o' },
        0x13 => if shifted { 'P' } else { 'p' },
        0x14 => if shifted { 'Q' } else { 'q' },
        0x15 => if shifted { 'R' } else { 'r' },
        0x16 => if shifted { 'S' } else { 's' },
        0x17 => if shifted { 'T' } else { 't' },
        0x18 => if shifted { 'U' } else { 'u' },
        0x19 => if shifted { 'V' } else { 'v' },
        0x1A => if shifted { 'W' } else { 'w' },
        0x1B => if shifted { 'X' } else { 'x' },
        0x1C => if shifted { 'Y' } else { 'y' },
        0x1D => if shifted { 'Z' } else { 'z' },

        0x1E => if shifted { '!' } else { '1' },
        0x1F => if shifted { '@' } else { '2' },
        0x20 => if shifted { '#' } else { '3' },
        0x21 => if shifted { '$' } else { '4' },
        0x22 => if shifted { '%' } else { '5' },
        0x23 => if shifted { '^' } else { '6' },
        0x24 => if shifted { '&' } else { '7' },
        0x25 => if shifted { '*' } else { '8' },
        0x26 => if shifted { '(' } else { '9' },
        0x27 => if shifted { ')' } else { '0' },

        0x28 => '\n',   // Enter
        0x2C => ' ',    // Space
        0x2D => if shifted { '_' } else { '-' },
        0x2E => if shifted { '+' } else { '=' },
        0x2F => if shifted { '{' } else { '[' },
        0x30 => if shifted { '}' } else { ']' },
        0x31 => if shifted { '|' } else { '\\' },
        0x33 => if shifted { ':' } else { ';' },
        0x34 => if shifted { '"' } else { '\'' },
        0x36 => if shifted { '>' } else { '.' },
        0x37 => if shifted { '?' } else { '/' },
        _ => return None,
    };

    Some(ch)
}
