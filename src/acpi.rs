//! Minimal ACPI power-management stubs.
//! Real support would parse ACPI tables and invoke S5/S4 via FADT/DSDT methods.

use crate::vga_buffer;

pub fn init() {
    vga_buffer::log_line("[acpi] initializing power management stubs");
}

/// Request system power-off (stub).
pub fn power_off() -> ! {
    vga_buffer::log_line("[acpi] power_off requested (stub)");
    loop {
        // Halt to simulate shutdown.
        x86_64::instructions::hlt();
    }
}

/// Request system reboot (stub).
pub fn reboot() -> ! {
    vga_buffer::log_line("[acpi] reboot requested (stub)");
    // In real impl: write to 0xCF9 or use ACPI reset register.
    loop {
        x86_64::instructions::hlt();
    }
}
