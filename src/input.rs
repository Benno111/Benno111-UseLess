
use alloc::format;
use core::sync::atomic::{AtomicUsize, Ordering};
use spin::Mutex;
use crate::{vga_buffer, windowing};

#[derive(Clone, Copy)]
pub enum InputEvent {
    Key(char),
    MouseMove { dx: isize, dy: isize },
}

struct EventQueue {
    buf: [Option<InputEvent>; 32],
    head: usize,
    tail: usize,
}

impl EventQueue {
    const fn new() -> Self {
        Self {
            buf: [None; 32],
            head: 0,
            tail: 0,
        }
    }

    fn push(&mut self, ev: InputEvent) {
        let next = (self.tail + 1) % self.buf.len();
        if next != self.head {
            self.buf[self.tail] = Some(ev);
            self.tail = next;
        }
    }

    fn pop(&mut self) -> Option<InputEvent> {
        if self.head == self.tail {
            return None;
        }
        let ev = self.buf[self.head].take();
        self.head = (self.head + 1) % self.buf.len();
        ev
    }

    fn len(&self) -> usize {
        if self.tail >= self.head {
            self.tail - self.head
        } else {
            self.buf.len() - (self.head - self.tail)
        }
    }
}

static QUEUE: Mutex<EventQueue> = Mutex::new(EventQueue::new());
static EVENT_IDX: AtomicUsize = AtomicUsize::new(0);
const SCRIPTED_EVENTS: &[InputEvent] = &[
    InputEvent::Key('h'),
    InputEvent::Key('i'),
    InputEvent::MouseMove { dx: 15, dy: 8 },
    InputEvent::Key('!'),
];

pub fn enqueue_demo_inputs() {
    let mut q = QUEUE.lock();
    for ev in SCRIPTED_EVENTS {
        q.push(*ev);
    }
}

/// Allow other subsystems (e.g., drivers) to enqueue input events.
pub fn enqueue_event(ev: InputEvent) {
    QUEUE.lock().push(ev);
}

pub fn poll_input_events() {
    // Drain all pending events so movement feels responsive and the queue stays small.
    let mut had_event = false;
    while let Some(ev) = QUEUE.lock().pop() {
        had_event = true;
        match ev {
            InputEvent::Key(c) => {
                vga_buffer::log_line(&format!("[Input] key: {}", c));
            }
            InputEvent::MouseMove { dx, dy } => {
                windowing::move_mouse(dx, dy);
                vga_buffer::log_line(&format!("[Input] mouse move: ({}, {})", dx, dy));
            }
        }
    }

    if had_event {
        return;
    }

    // fall back to a fixed script if queue empty
    let idx = EVENT_IDX.fetch_add(1, Ordering::SeqCst);
    if let Some(ev) = SCRIPTED_EVENTS.get(idx) {
        match ev {
            InputEvent::Key(c) => vga_buffer::log_line(&format!("[Input] key: {}", c)),
            InputEvent::MouseMove { dx, dy } => {
                windowing::move_mouse(*dx, *dy);
                vga_buffer::log_line(&format!("[Input] mouse move: ({}, {})", dx, dy));
            }
        }
    } else {
        vga_buffer::log_line("[Input] No more scripted events");
    }
}

/// Current queued input event count (for driver throttling).
pub fn queue_len() -> usize {
    QUEUE.lock().len()
}
