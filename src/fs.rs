//! Minimal FAT32-like main volume stub.
//! A real implementation would hook up `fatfs` with a block device; here we just
//! reserve a static buffer and log that the volume is available.

use crate::vga_buffer;

const MAIN_VOL_SIZE: usize = 512 * 1024; // 512 KiB scratch "disk"
static mut MAIN_VOL: [u8; MAIN_VOL_SIZE] = [0; MAIN_VOL_SIZE];
static mut INIT: bool = false;

pub fn init_main_volume() {
    unsafe {
        if INIT {
            return;
        }
        // Zero the volume to simulate a freshly formatted FAT32 space.
        MAIN_VOL.fill(0);
        INIT = true;
    }
    vga_buffer::log_line("[fs] FAT32 main volume (stub) initialized");
}
