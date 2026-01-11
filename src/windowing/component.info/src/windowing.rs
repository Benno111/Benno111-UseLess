use crate::framebuffer;
use crate::acpi;
use crate::crash_predictor;
use crate::vga_buffer;
use spin::Mutex;
use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use core::str;
use alloc::format;

mod apps;

#[derive(Clone, Copy)]
pub struct Window {
    pub title: &'static str,
    pub x: usize,
    pub y: usize,
    pub w: usize,
    pub h: usize,
}

pub const MAX_WINDOWS: usize = 4;
const MAX_ICONS: usize = 6;
const WORKSPACES: usize = 3;
const TASKBAR_H: usize = 22;
const MAX_USERS: usize = 4;
const NAME_MAX: usize = 16;

type WindowRender = fn(&Window);

#[derive(Clone, Copy)]
struct WindowEntry {
    window: Window,
    render: WindowRender,
    workspace: usize,
}

struct State {
    mouse: (usize, usize),
    last_render_mouse: (usize, usize),
    bounds: (usize, usize),
    login: LoginState,
    users: [Option<UserSlot>; MAX_USERS],
    active_user: usize,
    wm_mode: WindowManagerMode,
    pending_wm_start: bool,
    pending_wm_delay: usize,
    wm_sandboxed: bool,
    force_render_ticks: usize,
}

static STATE: Mutex<State> = Mutex::new(State {
    mouse: (20, 20),
    last_render_mouse: (usize::MAX, usize::MAX),
    bounds: (640, 480),
    login: LoginState::new(),
    users: [None; MAX_USERS],
    active_user: 0,
    wm_mode: WindowManagerMode::Login,
    pending_wm_start: false,
    pending_wm_delay: 0,
    wm_sandboxed: false,
    force_render_ticks: 0,
});
static RENDERING: AtomicBool = AtomicBool::new(false);
static FB_WARNED: AtomicBool = AtomicBool::new(false);
static RENDER_TICK: AtomicUsize = AtomicUsize::new(0);
static LAST_WM_MODE: AtomicUsize = AtomicUsize::new(usize::MAX);
static LAST_ACTIVE_USER: AtomicUsize = AtomicUsize::new(usize::MAX);

struct RenderGuard;

impl RenderGuard {
    fn new(flag: &'static AtomicBool) -> Option<Self> {
        if flag.swap(true, Ordering::SeqCst) {
            // Recover from a stuck guard to keep the render loop alive.
            vga_buffer::log_line("[windowing] render guard reset");
            flag.store(false, Ordering::SeqCst);
            flag.store(true, Ordering::SeqCst);
        }
        Some(Self)
    }
}

impl Drop for RenderGuard {
    fn drop(&mut self) {
        RENDERING.store(false, Ordering::SeqCst);
    }
}

struct RefreshGuard(bool);

impl RefreshGuard {
    fn new() -> Self {
        framebuffer::lock_refresh();
        Self(true)
    }
    fn release(&mut self) {
        if self.0 {
            framebuffer::unlock_refresh();
            self.0 = false;
        }
    }
}

impl Drop for RefreshGuard {
    fn drop(&mut self) {
        if self.0 {
            framebuffer::unlock_refresh();
            self.0 = false;
        }
    }
}

const CURSOR_W: usize = 20;
const CURSOR_H: usize = 20;
const CURSOR_SPRITE: [u8; CURSOR_W * CURSOR_H * 4] = generate_cursor_sprite();

#[derive(Clone, Copy, PartialEq, Eq)]
enum WindowManagerMode {
    Login,
    Desktop,
}

const LOGIN_MAX: usize = 16;

#[derive(Clone, Copy, PartialEq, Eq)]
enum LoginFocus {
    Username,
    Password,
}

#[derive(Clone, Copy)]
enum LoginStatus {
    None,
    Loading,
    Failed,
    Success,
}

#[derive(Clone, Copy)]
struct LoginState {
    username: [u8; LOGIN_MAX],
    ulen: usize,
    password: [u8; LOGIN_MAX],
    plen: usize,
    focus: LoginFocus,
    status: LoginStatus,
    window: Option<Window>,
}

impl LoginState {
    const fn new() -> Self {
        Self {
            username: [0; LOGIN_MAX],
            ulen: 0,
            password: [0; LOGIN_MAX],
            plen: 0,
            focus: LoginFocus::Username,
            status: LoginStatus::None,
            window: None,
        }
    }

    fn clear(&mut self) {
        self.username = [0; LOGIN_MAX];
        self.password = [0; LOGIN_MAX];
        self.ulen = 0;
        self.plen = 0;
        self.focus = LoginFocus::Username;
        self.status = LoginStatus::None;
        self.window = None;
    }
}

#[derive(Clone, Copy)]
struct DragState {
    active: bool,
    idx: usize,
    offset_x: isize,
    offset_y: isize,
}

impl DragState {
    const fn new() -> Self {
        Self {
            active: false,
            idx: 0,
            offset_x: 0,
            offset_y: 0,
        }
    }
}

