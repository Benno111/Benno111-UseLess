//! Minimal in-memory account table for the tiny kernel demo.
//! Uses fixed-size slots (no heap) and simple ASCII-only usernames/passwords.

use core::str;
use spin::Mutex;

const MAX_ACCOUNTS: usize = 8;
const MAX_USERNAME_LEN: usize = 16;
const MAX_PASSWORD_LEN: usize = 16;

#[derive(Clone, Copy)]
struct Account {
    username: [u8; MAX_USERNAME_LEN],
    password: [u8; MAX_PASSWORD_LEN],
}

impl Account {
    fn new(username: &str, password: &str) -> Self {
        let mut acc = Account {
            username: [0; MAX_USERNAME_LEN],
            password: [0; MAX_PASSWORD_LEN],
        };
        write_bytes(&mut acc.username, username);
        write_bytes(&mut acc.password, password);
        acc
    }

    fn username(&self) -> &str {
        bytes_to_str(&self.username)
    }

    fn password(&self) -> &str {
        bytes_to_str(&self.password)
    }
}

pub enum AccountError {
    Full,
    Exists,
    NotFound,
    EmptyField,
}

impl AccountError {
    pub fn as_str(&self) -> &'static str {
        match self {
            AccountError::Full => "account table is full",
            AccountError::Exists => "user already exists",
            AccountError::NotFound => "user not found",
            AccountError::EmptyField => "username/password cannot be empty",
        }
    }
}

struct AccountTable {
    slots: [Option<Account>; MAX_ACCOUNTS],
}

impl AccountTable {
    pub const fn new() -> Self {
        Self {
            slots: [None; MAX_ACCOUNTS],
        }
    }
}

static ACCOUNTS: Mutex<AccountTable> = Mutex::new(AccountTable::new());

/// Create a new user. Overly long values are truncated to the fixed buffer size.
pub fn add_user(username: &str, password: &str) -> Result<(), AccountError> {
    if username.is_empty() || password.is_empty() {
        return Err(AccountError::EmptyField);
    }

    let mut table = ACCOUNTS.lock();
    if table.slots.iter().any(|slot| slot.as_ref().map(|u| u.username()) == Some(username)) {
        return Err(AccountError::Exists);
    }

    if let Some(empty) = table.slots.iter_mut().find(|slot| slot.is_none()) {
        *empty = Some(Account::new(username, password));
        Ok(())
    } else {
        Err(AccountError::Full)
    }
}

pub fn delete_user(username: &str) -> Result<(), AccountError> {
    let mut table = ACCOUNTS.lock();
    if let Some(slot) = table
        .slots
        .iter_mut()
        .find(|slot| slot.as_ref().map(|u| u.username()) == Some(username))
    {
        *slot = None;
        Ok(())
    } else {
        Err(AccountError::NotFound)
    }
}

pub fn authenticate(username: &str, password: &str) -> bool {
    let table = ACCOUNTS.lock();
    table
        .slots
        .iter()
        .flatten()
        .any(|u| u.username() == username && u.password() == password)
}

/// Ensure a user exists, creating it if missing. Returns true if it exists or was created.
pub fn ensure_user(username: &str, password: &str) -> bool {
    match add_user(username, password) {
        Ok(()) => true,
        Err(AccountError::Exists) => true,
        Err(_) => false,
    }
}

/// Visit all usernames without allocating.
pub fn list_users<F: FnMut(&str)>(mut visitor: F) {
    let table = ACCOUNTS.lock();
    for user in table.slots.iter().flatten() {
        visitor(user.username());
    }
}

fn write_bytes(dest: &mut [u8], src: &str) {
    let bytes = src.as_bytes();
    let len = bytes.len().min(dest.len());
    dest[..len].copy_from_slice(&bytes[..len]);
    if len < dest.len() {
        dest[len..].fill(0);
    }
}

fn bytes_to_str(bytes: &[u8]) -> &str {
    let end = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
    // We only store ASCII, so utf8 conversion is safe. Fall back to empty on error.
    str::from_utf8(&bytes[..end]).unwrap_or("")
}
