use alloc::format;

use crate::framebuffer;
use crate::{driver_api, input, serial};

struct Task {
    name: &'static str,
    cpu_pct: u8,
    mem_kb: u32,
}

// Static sample tasks (no real scheduler yet).
const TASKS: &[Task] = &[
    Task {
        name: "kernel",
        cpu_pct: 17,
        mem_kb: 5120,
    },
    Task {
        name: "windowing",
        cpu_pct: 9,
        mem_kb: 2048,
    },
    Task {
        name: "drivers",
        cpu_pct: 6,
        mem_kb: 1024,
    },
    Task {
        name: "input",
        cpu_pct: 3,
        mem_kb: 768,
    },
    Task {
        name: "idle",
        cpu_pct: 65,
        mem_kb: 256,
    },
];

pub fn render(x: usize, y: usize, w: usize, h: usize) {
    // Header
    framebuffer::draw_text_no_invalidate("PID  NAME         CPU  MEM", x + 2, y + 2);
    framebuffer::draw_text_no_invalidate("--------------------------------", x + 2, y + 10);

    let mut row_y = y + 20;
    for (idx, task) in TASKS.iter().enumerate() {
        if row_y + 10 >= y + h {
            break;
        }
        let line = format!(
            "{:>3}  {:<11} {:>3}% {:>4}K",
            100 + idx,
            truncate(task.name, 11),
            task.cpu_pct,
            task.mem_kb
        );
        framebuffer::draw_text_no_invalidate(&line, x + 2, row_y);
        row_y += 12;
    }

    // Footer summary.
    let total_cpu: u32 = TASKS.iter().map(|t| t.cpu_pct as u32).sum();
    let total_mem: u32 = TASKS.iter().map(|t| t.mem_kb).sum();
    let summary = format!(
        "Total CPU: {:>3}%   Total MEM: {:>5}K",
        total_cpu.min(100) as u32,
        total_mem
    );
    if row_y + 12 < y + h {
        framebuffer::draw_text_no_invalidate(&summary, x + 2, row_y + 4);
        let detail_y = row_y + 16;
        let input_q = input::queue_len();
        let stats = driver_api::stats();
        framebuffer::draw_text_no_invalidate(
            &format!(
                "Input queue: {:>3}  Poll skips: {:>2}  Budget: {:>2}B",
                input_q, stats.poll_skips, stats.poll_budget
            ),
            x + 2,
            detail_y,
        );
        let _ = serial::log_line;
    }

    // Draw a simple usage bar under the header.
    let bar_y = y + 14;
    let bar_w = w.saturating_sub(12);
    let bar_x = x + 2;
    framebuffer::draw_rect(bar_x, bar_y, bar_w, 6, (30, 33, 40));
    let filled = bar_w.saturating_mul(total_cpu.min(100) as usize) / 100;
    if filled > 0 {
        framebuffer::fill_rect(bar_x + 1, bar_y + 1, filled.saturating_sub(2), 4, crate::framebuffer::COLOR_ACCENT);
    }
}

fn truncate(s: &'static str, max: usize) -> &'static str {
    if s.len() <= max {
        s
    } else {
        &s[..max]
    }
}