pub fn init_default_windows() {
    {
        let mut st = STATE.lock();
        // Start sandbox for user (logical flag for now).
        st.wm_sandboxed = true;
        // Prep desktop state for the active user.
        if active_user_ref(&st).is_none() {
            let idx = st.active_user;
            st.users[idx] = Some(UserSlot::empty());
        }
        if let Some(user) = active_user_mut(&mut st) {
            user.desktop = DesktopState::new();
            init_desktop_icons_locked(&mut user.desktop);
        }
        // Prep taskbar/render cadence.
        st.force_render_ticks = 120;
        // Startup WM and activate.
        st.wm_mode = WindowManagerMode::Desktop;
        st.pending_wm_start = false;
        st.pending_wm_delay = 0;
        // Swap to the new WM and stop the old one.
        st.login.status = LoginStatus::None;
        st.login.window = None;
    }
    framebuffer::set_force_swap(true);
    framebuffer::set_desktop_mode(true);
    vga_buffer::log_line("[windowing] user sandbox started");
    vga_buffer::log_line("[windowing] desktop icons prepared");
    vga_buffer::log_line("[windowing] taskbar prepared");
    vga_buffer::log_line("[windowing] window manager started");
    vga_buffer::log_line("[windowing] window manager active");
    vga_buffer::log_line("[windowing] swapped to new window manager");
    vga_buffer::log_line("[windowing] old window manager stopped");
    framebuffer::invalidate();
    let _ = register_window(
        Window {
            title: "Console",
            x: 10,
            y: 30,
            w: 240,
            h: 120,
        },
        apps::render_console,
    );
    let _ = register_window(
        Window {
            title: "Apps",
            x: 270,
            y: 40,
            w: 220,
            h: 120,
        },
        apps::render_apps,
    );
    let _ = register_window(
        Window {
            title: "Logs",
            x: 60,
            y: 170,
            w: 360,
            h: 120,
        },
        apps::render_logs,
    );
    let _ = register_window(
        Window {
            title: "Task Manager",
            x: 420,
            y: 40,
            w: 260,
            h: 180,
        },
        apps::render_task_manager,
    );
    framebuffer::invalidate();
}

pub fn init_login_window() {
    let mut st = STATE.lock();
    st.login.clear();
    st.wm_mode = WindowManagerMode::Login;
    st.pending_wm_start = false;
    st.pending_wm_delay = 0;
    st.wm_sandboxed = false;
    st.force_render_ticks = 0;
    framebuffer::set_desktop_mode(false);
    framebuffer::set_renderer(render_login_only);
    let (bw, bh) = st.bounds;
    let w = 360usize;
    let h = 200usize;
    let x = bw.saturating_sub(w) / 2;
    let y = bh.saturating_sub(h) / 2;
    st.login.window = Some(Window {
        title: "Login",
        x,
        y,
        w,
        h,
    });
    vga_buffer::log_line("[windowing] window manager stopped (login)");
    framebuffer::set_text_opacity(210);
    framebuffer::set_force_swap(false);
    framebuffer::invalidate();
}

fn init_desktop_icons() {
    let mut st = STATE.lock();
    if let Some(user) = active_user_mut(&mut st) {
        init_desktop_icons_locked(&mut user.desktop);
    }
}

fn init_desktop_icons_locked(desktop: &mut DesktopState) {
    desktop.icons = [None; MAX_ICONS];
    let icons = [
        DesktopIcon {
            label: "Console",
            w: 48,
            h: 48,
            render: apps::render_console,
        },
        DesktopIcon {
            label: "Apps",
            w: 48,
            h: 48,
            render: apps::render_apps,
        },
        DesktopIcon {
            label: "Logs",
            w: 48,
            h: 48,
            render: apps::render_logs,
        },
        DesktopIcon {
            label: "Tasks",
            w: 48,
            h: 48,
            render: apps::render_task_manager,
        },
    ];
    for (idx, icon) in icons.into_iter().enumerate() {
        if idx < MAX_ICONS {
            desktop.icons[idx] = Some(icon);
        }
    }
}

