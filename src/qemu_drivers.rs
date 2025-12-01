//! Minimal QEMU device stubs so the kernel can acknowledge the emulated environment.
//! These are placeholders; real drivers would map MMIO BARs and negotiate virtio features.

use crate::vga_buffer;

pub fn init() {
    vga_buffer::log_line("[qemu] initializing emulated devices");
    init_rtc();
    init_virtio_blk();
    init_virtio_net();
}

fn init_rtc() {
    // Stub for QEMU RTC.
    vga_buffer::log_line("[qemu] RTC stub ready");
}

fn init_virtio_blk() {
    // Stub for VirtIO block device.
    vga_buffer::log_line("[qemu] virtio-blk stub ready");
}

fn init_virtio_net() {
    // Stub for VirtIO network device.
    vga_buffer::log_line("[qemu] virtio-net stub ready");
}
