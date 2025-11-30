//! Tiny command handler that operates on the in-memory account table.

use alloc::format;

use crate::accounts;
use crate::vga_buffer;

pub fn run_command(line: &str) {
    vga_buffer::log(format_args!("> {line}"));

    let mut parts = line.split_whitespace();
    match parts.next() {
        Some("useradd") => {
            if let (Some(user), Some(pass)) = (parts.next(), parts.next()) {
                match accounts::add_user(user, pass) {
                    Ok(()) => {
                        vga_buffer::log_line(&format!("user '{user}' created"));
                    }
                    Err(e) => vga_buffer::log_line(&format!("error: {}", e.as_str())),
                }
            } else {
                vga_buffer::log_line("usage: useradd <name> <password>");
            }
        }
        Some("userdel") => {
            if let Some(user) = parts.next() {
                match accounts::delete_user(user) {
                    Ok(()) => {
                        vga_buffer::log_line(&format!("user '{user}' removed"));
                    }
                    Err(e) => vga_buffer::log_line(&format!("error: {}", e.as_str())),
                }
            } else {
                vga_buffer::log_line("usage: userdel <name>");
            }
        }
        Some("login") => {
            if let (Some(user), Some(pass)) = (parts.next(), parts.next()) {
                if accounts::authenticate(user, pass) {
                    vga_buffer::log_line(&format!("login successful for '{user}'"));
                } else {
                    vga_buffer::log_line(&format!("invalid credentials for '{user}'"));
                }
            } else {
                vga_buffer::log_line("usage: login <name> <password>");
            }
        }
        Some("listusers") => {
            let mut empty = true;
            accounts::list_users(|name| {
                empty = false;
                vga_buffer::log_line(&format!("- {name}"));
            });
            if empty {
                vga_buffer::log_line("(no users)");
            }
        }
        Some(other) => {
            vga_buffer::log_line(&format!("unknown command: {other}"));
            vga_buffer::log_line("available: useradd, userdel, login, listusers");
        }
        None => {
            vga_buffer::log_line("no command provided");
        }
    }
}

// run_demo_session removed (unused)
