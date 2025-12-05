//! Dynamic kernel heap allocator using the full memory map.
//!
//! - Add one or more regions via `add_region` or `init_from_memory_map`.
//! - Metadata (free list nodes) live inside the free memory itself.

#![no_std]

use core::alloc::{GlobalAlloc, Layout};
use core::ptr::null_mut;
use spin::Mutex;

/// A free block in the heap.
struct ListNode {
    size: usize,
    next: Option<&'static mut ListNode>,
}

impl ListNode {
    const fn new(size: usize) -> Self {
        Self { size, next: None }
    }

    fn start_addr(&self) -> usize {
        self as *const ListNode as usize
    }

    fn end_addr(&self) -> usize {
        self.start_addr() + self.size
    }
}

/// Align `addr` upwards to `align` (must be power of two).
fn align_up(addr: usize, align: usize) -> usize {
    debug_assert!(align.is_power_of_two());
    (addr + align - 1) & !(align - 1)
}

const NODE_ALIGN: usize = core::mem::align_of::<ListNode>();
const NODE_SIZE: usize = core::mem::size_of::<ListNode>();

pub struct LinkedListAllocator {
    head: ListNode, // sentinel node; real list is in head.next
}

impl LinkedListAllocator {
    /// Create an empty allocator (no memory regions yet).
    pub const fn new() -> Self {
        Self {
            head: ListNode::new(0),
        }
    }

    /// Add a free region [start, start+size) to the allocator.
    ///
    /// You should only pass **mapped, usable** memory here (from your memory map).
    pub unsafe fn add_region(&mut self, start: usize, size: usize) {
        let mut region_start = align_up(start, NODE_ALIGN);
        let region_end = start.saturating_add(size);

        if region_end <= region_start {
            return;
        }

        let mut region_size = region_end - region_start;

        // Region must be large enough to hold a ListNode
        if region_size < NODE_SIZE {
            return;
        }

        // The free block becomes a ListNode stored in-place at region_start
        region_size = region_size & !(NODE_ALIGN - 1);

        self.free_block(region_start as *mut u8, region_size);
    }

    /// Free a block back into the allocator (internal use + can be used by kernel).
    ///
    /// Safety:
    /// - `ptr` must come from a previous `alloc_block` call (or from `add_region`),
    ///   and not be used after this call.
    pub unsafe fn free_block(&mut self, ptr: *mut u8, size: usize) {
        let size = size & !(NODE_ALIGN - 1);

        if size < NODE_SIZE {
            return; // too small to store a node, we just drop it
        }

        debug_assert_eq!(ptr as usize % NODE_ALIGN, 0);

        let mut node = ptr as *mut ListNode;
        node.write(ListNode {
            size,
            next: self.head.next.take(),
        });

        // Insert at front of list
        self.head.next = Some(&mut *node);
        // (Optional) you could coalesce neighbours here for less fragmentation.
    }

    fn alloc_block(&mut self, layout: Layout) -> *mut u8 {
        let size = layout.size().max(NODE_SIZE);
        let align = layout.align().max(NODE_ALIGN);

        // We require regions we add to be aligned to at least `align`.
        // If they are page-aligned (4KiB), all typical allocations work fine.
        assert!(align.is_power_of_two());

        let mut current = &mut self.head;

        while let Some(ref mut node) = current.next {
            let node_start = node.start_addr();
            let node_size = node.size;
            let node_end = node_start + node_size;

            // We only allocate from blocks that already start aligned.
            // This is okay if all regions you add are page-aligned.
            if node_start % align != 0 {
                current = current.next.as_mut().unwrap();
                continue;
            }

            // Check if it fits.
            if size > node_size {
                current = current.next.as_mut().unwrap();
                continue;
            }

            let alloc_start = node_start;
            let alloc_end = alloc_start + size;
            debug_assert!(alloc_end <= node_end);

            let remaining_size = node_size - size;

            if remaining_size >= NODE_SIZE {
                // Split block: front goes to caller, tail remains as free block.
                let new_free_start = alloc_end;
                let new_node_ptr = new_free_start as *mut ListNode;

                unsafe {
                    new_node_ptr.write(ListNode {
                        size: remaining_size,
                        next: node.next.take(),
                    });
                }

                current.next = Some(unsafe { &mut *new_node_ptr });
            } else {
                // Use whole block (too small to keep tail as a ListNode).
                current.next = node.next.take();
            }

            return alloc_start as *mut u8;
        }

        // Out of memory
        null_mut()
    }
}

/// Spin-locked wrapper so we can implement `GlobalAlloc`.
pub struct LockedAllocator(Mutex<LinkedListAllocator>);

impl LockedAllocator {
    pub const fn new() -> Self {
        LockedAllocator(Mutex::new(LinkedListAllocator::new()))
    }

    /// Add a region to the heap from a memory map entry.
    ///
    /// Safety:
    /// - Region must be mapped, usable RAM.
    /// - Must not overlap with kernel, stack, other reserved areas.
    pub unsafe fn add_region(&self, start: usize, size: usize) {
        self.0.lock().add_region(start, size);
    }

    /// Example helper: initialize from an iterator of (start, size) regions.
    ///
    /// Safety:
    /// Same as `add_region` for every region.
    pub unsafe fn init_from_memory_map<I>(&self, regions: I)
    where
        I: IntoIterator<Item = (usize, usize)>,
    {
        let mut alloc = self.0.lock();
        for (start, size) in regions {
            alloc.add_region(start, size);
        }
    }

    /// Optional: Expose a manual free for kernel subsystems (not used by GlobalAlloc).
    pub unsafe fn free_block(&self, ptr: *mut u8, size: usize) {
        self.0.lock().free_block(ptr, size);
    }
}

unsafe impl GlobalAlloc for LockedAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        self.0.lock().alloc_block(layout)
    }

    unsafe fn dealloc(&self, _ptr: *mut u8, _layout: Layout) {
        // For now, we don't hook global dealloc into our free_list.
        // If you want a fully general allocator, call:
        //   self.0.lock().free_block(_ptr, _layout.size());
        //
        // But doing so safely requires you to always pass correct sizes here.
        //
        // Uncomment if you're ready for full free():
        //
        // self.0.lock().free_block(_ptr, _layout.size());
    }
}

#[global_allocator]
static GLOBAL_ALLOCATOR: LockedAllocator = LockedAllocator::new();