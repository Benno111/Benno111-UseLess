use core::slice;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct BootInfo {
    pub magic: u64,
    pub version: u32,
    pub boot_method: u32,
    pub memory_map_ptr: *const MemoryRegion,
    pub memory_map_len: usize,
    pub rsdp_addr: u64,
    pub physical_memory_offset: u64,
    pub kernel_addr: u64,
    pub kernel_len: u64,
    pub kernel_image_offset: u64,
    pub cmdline_ptr: *const u8,
    pub cmdline_len: usize,
}

impl BootInfo {
    pub fn memory_regions(&self) -> &'static [MemoryRegion] {
        if self.memory_map_ptr.is_null() || self.memory_map_len == 0 {
            &[]
        } else {
            unsafe { slice::from_raw_parts(self.memory_map_ptr, self.memory_map_len) }
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct MemoryRegion {
    pub start: u64,
    pub end: u64,
    pub kind: MemoryRegionKind,
}

#[repr(u32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MemoryRegionKind {
    Usable = 1,
    Reserved = 2,
    AcpiReclaimable = 3,
    AcpiNvs = 4,
    BadMemory = 5,
    Bootloader = 6,
    Kernel = 7,
    KernelStack = 8,
    Framebuffer = 9,
    Unknown = 0xFFFF_FFFF,
}
