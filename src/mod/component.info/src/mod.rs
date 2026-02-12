use alloc::{format, vec::Vec};
use crate::serial;
use crate::pci;

pub mod device;
pub mod descriptor;
pub mod hid;
pub mod mouse;
pub mod xhci;

use device::UsbDevice;

/// PCI + xHCI discovery.
pub fn init_pci_usb() {
    if let Some(xhci_dev) = pci::find_xhci() {
        let bar0 = pci::bar0(&xhci_dev);
        serial::log_line(&format!(
            "[USB] xHCI candidate at {:02x}:{:02x}.{} BAR0=0x{:08x}",
            xhci_dev.bus, xhci_dev.slot, xhci_dev.func, bar0,
        ));

        match xhci::init_from_pci(&xhci_dev, bar0) {
            Ok(_) => serial::log_line("[USB] xHCI initialized."),
            Err(e) => serial::log_line(e),
        }
    } else {
        serial::log_line("[USB] No xHCI controller detected via PCI.");
    }
}

/// Low-level port scan (ports only).
pub fn enumerate_ports() -> Vec<UsbDevice> {
    xhci::enumerate_ports()
}
