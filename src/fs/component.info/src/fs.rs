//! Minimal FAT32-like main volume stub.
//! A real implementation would hook up `fatfs` with a block device; here we just
//! reserve a static buffer and log that the volume is available.

use crate::serial;

const MAIN_VOL_SIZE: usize = 512 * 1024; // 512 KiB scratch "disk"
static mut MAIN_VOL: [u8; MAIN_VOL_SIZE] = [0; MAIN_VOL_SIZE];
static mut INIT: bool = false;
const LOG_AREA_SIZE: usize = 4096;
const LOG_MAGIC: &[u8; 8] = b"OSKLOG01";

pub fn init_main_volume() {
    unsafe {
        if INIT {
            return;
        }
        // Zero the volume to simulate a freshly formatted FAT32 space.
        let ptr = core::ptr::addr_of_mut!(MAIN_VOL) as *mut u8;
        core::ptr::write_bytes(ptr, 0, MAIN_VOL_SIZE);
        INIT = true;
    }
    serial::log_line("[fs] FAT32 main volume (stub) initialized");
}

/// Write a small log blob into a reserved slot near the end of the stub volume.
/// Layout:
///   magic[8] = "OSKLOG01"
///   path_len (u8)
///   payload_len (u32 LE)
///   path bytes
///   payload bytes
pub fn embed_log(path: &str, payload: &[u8]) -> Result<(), &'static str> {
    if path.is_empty() {
        return Err("[fs] embed_log: empty path");
    }
    if path.len() > 255 {
        return Err("[fs] embed_log: path too long");
    }
    unsafe {
        if !INIT {
            return Err("[fs] embed_log: volume not initialized");
        }
        if LOG_AREA_SIZE + 16 > MAIN_VOL_SIZE {
            return Err("[fs] embed_log: log area misconfigured");
        }
        let start = MAIN_VOL_SIZE - LOG_AREA_SIZE;
        let area = &mut MAIN_VOL[start..];
        let path_len = path.len() as u8;

        let header = 8 + 1 + 4; // magic + path_len + payload_len
        let max_payload = LOG_AREA_SIZE.saturating_sub(header + path.len());
        let payload_len = core::cmp::min(payload.len(), max_payload);

        if payload_len == 0 {
            return Err("[fs] embed_log: payload too large for slot");
        }

        // Clear the area first.
        for b in area.iter_mut() {
            *b = 0;
        }

        area[..8].copy_from_slice(LOG_MAGIC);
        area[8] = path_len;
        area[9..13].copy_from_slice(&(payload_len as u32).to_le_bytes());

        let mut idx = header;
        area[idx..idx + path.len()].copy_from_slice(path.as_bytes());
        idx += path.len();
        area[idx..idx + payload_len].copy_from_slice(&payload[..payload_len]);
    }

    Ok(())
}
