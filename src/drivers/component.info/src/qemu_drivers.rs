//! Minimal QEMU device stubs so the kernel can acknowledge the emulated environment.
//! These are placeholders; real drivers would map MMIO BARs and negotiate virtio features.

use crate::serial;

pub fn init() {
    serial::log_line("[qemu] initializing emulated devices");
    init_rtc();
    init_virtio_blk();
    init_virtio_net();
}

fn init_rtc() {
    // Stub for QEMU RTC.
    serial::log_line("[qemu] RTC stub ready");
}

fn init_virtio_blk() {
    // Stub for VirtIO block device.
    serial::log_line("[qemu] virtio-blk stub ready");
}

fn init_virtio_net() {
    // Stub for VirtIO network device.
    serial::log_line("[qemu] virtio-net stub ready");
}
