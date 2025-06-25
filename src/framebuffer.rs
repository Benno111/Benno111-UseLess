
pub fn init() {
    clear_screen();
    draw_text("Desktop Initialized", 2, 2);
}

pub fn clear_screen() {
    println!("[FB] Clear screen");
}

pub fn draw_text(text: &str, x: usize, y: usize) {
    println!("[FB] ({}, {}): {}", x, y, text);
}

pub fn render_frame() {
    println!("[FB] Frame rendered.");
}
