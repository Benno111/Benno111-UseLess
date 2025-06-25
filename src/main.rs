
mod boot_manager;
mod drivers;
mod framebuffer;
mod input;
mod apps_launcher;
mod settings_app;
mod config;
mod utils;

fn main() {
    boot_manager::boot_sequence();
}
