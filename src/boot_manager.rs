
use crate::drivers;
use crate::framebuffer;
use crate::input;
use crate::apps_launcher;
use crate::config;

pub fn boot_sequence() {
    if firmware_keyboard::wait_for_ctrl_b_hold() {
        enter_boot_options_menu();
    } else {
        normal_boot();
    }
}

fn enter_boot_options_menu() {
    let mut selected = 0;
    let options = ["Toggle Verbose Boot", "Select Boot Device", "Continue Boot"];

    loop {
        framebuffer::clear_screen();
        framebuffer::draw_text("Boot Options Menu", 2, 2);

        for (i, option) in options.iter().enumerate() {
            let prefix = if i == selected { ">> " } else { "   " };
            framebuffer::draw_text(&format!("{}{}", prefix, option), 4, 4 + i * 2);
        }

        framebuffer::render_frame();

        match firmware_keyboard::read_key() {
            Some(firmware_keyboard::FWKey::Up) => {
                if selected > 0 { selected -= 1; }
            }
            Some(firmware_keyboard::FWKey::Down) => {
                if selected < options.len() - 1 { selected += 1; }
            }
            Some(firmware_keyboard::FWKey::Enter) => {
                match selected {
                    0 => config::toggle_verbose(),
                    1 => framebuffer::draw_text("Boot device selection: Default", 4, 12),
                    2 => return,
                    _ => {}
                }
            }
            Some(firmware_keyboard::FWKey::Esc) => return,
            _ => {}
        }
    }
}

fn normal_boot() {
    if config::is_verbose() {
        framebuffer::draw_text("Booting in VERBOSE mode...", 2, 2);
    }

    drivers::init_ps2();
    drivers::init_usb();
    framebuffer::init();
    apps_launcher::start();

    loop {
        input::poll_input_events();
        framebuffer::render_frame();
    }
}

mod firmware_keyboard {
    use std::time::{Instant, Duration};

    #[derive(Debug)]
    pub enum FWKey {
        Up, Down, Enter, Esc, Char(char),
    }

    pub fn wait_for_ctrl_b_hold() -> bool {
        println!("(Simulated) Waiting for Ctrl+B hold...");
        std::thread::sleep(Duration::from_secs(1));
        false
    }

    pub fn read_key() -> Option<FWKey> {
        None
    }
}
