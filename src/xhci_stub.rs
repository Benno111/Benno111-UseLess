//! Stubbed xHCI backend so the USB stack can be wired without hardware access.

use alloc::vec::Vec;

use crate::pci::PciDevice;
use crate::usb::device::UsbDevice;

/// Stub: pretend to initialize and report that we're not actually driving hardware.
pub fn init_from_pci(_dev: &PciDevice, _bar0: u32) -> Result<(), &'static str> {
    Err("[xHCI] initialization stubbed; no controller active")
}

/// Stub: no devices present without a real controller.
pub fn enumerate_ports() -> Vec<UsbDevice> {
    Vec::new()
}

/// Stubbed interrupt IN transfer (returns an error so callers can log or ignore).
pub fn interrupt_in(_slot_id: u8, _ep_id: u8, _buf: &mut [u8]) -> Result<usize, &'static str> {
    Err("[xHCI] interrupt_in stubbed; no transfers performed")
}
