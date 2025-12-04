use alloc::format;
use spin::Mutex;
use libm::{sqrtf, roundf};
use crate::{vga_buffer, windowing};

#[derive(Clone, Copy)]
pub enum InputEvent {
    Key(char),
    MouseMove { dx: isize, dy: isize },
}

// =============================
// Mouse acceleration settings
// =============================

// Base sensitivity multiplier (applies at all speeds)
const MOUSE_SENS: f32 = 2.4;

// Acceleration strength (low value keeps response snappy without runaway speed)
const MOUSE_ACCEL: f32 = 0.15;

// Prevent insane movement from bad deltas or overflow
const MOUSE_MAX_DELTA: f32 = 1000.0;

// Ignore tiny jitter to avoid cursor wiggle
const MOUSE_DEADZONE: f32 = 0.25;

// Cap the gain so wild packets don't explode
const MOUSE_MAX_GAIN: f32 = 8.0;

// =============================
// Mouse acceleration function
// =============================

fn apply_mouse_accel(dx: isize, dy: isize) -> (isize, isize) {
    let dx_f = dx as f32;
    let dy_f = dy as f32;

    // Clamp extreme input from buggy hardware
    let dx_f = dx_f.clamp(-MOUSE_MAX_DELTA, MOUSE_MAX_DELTA);
    let dy_f = dy_f.clamp(-MOUSE_MAX_DELTA, MOUSE_MAX_DELTA);

    // Speed = vector length
    // Use libm sqrt because we're in a no_std environment
    let speed = sqrtf(dx_f * dx_f + dy_f * dy_f);

    // Drop tiny jitter; keep hardware noise from nudging the cursor
    if speed < MOUSE_DEADZONE {
        return (0, 0);
    }

    // Acceleration:
    // gain = base sensitivity + (speed * accel * 0.01)
    let mut gain = MOUSE_SENS + (speed * MOUSE_ACCEL * 0.01);
    if gain > MOUSE_MAX_GAIN {
        gain = MOUSE_MAX_GAIN;
    }

    let dx_scaled = dx_f * gain;
    let dy_scaled = dy_f * gain;

    let dx_out = roundf(dx_scaled);
    let dy_out = roundf(dy_scaled);

    (dx_out as isize, dy_out as isize)
}

// =============================
// Event Queue
// =============================

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

    fn push(&mut self, ev: InputEvent) -> bool {
        let next = (self.tail + 1) % self.buf.len();
        if next != self.head {
            self.buf[self.tail] = Some(ev);
            self.tail = next;
            true
        } else {
            // queue full: drop silently
            false
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
// =============================
// Public API
// =============================

pub fn enqueue_event(ev: InputEvent) {
    let mut q = QUEUE.lock();
    let _ = q.push(ev);
}

pub fn enqueue_events<I: IntoIterator<Item = InputEvent>>(events: I) {
    let mut q = QUEUE.lock();
    for ev in events {
        let _ = q.push(ev);
    }
}

pub fn queue_len() -> usize {
    QUEUE.lock().len()
}

// =============================
// Event Handler
// =============================

fn handle_event(ev: InputEvent) {
    match ev {
        InputEvent::MouseMove { dx, dy } => {
            // Apply acceleration
            let (adx, ady) = apply_mouse_accel(dx, dy);

            windowing::move_mouse(adx, ady);
        }
        InputEvent::Key('\n') => {
            windowing::mouse_click_left();
            vga_buffer::log_line("[Input] mouse left click via Enter");
        }
        InputEvent::Key('\u{8}') => {
            windowing::mouse_click_right();
            vga_buffer::log_line("[Input] mouse right click via Backspace");
        }
        InputEvent::Key(c) => {
            vga_buffer::log_line(&format!("[Input] key: {}", c));
        }
    }
}

// =============================
// Main Poll Function
// =============================

pub fn poll_input_events() {
    let mut had_event = false;
    let mut processed = 0usize;

    // Drain queue with only *one* lock
    {
        let mut q = QUEUE.lock();
        while let Some(ev) = q.pop() {
            had_event = true;
            processed += 1;
            handle_event(ev);
        }
    }

    let _ = processed;
    let _ = had_event;
}