/// Replace the current window list with a tiled set derived from app names.
#[allow(dead_code)]
pub fn set_app_windows(names: &[&'static str]) {
    clear_windows();
    const FLOAT_POS: &[(usize, usize)] = &[(40, 50), (220, 80), (120, 220), (300, 180)];
    for (i, name) in names.iter().take(MAX_WINDOWS).enumerate() {
        let (x, y) = FLOAT_POS.get(i).copied().unwrap_or((30 + i * 40, 40 + i * 30));
        let _ = register_window(
            Window {
                title: name,
                x,
                y,
                w: 220,
                h: 140,
            },
            render_placeholder,
        );
    }
    framebuffer::invalidate();
}

#[allow(dead_code)]
pub fn set_mouse_position(x: usize, y: usize) {
    let mut st = STATE.lock();
    let (w, h) = st.bounds;
    st.mouse = (x.min(w.saturating_sub(1)), y.min(h.saturating_sub(1)));
    framebuffer::invalidate();
}

pub fn move_mouse(dx: isize, dy: isize) {
    let mut st = STATE.lock();
    let (cur_x, cur_y) = st.mouse;
    let (bw, bh) = st.bounds;
    let new_x = clamp(cur_x as isize + dx, 0, bw.saturating_sub(1) as isize) as usize;
    let new_y = clamp(cur_y as isize + dy, 0, bh.saturating_sub(1) as isize) as usize;
    st.mouse = (new_x, new_y);
    if st.wm_mode == WindowManagerMode::Desktop {
        if let Some(user) = active_user_mut(&mut st) {
            if user.desktop.drag.active {
                let drag_idx = user.desktop.drag.idx;
                let off_x = user.desktop.drag.offset_x;
                let off_y = user.desktop.drag.offset_y;
                if let Some(entry) = user.desktop.windows.get_mut(drag_idx).and_then(|w| w.as_mut())
                {
                    let win = &mut entry.window;
                    let new_win_x =
                        clamp(new_x as isize + off_x, 0, bw.saturating_sub(win.w) as isize);
                    let new_win_y =
                        clamp(new_y as isize + off_y, 0, bh.saturating_sub(win.h) as isize);
                    win.x = new_win_x as usize;
                    win.y = new_win_y as usize;
                }
            }
        }
    }
    framebuffer::invalidate();
}

pub fn mouse_click_left() {
    let (mx, my, wm_mode) = {
        let st = STATE.lock();
        (st.mouse.0, st.mouse.1, st.wm_mode)
    };
    if wm_mode == WindowManagerMode::Login {
        vga_buffer::log_line("[windowing] login click");
        let _ = handle_login_click(mx, my);
        return;
    }
    if handle_desktop_click(mx, my) {
        return;
    }
    if handle_window_mouse_down(mx, my) {
        return;
    }
    framebuffer::invalidate();
}

pub fn mouse_click_right() {
    framebuffer::invalidate();
}

pub fn mouse_release_left() {
    let mut st = STATE.lock();
    if st.wm_mode == WindowManagerMode::Desktop {
        if let Some(user) = active_user_mut(&mut st) {
            user.desktop.drag.active = false;
        }
    }
    framebuffer::invalidate();
}

pub fn set_bounds(w: usize, h: usize) {
    STATE.lock().bounds = (w, h);
    framebuffer::invalidate();
}

/// Move a window by index by the given delta (floating manager behavior).
#[allow(dead_code)]
pub fn move_window(idx: usize, dx: isize, dy: isize) {
    let mut st = STATE.lock();
    let (bw, bh) = st.bounds;
    if let Some(user) = active_user_mut(&mut st) {
        if let Some(entry) = user.desktop.windows.get_mut(idx).and_then(|w| w.as_mut()) {
            let win = &mut entry.window;
            let new_x = clamp(win.x as isize + dx, 0, bw.saturating_sub(win.w) as isize);
            let new_y = clamp(win.y as isize + dy, 0, bh.saturating_sub(win.h) as isize);
            win.x = new_x as usize;
            win.y = new_y as usize;
            framebuffer::invalidate();
        }
    }
}

fn clamp(v: isize, min: isize, max: isize) -> isize {
    if v < min {
        min
    } else if v > max {
        max
    } else {
        v
    }
}

pub fn render() {
    let Some(_render_guard) = RenderGuard::new(&RENDERING) else {
        return;
    };
    let tick = RENDER_TICK.fetch_add(1, Ordering::Relaxed) + 1;
    if tick % 60 == 0 && desktop_active() {
        vga_buffer::log_line(&format!("[windowing] render tick {}", tick));
    }
    let start_desktop = {
        let mut st = STATE.lock();
        if st.pending_wm_start {
            if st.pending_wm_delay > 0 {
                st.pending_wm_delay = st.pending_wm_delay.saturating_sub(1);
                false
            } else {
                st.pending_wm_start = false;
                st.wm_mode = WindowManagerMode::Desktop;
                vga_buffer::log_line("[windowing] pending WM start: switching to desktop");
                true
            }
        } else {
            false
        }
    };
    if start_desktop {
        init_default_windows();
    }
    {
        let mut st = STATE.lock();
        if st.wm_mode == WindowManagerMode::Desktop && active_user_ref(&st).is_none() {
            let idx = st.active_user;
            st.users[idx] = Some(UserSlot::empty());
            if let Some(user) = active_user_mut(&mut st) {
                user.desktop = DesktopState::new();
                init_desktop_icons_locked(&mut user.desktop);
            }
            st.force_render_ticks = 120;
            framebuffer::set_force_swap(true);
            framebuffer::set_desktop_mode(true);
            framebuffer::invalidate();
            vga_buffer::log_line("[windowing] desktop state recovered");
        }
    }
    let (mouse, last_mouse) = {
        let st = STATE.lock();
        (st.mouse, st.last_render_mouse)
    };
    if !framebuffer::framebuffer_available() {
        if !FB_WARNED.swap(true, Ordering::SeqCst) {
            vga_buffer::log_line("[windowing] framebuffer unavailable; redraw skipped");
        }
        crash_predictor::note_framebuffer_unavailable();
        return;
    }
    framebuffer::invalidate();
    framebuffer::draw_desktop();
    let (mx, my, wm_mode, pending_start, active_user_id) = {
        let st = STATE.lock();
        (st.mouse.0, st.mouse.1, st.wm_mode, st.pending_wm_start, st.active_user)
    };
    let mode_id = match wm_mode {
        WindowManagerMode::Desktop => 1usize,
        WindowManagerMode::Login => 2usize,
    };
    let last_mode = LAST_WM_MODE.swap(mode_id, Ordering::SeqCst);
    if last_mode != mode_id {
        vga_buffer::log_line(&format!(
            "[windowing] renderer mode change: {}",
            if mode_id == 1 { "Desktop" } else { "Login" }
        ));
    }
    if mode_id == 1 || pending_start {
        let last_user = LAST_ACTIVE_USER.swap(active_user_id, Ordering::SeqCst);
        if last_user != active_user_id {
            vga_buffer::log_line(&format!(
                "[windowing] renderer target WM user slot: {}",
                active_user_id
            ));
        }
    } else {
        let last_user = LAST_ACTIVE_USER.swap(usize::MAX, Ordering::SeqCst);
        if last_user != usize::MAX {
            vga_buffer::log_line("[windowing] renderer target WM: login");
        }
    }
    render_animated_circle(tick);
    match wm_mode {
        WindowManagerMode::Desktop => render_desktop_path(tick, mx, my),
        WindowManagerMode::Login => render_login_path(tick, mx, my),
    }
    framebuffer::render_frame();
    {
        let mut st = STATE.lock();
        st.last_render_mouse = (mx, my);
        if st.force_render_ticks > 0 {
            st.force_render_ticks = st.force_render_ticks.saturating_sub(1);
            if st.force_render_ticks == 0 {
                framebuffer::set_force_swap(false);
            }
        }
    }
}

pub fn render_login_only() {
    let Some(_render_guard) = RenderGuard::new(&RENDERING) else {
        return;
    };
    let tick = RENDER_TICK.fetch_add(1, Ordering::Relaxed) + 1;
    let (mx, my) = {
        let st = STATE.lock();
        (st.mouse.0, st.mouse.1)
    };
    if !framebuffer::framebuffer_available() {
        if !FB_WARNED.swap(true, Ordering::SeqCst) {
            vga_buffer::log_line("[windowing] framebuffer unavailable; redraw skipped");
        }
        crash_predictor::note_framebuffer_unavailable();
        return;
    }
    framebuffer::invalidate();
    framebuffer::draw_desktop();
    render_animated_circle(tick);
    render_login_path(tick, mx, my);
    framebuffer::render_frame();
    {
        let mut st = STATE.lock();
        st.last_render_mouse = (mx, my);
    }
}

fn render_desktop_path(tick: usize, mx: usize, my: usize) {
    let (windows_snapshot, active_ws, icons_snapshot, bounds, first_frame) = {
        let st = STATE.lock();
        if let Some(user) = active_user_ref(&st) {
            (
                user.desktop.windows,
                user.desktop.active_workspace,
                user.desktop.icons,
                st.bounds,
                !user.desktop.desktop_rendered_once,
            )
        } else {
            vga_buffer::log_line("[windowing] render error: no active user (dead WM instance)");
            (
                [None; MAX_WINDOWS],
                0,
                [None; MAX_ICONS],
                st.bounds,
                false,
            )
        }
    };
    render_taskbar(&windows_snapshot, active_ws, bounds);
    render_icons(&icons_snapshot);
    render_spinner(tick);
    if first_frame {
        let mut st = STATE.lock();
        if let Some(user) = active_user_mut(&mut st) {
            if !user.desktop.desktop_rendered_once {
                user.desktop.desktop_rendered_once = true;
                vga_buffer::log_line("[windowing] desktop frame rendered");
            }
        }
    }
    for entry in windows_snapshot.iter().flatten() {
        if entry.workspace != active_ws {
            continue;
        }
        render_window(&entry.window, entry.render);
    }
    draw_cursor(mx, my);
}

fn render_login_path(tick: usize, mx: usize, my: usize) {
    if let Some(win) = login_window() {
        render_window(&win, render_login);
    } else {
        vga_buffer::log_line("[windowing] render error: login window missing");
    }
    render_spinner(tick);
    draw_cursor(mx, my);
}

fn draw_cursor(x: usize, y: usize) {
    if desktop_active() {
        framebuffer::draw_cursor(x, y, (255, 255, 255));
    } else {
        framebuffer::blit_rgba(x, y, CURSOR_W, CURSOR_H, &CURSOR_SPRITE);
    }
}

fn render_spinner(tick: usize) {
    const CENTER_X: usize = 18;
    const CENTER_Y: usize = 18;
    const DOT_SIZE: usize = 3;
    const OFFSETS: [(isize, isize); 8] = [
        (0, -8),
        (6, -6),
        (8, 0),
        (6, 6),
        (0, 8),
        (-6, 6),
        (-8, 0),
        (-6, -6),
    ];
    let phase = (tick / 4) % OFFSETS.len();
    for (idx, (dx, dy)) in OFFSETS.iter().enumerate() {
        let x = (CENTER_X as isize + dx) as usize;
        let y = (CENTER_Y as isize + dy) as usize;
        let color = if idx == phase {
            (220, 230, 255)
        } else {
            (70, 78, 92)
        };
        framebuffer::draw_rect(x, y, DOT_SIZE, DOT_SIZE, color);
    }
}

fn render_animated_circle(tick: usize) {
    const CENTER_X: isize = 36;
    const CENTER_Y: isize = 36;
    const POINTS: [(isize, isize); 12] = [
        (0, -12),
        (6, -10),
        (10, -6),
        (12, 0),
        (10, 6),
        (6, 10),
        (0, 12),
        (-6, 10),
        (-10, 6),
        (-12, 0),
        (-10, -6),
        (-6, -10),
    ];
    let phase = (tick / 3) % POINTS.len();
    for (idx, (dx, dy)) in POINTS.iter().enumerate() {
        let x = (CENTER_X + dx) as usize;
        let y = (CENTER_Y + dy) as usize;
        let color = if idx == phase {
            (255, 220, 120)
        } else {
            (90, 96, 108)
        };
        framebuffer::draw_rect(x, y, 2, 2, color);
    }
}

// ===== Window app API =====

/// Clear all windows.
pub fn clear_windows() {
    let mut st = STATE.lock();
    if let Some(user) = active_user_mut(&mut st) {
        user.desktop.windows = [None; MAX_WINDOWS];
    }
}

/// Register a window with a render callback. Returns its slot index if inserted.
pub fn register_window(win: Window, render: WindowRender) -> Option<usize> {
    let mut st = STATE.lock();
    let workspace = active_user_ref(&st)
        .map(|user| user.desktop.active_workspace)
        .unwrap_or(0);
    register_window_in_workspace(&mut st, win, render, workspace)
}

fn register_window_in_workspace(
    st: &mut State,
    win: Window,
    render: WindowRender,
    workspace: usize,
) -> Option<usize> {
    if let Some(user) = active_user_mut(st) {
        return register_window_in_desktop(&mut user.desktop, win, render, workspace);
    }
    None
}

fn render_login(win: &Window) {
    let inner_x = win.x + 12;
    let mut y = win.y + 34;
    let (focus, ulen, plen, username, password, status) = {
        let st = STATE.lock();
        (
            st.login.focus,
            st.login.ulen,
            st.login.plen,
            st.login.username,
            st.login.password,
            st.login.status,
        )
    };
    framebuffer::draw_text("Welcome back", inner_x, y);
    y += 18;
    framebuffer::draw_text("Username", inner_x, y);
    y += 12;
    let user_focused = matches!(focus, LoginFocus::Username);
    draw_login_field(inner_x, y, win.w - 24, 18, user_focused);
    draw_login_text(inner_x + 6, y + 4, &username, ulen, false);
    y += 28;
    framebuffer::draw_text("Password", inner_x, y);
    y += 12;
    let pass_focused = matches!(focus, LoginFocus::Password);
    draw_login_field(inner_x, y, win.w - 24, 18, pass_focused);
    draw_login_text(inner_x + 6, y + 4, &password, plen, true);
    y += 30;
    framebuffer::draw_rect(inner_x, y, 120, 22, (82, 156, 255));
    framebuffer::draw_text("Sign In", inner_x + 16, y + 4);
    framebuffer::draw_rect(inner_x + 130, y, 96, 22, (60, 66, 78));
    framebuffer::draw_text("Reboot", inner_x + 146, y + 4);
    framebuffer::draw_rect(inner_x + 232, y, 96, 22, (60, 66, 78));
    framebuffer::draw_text("Shutdown", inner_x + 238, y + 4);
    match status {
        LoginStatus::None => {
            framebuffer::draw_text("Hint: admin / pass123", inner_x, win.y + win.h - 18);
        }
        LoginStatus::Loading => {
            framebuffer::draw_text("Loading desktop...", inner_x, win.y + win.h - 18);
        }
        LoginStatus::Failed => {
            framebuffer::draw_text("Invalid credentials", inner_x, win.y + win.h - 18);
        }
        LoginStatus::Success => {
            framebuffer::draw_text("Login successful", inner_x, win.y + win.h - 18);
        }
    }
}

fn render_placeholder(win: &Window) {
    framebuffer::draw_text(win.title, win.x + 8, win.y + 28);
}

fn render_window(win: &Window, render: WindowRender) {
    framebuffer::draw_rect(win.x, win.y, win.w, win.h, (50, 120, 200));
    framebuffer::draw_rect(win.x + 2, win.y + 16, win.w - 4, win.h - 18, (20, 20, 20));
    framebuffer::draw_text(win.title, win.x + 6, win.y + 2);
    render(win);
}

fn render_taskbar(
    windows: &[Option<WindowEntry>; MAX_WINDOWS],
    active_ws: usize,
    bounds: (usize, usize),
) {
    let (bw, bh) = bounds;
    let bar_y = bh.saturating_sub(TASKBAR_H);
    framebuffer::draw_rect(0, bar_y, bw, TASKBAR_H, (26, 30, 38));
    let mut x = 6usize;
    for ws in 0..WORKSPACES {
        let active = ws == active_ws;
        let color = if active { (82, 156, 255) } else { (60, 66, 78) };
        framebuffer::draw_rect(x, bar_y + 3, 36, 16, color);
        framebuffer::draw_text(&format!("W{}", ws + 1), x + 8, bar_y + 6);
        x += 42;
    }
    for entry in windows.iter().flatten().filter(|e| e.workspace == active_ws) {
        if x + 80 >= bw {
            break;
        }
        framebuffer::draw_rect(x, bar_y + 3, 80, 16, (45, 48, 56));
        framebuffer::draw_text(entry.window.title, x + 6, bar_y + 6);
        x += 86;
    }
}

fn render_icons(icons: &[Option<DesktopIcon>; MAX_ICONS]) {
    let mut idx = 0usize;
    for icon in icons.iter().flatten() {
        let rect = icon_rect(idx);
        framebuffer::draw_rect(rect.x, rect.y, rect.w, rect.h, (40, 44, 54));
        framebuffer::draw_text(icon.label, rect.x + 6, rect.y + rect.h + 4);
        idx += 1;
    }
}

fn icon_rect(idx: usize) -> Rect {
    let col = idx % 3;
    let row = idx / 3;
    let x = 16 + col * 80;
    let y = 24 + row * 80;
    Rect {
        x,
        y,
        w: 48,
        h: 48,
    }
}

fn open_window_for_icon(desktop: &mut DesktopState, bounds: (usize, usize), icon: DesktopIcon) {
    if bring_to_front_by_title(&mut desktop.windows, icon.label, desktop.active_workspace) {
        framebuffer::invalidate();
        return;
    }
    let (bw, bh) = bounds;
    let offset = (desktop
        .windows
        .iter()
        .flatten()
        .filter(|e| e.workspace == desktop.active_workspace)
        .count()
        * 24) as isize;
    let w = 280usize;
    let h = 160usize;
    let x = clamp(40 + offset, 0, bw.saturating_sub(w) as isize) as usize;
    let y = clamp(50 + offset, 0, bh.saturating_sub(h + TASKBAR_H) as isize) as usize;
    let _ = register_window_in_desktop(
        desktop,
        Window {
            title: icon.label,
            x,
            y,
            w,
            h,
        },
        icon.render,
        desktop.active_workspace,
    );
}

fn register_window_in_desktop(
    desktop: &mut DesktopState,
    win: Window,
    render: WindowRender,
    workspace: usize,
) -> Option<usize> {
    if let Some((idx, slot)) = desktop
        .windows
        .iter_mut()
        .enumerate()
        .find(|(_, s)| s.is_none())
    {
        *slot = Some(WindowEntry {
            window: win,
            render,
            workspace: workspace.min(WORKSPACES.saturating_sub(1)),
        });
        framebuffer::invalidate();
        return Some(idx);
    }
    None
}

fn bring_to_front_by_title(
    windows: &mut [Option<WindowEntry>; MAX_WINDOWS],
    title: &'static str,
    workspace: usize,
) -> bool {
    let mut found_idx = None;
    for (idx, entry) in windows.iter().enumerate() {
        if let Some(w) = entry.as_ref() {
            if w.workspace == workspace && w.window.title == title {
                found_idx = Some(idx);
                break;
            }
        }
    }
    if let Some(idx) = found_idx {
        bring_to_front(windows, idx);
        true
    } else {
        false
    }
}

fn draw_login_field(x: usize, y: usize, w: usize, h: usize, focused: bool) {
    let border = if focused { (82, 156, 255) } else { (45, 48, 56) };
    framebuffer::draw_rect(x, y, w, h, border);
}

fn draw_login_text(x: usize, y: usize, buf: &[u8; LOGIN_MAX], len: usize, mask: bool) {
    if len == 0 {
        return;
    }
    let mut tmp = [0u8; LOGIN_MAX];
    let out = if mask {
        for i in 0..len.min(LOGIN_MAX) {
            tmp[i] = b'*';
        }
        &tmp[..len.min(LOGIN_MAX)]
    } else {
        &buf[..len.min(LOGIN_MAX)]
    };
    if let Ok(s) = str::from_utf8(out) {
        framebuffer::draw_text(s, x, y);
    }
}

fn handle_login_click(x: usize, y: usize) -> bool {
    let login_win = login_window();
    let Some(win) = login_win else {
        return false;
    };
    let layout = login_layout(&win);
    let mut st = STATE.lock();
    if layout.user.contains(x, y) {
        st.login.focus = LoginFocus::Username;
        st.login.status = LoginStatus::None;
        vga_buffer::log_line("[login] focus username");
        framebuffer::invalidate();
        return true;
    }
    if layout.pass.contains(x, y) {
        st.login.focus = LoginFocus::Password;
        st.login.status = LoginStatus::None;
        vga_buffer::log_line("[login] focus password");
        framebuffer::invalidate();
        return true;
    }
    if layout.button.contains(x, y) {
        drop(st);
        vga_buffer::log_line("[login] sign in click");
        submit_login();
        return true;
    }
    if layout.reboot.contains(x, y) {
        drop(st);
        vga_buffer::log_line("[login] reboot click");
        acpi::reboot();
    }
    if layout.shutdown.contains(x, y) {
        drop(st);
        vga_buffer::log_line("[login] shutdown click");
        acpi::power_off();
    }
    false
}

fn handle_desktop_click(x: usize, y: usize) -> bool {
    let mut st = STATE.lock();
    let (bw, bh) = st.bounds;
    let bar_y = bh.saturating_sub(TASKBAR_H);
    if y >= bar_y {
        let mut btn_x = 6usize;
        for ws in 0..WORKSPACES {
            let rect = Rect {
                x: btn_x,
                y: bar_y + 3,
                w: 36,
                h: 16,
            };
            if rect.contains(x, y) {
                if let Some(user) = active_user_mut(&mut st) {
                    user.desktop.active_workspace = ws;
                    user.desktop.drag.active = false;
                }
                vga_buffer::log_line(&format!("[windowing] workspace switched to {}", ws + 1));
                framebuffer::invalidate();
                return true;
            }
            btn_x += 42;
        }
        let active_ws = active_user_ref(&st)
            .map(|u| u.desktop.active_workspace)
            .unwrap_or(0);
        if let Some(user) = active_user_mut(&mut st) {
            for entry in user
                .desktop
                .windows
                .iter()
                .flatten()
                .filter(|e| e.workspace == active_ws)
            {
                let rect = Rect {
                    x: btn_x,
                    y: bar_y + 3,
                    w: 80,
                    h: 16,
                };
                if rect.contains(x, y) {
                    let title = entry.window.title;
                    bring_to_front_by_title(&mut user.desktop.windows, title, active_ws);
                    user.desktop.drag.active = false;
                    framebuffer::invalidate();
                    return true;
                }
                btn_x += 86;
                if btn_x >= bw {
                    break;
                }
            }
        }
        return true;
    }

    let bounds = st.bounds;
    if let Some(user) = active_user_mut(&mut st) {
        let mut hit_icon = None;
        for (idx, icon) in user.desktop.icons.iter().flatten().enumerate() {
            let rect = icon_rect(idx);
            if rect.contains(x, y) {
                hit_icon = Some(*icon);
                break;
            }
        }
        if let Some(icon) = hit_icon {
            open_window_for_icon(&mut user.desktop, bounds, icon);
            user.desktop.drag.active = false;
            return true;
        }
    }
    false
}

pub fn handle_key_input(ch: char) -> bool {
    let Some(_win) = login_window() else {
        return false;
    };
    let mut st = STATE.lock();
    match ch {
        '\n' => {
            drop(st);
            vga_buffer::log_line("[login] submit key");
            submit_login();
            return true;
        }
        '\t' => {
            st.login.focus = match st.login.focus {
                LoginFocus::Username => LoginFocus::Password,
                LoginFocus::Password => LoginFocus::Username,
            };
            st.login.status = LoginStatus::None;
            vga_buffer::log_line("[login] focus toggle");
            framebuffer::invalidate();
            return true;
        }
        '\u{8}' => {
            match st.login.focus {
                LoginFocus::Username => {
                    if st.login.ulen > 0 {
                        st.login.ulen -= 1;
                        let idx = st.login.ulen;
                        st.login.username[idx] = 0;
                    }
                }
                LoginFocus::Password => {
                    if st.login.plen > 0 {
                        st.login.plen -= 1;
                        let idx = st.login.plen;
                        st.login.password[idx] = 0;
                    }
                }
            }
            st.login.status = LoginStatus::None;
            vga_buffer::log_line("[login] backspace");
            framebuffer::invalidate();
            return true;
        }
        _ => {}
    }

    if !ch.is_ascii() || (!ch.is_ascii_graphic() && ch != ' ') {
        return true;
    }

    match st.login.focus {
        LoginFocus::Username => {
            if st.login.ulen < LOGIN_MAX {
                let idx = st.login.ulen;
                st.login.username[idx] = ch as u8;
                st.login.ulen = idx + 1;
            }
        }
        LoginFocus::Password => {
            if st.login.plen < LOGIN_MAX {
                let idx = st.login.plen;
                st.login.password[idx] = ch as u8;
                st.login.plen = idx + 1;
            }
        }
    }
    st.login.status = LoginStatus::None;
    vga_buffer::log_line("[login] key input");
    framebuffer::invalidate();
    true
}

pub fn desktop_active() -> bool {
    let st = STATE.lock();
    st.wm_mode == WindowManagerMode::Desktop
}

fn submit_login() {
    let (uname_buf, ulen, pass_buf, plen) = {
        let st = STATE.lock();
        (st.login.username, st.login.ulen, st.login.password, st.login.plen)
    };
    let username = str::from_utf8(&uname_buf[..ulen]).unwrap_or("");
    let password = str::from_utf8(&pass_buf[..plen]).unwrap_or("");
    vga_buffer::log_line("[login] submit");
    let start_desktop = if crate::accounts::authenticate(username, password) {
        let mut st = STATE.lock();
        st.login.status = LoginStatus::Loading;
        st.pending_wm_start = true;
        st.pending_wm_delay = 0;
        st.force_render_ticks = 120;
        framebuffer::set_force_swap(true);
        framebuffer::set_text_opacity(210);
        let user_idx = ensure_user_slot(&mut st, username);
        st.active_user = user_idx;
        if st.users[user_idx].is_none() {
            st.users[user_idx] = Some(UserSlot::empty());
        }
        framebuffer::set_renderer(render);
        true
    } else {
        let mut st = STATE.lock();
        st.login.status = LoginStatus::Failed;
        framebuffer::invalidate();
        vga_buffer::log_line("[login] invalid credentials");
        false
    };
    if start_desktop {
        vga_buffer::log_line("[login] authenticated");
        vga_buffer::log_line("[login] switching to desktop");
        framebuffer::set_force_swap(true);
        framebuffer::set_desktop_mode(true);
        framebuffer::invalidate();
    }
}

fn ensure_user_slot(st: &mut State, username: &str) -> usize {
    let name_bytes = username.as_bytes();
    for (idx, slot) in st.users.iter().enumerate() {
        if let Some(user) = slot.as_ref() {
            if user.len == name_bytes.len().min(NAME_MAX)
                && user.name[..user.len] == name_bytes[..user.len]
            {
                return idx;
            }
        }
    }
    if let Some((idx, slot)) = st.users.iter_mut().enumerate().find(|(_, s)| s.is_none()) {
        let mut user = UserSlot::empty();
        let len = name_bytes.len().min(NAME_MAX);
        user.name[..len].copy_from_slice(&name_bytes[..len]);
        user.len = len;
        *slot = Some(user);
        return idx;
    }
    0
}

#[derive(Clone, Copy)]
struct LoginLayout {
    user: Rect,
    pass: Rect,
    button: Rect,
    reboot: Rect,
    shutdown: Rect,
}

#[derive(Clone, Copy)]
struct Rect {
    x: usize,
    y: usize,
    w: usize,
    h: usize,
}

impl Rect {
    fn contains(&self, px: usize, py: usize) -> bool {
        px >= self.x && px < self.x + self.w && py >= self.y && py < self.y + self.h
    }
}

fn login_window() -> Option<Window> {
    let st = STATE.lock();
    st.login.window
}

fn active_user_mut(st: &mut State) -> Option<&mut UserSlot> {
    let idx = st.active_user;
    st.users.get_mut(idx).and_then(|u| u.as_mut())
}

fn active_user_ref(st: &State) -> Option<&UserSlot> {
    let idx = st.active_user;
    st.users.get(idx).and_then(|u| u.as_ref())
}

fn login_layout(win: &Window) -> LoginLayout {
    let inner_x = win.x + 12;
    let mut y = win.y + 34;
    y += 18; // welcome
    y += 12; // "Username"
    let user = Rect {
        x: inner_x,
        y,
        w: win.w - 24,
        h: 18,
    };
    y += 28; // field + spacing
    y += 12; // "Password"
    let pass = Rect {
        x: inner_x,
        y,
        w: win.w - 24,
        h: 18,
    };
    y += 30;
    let button = Rect {
        x: inner_x,
        y,
        w: 120,
        h: 22,
    };
    let reboot = Rect {
        x: inner_x + 130,
        y,
        w: 96,
        h: 22,
    };
    let shutdown = Rect {
        x: inner_x + 232,
        y,
        w: 96,
        h: 22,
    };
    LoginLayout {
        user,
        pass,
        button,
        reboot,
        shutdown,
    }
}

fn handle_window_mouse_down(x: usize, y: usize) -> bool {
    let mut st = STATE.lock();
    if st.wm_mode != WindowManagerMode::Desktop {
        return false;
    }
    let active_ws = active_user_ref(&st)
        .map(|u| u.desktop.active_workspace)
        .unwrap_or(0);
    let Some(user) = active_user_mut(&mut st) else {
        return false;
    };
    let Some(idx) = window_at(&user.desktop.windows, active_ws, x, y) else {
        return false;
    };
    bring_to_front(&mut user.desktop.windows, idx);
    let top_idx = top_window_index(&user.desktop.windows).unwrap_or(idx);
    let (win_x, win_y, in_title) = user
        .desktop
        .windows
        .get(top_idx)
        .and_then(|w| w.as_ref())
        .map(|entry| {
            (
                entry.window.x,
                entry.window.y,
                in_title_bar(&entry.window, x, y),
            )
        })
        .unwrap_or((0, 0, false));
    if in_title {
        user.desktop.drag.active = true;
        user.desktop.drag.idx = top_idx;
        user.desktop.drag.offset_x = win_x as isize - x as isize;
        user.desktop.drag.offset_y = win_y as isize - y as isize;
    }
    framebuffer::invalidate();
    true
}

fn in_title_bar(win: &Window, x: usize, y: usize) -> bool {
    x >= win.x && x < win.x + win.w && y >= win.y && y < win.y + 16
}

fn window_at(
    windows: &[Option<WindowEntry>; MAX_WINDOWS],
    workspace: usize,
    x: usize,
    y: usize,
) -> Option<usize> {
    let mut found = None;
    for (idx, entry) in windows.iter().enumerate() {
        if let Some(w) = entry.as_ref().map(|e| e.window) {
            if entry.as_ref().map(|e| e.workspace) != Some(workspace) {
                continue;
            }
            if x >= w.x && x < w.x + w.w && y >= w.y && y < w.y + w.h {
                found = Some(idx);
            }
        }
    }
    found
}

fn bring_to_front(windows: &mut [Option<WindowEntry>; MAX_WINDOWS], idx: usize) {
    if idx >= MAX_WINDOWS {
        return;
    }
    let entry = windows[idx];
    if entry.is_none() {
        return;
    }
    for i in idx..MAX_WINDOWS - 1 {
        windows[i] = windows[i + 1];
    }
    windows[MAX_WINDOWS - 1] = entry;
}

fn top_window_index(windows: &[Option<WindowEntry>; MAX_WINDOWS]) -> Option<usize> {
    for i in (0..MAX_WINDOWS).rev() {
        if windows[i].is_some() {
            return Some(i);
        }
    }
    None
}

const fn generate_cursor_sprite() -> [u8; CURSOR_W * CURSOR_H * 4] {
    let mut data = [0u8; CURSOR_W * CURSOR_H * 4];
    let mut y = 0;
    while y < CURSOR_H {
        let mut x = 0;
        while x < CURSOR_W {
            let idx = (y * CURSOR_W + x) * 4;
            // Simple arrow with a faint shadow.
            let shadow = x + 2 <= y + 2 && x + 2 >= y.saturating_sub(6) && y + 2 < CURSOR_H;
            let filled = x <= y && x >= y.saturating_sub(6);
            let border = x == y || x + 1 == y;
            if border {
                data[idx] = 0;
                data[idx + 1] = 0;
                data[idx + 2] = 0;
                data[idx + 3] = 255;
            } else if filled {
                data[idx] = 245;
                data[idx + 1] = 245;
                data[idx + 2] = 245;
                data[idx + 3] = 255;
            } else if shadow {
                data[idx] = 0;
                data[idx + 1] = 0;
                data[idx + 2] = 0;
                data[idx + 3] = 90;
            } else {
                data[idx + 3] = 0;
            }
            x += 1;
        }
        y += 1;
    }
    data
}
#[derive(Clone, Copy)]
struct DesktopIcon {
    label: &'static str,
    w: usize,
    h: usize,
    render: WindowRender,
}

#[derive(Clone, Copy)]
struct DesktopState {
    windows: [Option<WindowEntry>; MAX_WINDOWS],
    drag: DragState,
    active_workspace: usize,
    icons: [Option<DesktopIcon>; MAX_ICONS],
    desktop_rendered_once: bool,
}

impl DesktopState {
    const fn new() -> Self {
        Self {
            windows: [None; MAX_WINDOWS],
            drag: DragState::new(),
            active_workspace: 0,
            icons: [None; MAX_ICONS],
            desktop_rendered_once: false,
        }
    }
}

#[derive(Clone, Copy)]
struct UserSlot {
    name: [u8; NAME_MAX],
    len: usize,
    desktop: DesktopState,
}

impl UserSlot {
    const fn empty() -> Self {
        Self {
            name: [0; NAME_MAX],
            len: 0,
            desktop: DesktopState::new(),
        }
    }
}
