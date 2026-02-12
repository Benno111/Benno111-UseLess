//! Minimal ACPI power-management stubs.
//! Real support would parse ACPI tables and invoke S5/S4 via FADT/DSDT methods.

use crate::serial;
use x86_64::instructions::port::Port;

pub fn init() {
    serial::log_line("[acpi] initializing power management stubs");
}

/// Request system power-off.
#[allow(dead_code)]
pub fn power_off() -> ! {
    serial::log_line("[acpi] power_off requested");
    unsafe {
        // QEMU ACPI power-off.
        let mut qemu: Port<u16> = Port::new(0x604);
        qemu.write(0x2000);
        // Bochs/QEMU legacy.
        let mut bochs: Port<u16> = Port::new(0xB004);
        bochs.write(0x2000);
    }
    loop {
        x86_64::instructions::hlt();
    }
}

/// Request system reboot.
#[allow(dead_code)]
pub fn reboot() -> ! {
    serial::log_line("[acpi] reboot requested");
    unsafe {
        // Reset Control Register (CF9) reset.
        let mut reset: Port<u8> = Port::new(0xCF9);
        reset.write(0x02);
        reset.write(0x06);
        // Fallback: keyboard controller reset.
        let mut kbd: Port<u8> = Port::new(0x64);
        kbd.write(0xFE);
    }
    loop {
        x86_64::instructions::hlt();
    }
}
