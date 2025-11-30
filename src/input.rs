
use core::sync::atomic::{AtomicUsize, Ordering};
use crate::vga_buffer;

static EVENT_IDX: AtomicUsize = AtomicUsize::new(0);
const SCRIPTED_EVENTS: &[&str] = &[
    "[Input] keyboard event: Enter",
    "[Input] mouse move: (10,5)",
    "[Input] keyboard event: Esc",
];

pub fn poll_input_events() {
    let idx = EVENT_IDX.fetch_add(1, Ordering::SeqCst);
    if let Some(ev) = SCRIPTED_EVENTS.get(idx) {
        vga_buffer::log_line(ev);
    } else {
        vga_buffer::log_line("[Input] No more scripted events");
    }
}
