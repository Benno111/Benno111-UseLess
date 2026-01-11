#![allow(dead_code)]
#![allow(unused_imports)]

use alloc::{format, vec::Vec};

use crate::vga_buffer;

pub mod device;
pub mod descriptor;
pub mod hid;
pub mod mouse;
#[path = "xhci_stub.rs"]
pub mod xhci;

use device::UsbDevice;

/// Discover an xHCI controller via PCI and perform minimal init.
pub fn init_pci_usb() {
    if let Some(xhci_dev) = crate::pci::find_xhci() {
        let bar0 = crate::pci::bar0(&xhci_dev);
        vga_buffer::log_line(&format!(
            "[USB] xHCI candidate at {:02x}:{:02x}.{} BAR0=0x{:08x}",
            xhci_dev.bus, xhci_dev.slot, xhci_dev.func, bar0,
        ));

        match xhci::init_from_pci(&xhci_dev, bar0) {
            Ok(_) => vga_buffer::log_line("[USB] xHCI initialized."),
            Err(e) => vga_buffer::log_line(e),
        }
    } else {
        vga_buffer::log_line("[USB] No xHCI controller detected via PCI.");
    }
}

/// Enumerate devices on xHCI root ports.
pub fn enumerate_ports() -> Vec<UsbDevice> {
    xhci::enumerate_ports()
}

/// High-level entry point for the USB stack.
pub fn init() {
    init_pci_usb();
}
