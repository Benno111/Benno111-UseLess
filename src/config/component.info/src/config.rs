
use serde::Deserialize;

#[derive(Deserialize)]
pub struct DriverConfig {
    pub platform_exclusive_calls: Vec<String>,
}

static mut VERBOSE_BOOT: bool = true;

pub fn toggle_verbose() {
    unsafe {
        VERBOSE_BOOT = !VERBOSE_BOOT;
        let msg = if VERBOSE_BOOT {
            "Verbose Boot ENABLED"
        } else {
            "Verbose Boot DISABLED"
        };
        crate::framebuffer::draw_text(msg, 4, 14);
    }
}

pub fn is_verbose() -> bool {
    unsafe { VERBOSE_BOOT }
}

pub fn load_driver_config(_path: &str) -> DriverConfig {
    DriverConfig {
        platform_exclusive_calls: vec!["x86_64_specific_call".into()],
    }
}
