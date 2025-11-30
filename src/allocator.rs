//! Tiny bump allocator backed by a fixed static buffer.

use core::alloc::{GlobalAlloc, Layout};
use core::ptr::null_mut;
use spin::Mutex;

const HEAP_SIZE: usize = 64 * 1024; // 64 KiB
static mut HEAP: [u8; HEAP_SIZE] = [0; HEAP_SIZE];

struct BumpAllocator {
    heap_start: usize,
    heap_end: usize,
    next: usize,
}

impl BumpAllocator {
    const fn new() -> Self {
        Self {
            heap_start: 0,
            heap_end: 0,
            next: 0,
        }
    }

    unsafe fn init(&mut self, heap_start: usize, heap_size: usize) {
        self.heap_start = heap_start;
        self.heap_end = heap_start + heap_size;
        self.next = heap_start;
    }

    unsafe fn alloc(&mut self, layout: Layout) -> *mut u8 {
        let alloc_start = align_up(self.next, layout.align());
        let alloc_end = match alloc_start.checked_add(layout.size()) {
            Some(end) => end,
            None => return null_mut(),
        };
        if alloc_end > self.heap_end {
            return null_mut();
        }
        self.next = alloc_end;
        alloc_start as *mut u8
    }
}

fn align_up(addr: usize, align: usize) -> usize {
    (addr + align - 1) & !(align - 1)
}

struct LockedAllocator(Mutex<BumpAllocator>);

unsafe impl GlobalAlloc for LockedAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let mut bump = self.0.lock();
        bump.alloc(layout)
    }

    unsafe fn dealloc(&self, _ptr: *mut u8, _layout: Layout) {
        // bump allocator does not support free
    }
}

#[global_allocator]
static ALLOCATOR: LockedAllocator = LockedAllocator(Mutex::new(BumpAllocator::new()));

#[allow(static_mut_refs)]
pub unsafe fn init_heap() {
    ALLOCATOR
        .0
        .lock()
        .init(HEAP.as_mut_ptr() as usize, HEAP_SIZE);
}

// Without the alloc_error_handler feature we can't define a handler; allow oom to panic via core.
