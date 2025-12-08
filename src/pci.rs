//! Minimal PCI stub to allow USB xHCI discovery wiring.

#[derive(Clone, Copy, Debug)]
pub struct PciDevice {
    pub bus: u8,
    pub slot: u8,
    pub func: u8,
}

/// Return an xHCI device if found. Stubbed to None for now.
pub fn find_xhci() -> Option<PciDevice> {
    None
}

/// Return BAR0 value for the given device. Stubbed to 0 for now.
pub fn bar0(_dev: &PciDevice) -> u32 {
    0
}
