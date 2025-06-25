mod boot_manager;
mod drivers;
mod framebuffer;
mod input;
mod apps_launcher;
mod settings_app;
mod config;
mod utils;

fn main() {
    // Start boot sequence
    boot_manager::boot_sequence();
}
