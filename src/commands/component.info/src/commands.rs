//! Tiny command handler that operates on the in-memory account table.

use alloc::format;

use crate::accounts;
use crate::serial;

#[allow(dead_code)]
pub fn run_command(line: &str) {
    serial::log(format_args!("> {line}"));

    let mut parts = line.split_whitespace();
    match parts.next() {
        Some("useradd") => {
            if let (Some(user), Some(pass)) = (parts.next(), parts.next()) {
                match accounts::add_user(user, pass) {
                    Ok(()) => {
                        serial::log_line(&format!("user '{user}' created"));
                    }
                    Err(e) => serial::log_line(&format!("error: {}", e.as_str())),
                }
            } else {
                serial::log_line("usage: useradd <name> <password>");
            }
        }
        Some("userdel") => {
            if let Some(user) = parts.next() {
                match accounts::delete_user(user) {
                    Ok(()) => {
                        serial::log_line(&format!("user '{user}' removed"));
                    }
                    Err(e) => serial::log_line(&format!("error: {}", e.as_str())),
                }
            } else {
                serial::log_line("usage: userdel <name>");
            }
        }
        Some("login") => {
            if let (Some(user), Some(pass)) = (parts.next(), parts.next()) {
                if accounts::authenticate(user, pass) {
                    serial::log_line(&format!("login successful for '{user}'"));
                } else {
                    serial::log_line(&format!("invalid credentials for '{user}'"));
                }
            } else {
                serial::log_line("usage: login <name> <password>");
            }
        }
        Some("listusers") => {
            let mut empty = true;
            accounts::list_users(|name| {
                empty = false;
                serial::log_line(&format!("- {name}"));
            });
            if empty {
                serial::log_line("(no users)");
            }
        }
        Some(other) => {
            serial::log_line(&format!("unknown command: {other}"));
            serial::log_line("available: useradd, userdel, login, listusers");
        }
        None => {
            serial::log_line("no command provided");
        }
    }
}

// run_demo_session removed (unused)
