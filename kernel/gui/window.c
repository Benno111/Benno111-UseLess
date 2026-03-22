/*
 * OS8 - GUI Windowing System
 *
 * Complete window manager with compositor and widget toolkit.
 */

#include "build_uuid.h"
#include "arch/arch.h"
#include "desktop.h"         /* Desktop manager */
#include "drivers/pci.h"
#include "dock_icons.h"      /* Dock icons (PNG-based) */
#include "drivers/uart.h"
#include "fs/vfs.h"          /* VFS headers */
#include "icons.h"           /* Icon bitmaps */
#include "drivers/storage.h"
#include "media/media.h"
#include "mm/kmalloc.h"
#include "printk.h"
#include "../../shared/password_hash.h"
#include "toolbar_icons.h" /* Toolbar icons for image viewer */
#include "types.h"

struct window *gui_create_file_manager(int x, int y);
struct window *gui_create_file_manager_path(int x, int y, const char *path);
void gui_open_notepad(const char *path);
int gui_launch_app_by_id(const char *app_id);
extern int bochs_init(uint32_t width, uint32_t height);
extern void bochs_get_info(uint32_t **buffer, uint32_t *width, uint32_t *height);

/* Forward declarations for drawing helpers used before their definitions. */
void gui_draw_rect(int x, int y, int w, int h, uint32_t color);
void gui_draw_rect_outline(int x, int y, int w, int h, uint32_t color,
                           int thickness);
void gui_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void gui_draw_circle(int cx, int cy, int r, uint32_t color, bool filled);
void gui_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void gui_draw_string(int x, int y, const char *str, uint32_t fg, uint32_t bg);
int gui_draw_system_app_icon(const char *app_id, int x, int y, int size);
static void gui_fill_rect_alpha(int x, int y, int w, int h, uint32_t color);
static void gui_draw_glass_panel(int x, int y, int w, int h, uint32_t tint,
                                 uint32_t glow, uint32_t border,
                                 int blur_stride);
static void draw_top_rounded_rect_alpha(int x, int y, int w, int h, int r,
                                        uint32_t color);
static void draw_filled_circle(int cx, int cy, int r, uint32_t color);
static int startup_flow_active(void);
static int startup_setup_account_active(void);
static int startup_setup_welcome_active(void);
static int startup_setup_account_form_active(void);
static int startup_setup_storage_active(void);
static void startup_close_other_windows(void);
static void startup_get_setup_layout(int content_x, int content_y, int content_w,
                                     int content_h, int *panel_x,
                                     int *panel_y, int *panel_w, int *panel_h,
                                     int *rail_w, int *card_x, int *card_y,
                                     int *card_w, int *card_h);
static void startup_get_setup_button_rect(int content_x, int content_y,
                                          int content_w, int content_h, int *x,
                                          int *y, int *w, int *h);
static void startup_get_setup_field_rect(int content_x, int content_y,
                                         int content_w, int content_h,
                                         int field_index, int *x, int *y,
                                         int *w, int *h);
static void installer_refresh_disk_inventory(void);
static const char *installer_selected_disk_label(void);
static void installer_write_target_config(void);
static void open_partition_manager_window(int x, int y);
static void draw_partition_manager_window(int content_x, int content_y,
                                          int content_w, int content_h);
static void partition_manager_refresh_partitions(void);
static void installer_ensure_parent_dirs(const char *path);
static int write_text_file(const char *path, const char *content);
static void str_copy_safe(char *dst, const char *src, int max);
static int str_cmp(const char *s1, const char *s2);
static const char *resolve_user_storage_path(const char *path, char *buf,
                                             int max);
static void ensure_user_storage_dirs(void);
static int user_storage_mkdir(const char *path, mode_t mode);
static int user_storage_unlink(const char *path);
static int user_storage_rmdir(const char *path);
static int user_storage_rename(const char *old_path, const char *new_path);
void gui_open_image_viewer(const char *path);
static void gui_play_mp3_file(const char *path);
void compositor_mark_full_redraw(void);
void gui_set_blur_effects_enabled(int enabled);
int gui_blur_effects_requested(void);
int gui_are_blur_effects_enabled(void);
int gui_is_gpu_rendering_enabled(void);

/* Blur/compositor state is defined later but used by early draw helpers. */
static int g_blur_effects_requested;
static int g_blur_effects_enabled;
static char g_gpu_backend_name[32];


/* Terminal functions from terminal.c */
struct terminal;
extern struct terminal *term_get_active(void);
extern struct terminal *term_create(int x, int y, int cols, int rows);
extern void term_set_active(struct terminal *term);
extern void term_handle_key(struct terminal *term, int key);
extern int term_get_input_len(struct terminal *t);
extern char term_get_input_char(struct terminal *t, int idx);
extern void term_render(struct terminal *term);
extern void term_set_content_pos(struct terminal *t, int x, int y);

/* ===================================================================== */
/* Display and Color */
/* ===================================================================== */

#define COLOR_BLACK 0x000000
#define COLOR_WHITE 0xFFFFFF
#define COLOR_RED 0xFF0000
#define COLOR_GREEN 0x00FF00
#define COLOR_BLUE 0x0000FF
#define COLOR_GRAY 0x808080
#define COLOR_DARK_GRAY 0x404040
#define COLOR_LIGHT_GRAY 0xC0C0C0

/* UI Theme Colors - Modern Premium Design */
#define THEME_BG 0x1A1A2E       /* Deep dark background */
#define THEME_FG 0xE4E4E7       /* Crisp light text */
#define THEME_ACCENT 0x6366F1   /* Indigo accent (modern) */
#define THEME_ACCENT2 0xEC4899  /* Pink accent */
#define THEME_TITLEBAR 0x27272A /* Dark zinc titlebar */
#define THEME_TITLEBAR_INACTIVE 0x3F3F46
#define THEME_BORDER 0x52525B       /* Zinc border */
#define THEME_BUTTON 0x3F3F46       /* Modern button background */
#define THEME_BUTTON_HOVER 0x525258 /* Button hover */

/* macOS Traffic Light Colors - Slightly desaturated for premium look */
#define COLOR_BTN_CLOSE 0xEF4444    /* Red */
#define COLOR_BTN_MINIMIZE 0xF59E0B /* Amber */
#define COLOR_BTN_ZOOM 0x22C55E     /* Green */

/* Menu Bar - Frosted glass effect */
#define COLOR_MENU_BG 0x18181B   /* Very dark */
#define COLOR_MENU_TEXT 0xFAFAFA /* White text */
#define MENU_BAR_HEIGHT 0

/* Dock - Modern glass dock */
#define COLOR_DOCK_BG 0x1F1F23     /* Dark dock */
#define COLOR_DOCK_BORDER 0x3F3F46 /* Subtle border */
#define COLOR_DOCK_GLASS 0x2A2A30  /* Glass effect layer */
#define DOCK_HEIGHT 70

#define KEY_WINDOW_SWITCHER 0x110
#define KEY_CTRL_ALT_DEL 0x111
#define KEY_UP 0x100
#define KEY_DOWN 0x101
#define KEY_LEFT 0x102
#define KEY_RIGHT 0x103

static int session_authenticated = 1;

static int dock_is_visible(void) {
  extern int boot_is_usb_boot(void);
  return !boot_is_usb_boot() && session_authenticated && !startup_flow_active();
}

static int dock_reserved_height(void) { return dock_is_visible() ? DOCK_HEIGHT : 0; }

static int desktop_session_active(void) {
  return session_authenticated && !startup_flow_active();
}

static int gui_is_installer_mode(void) {
  extern int boot_is_installer_mode(void);
  return boot_is_installer_mode();
}

static char installer_status[96] = "Ready to install the system image.";
static int installer_has_run = 0;
static int installer_active = 0;
static int installer_show_restart_screen = 0;
static int installer_phase = 0;
static int installer_progress_done = 0;
static int installer_progress_total = 5;
static int installer_copied_files = 0;
static int installer_failed_files = 0;
static int installer_ensured_changes = 0;
static uint64_t installer_reboot_deadline_ms = 0;
static char installer_target_root[96];
static char installer_efi_root[128];
static char installer_update_root[128];
static char partition_manager_status[96] = "Select a real disk to manage.";
static int installer_disk_count = 0;
static int installer_selected_disk = 0;
static char installer_disk_labels[8][80];
static int partition_manager_partition_count = 0;
static int partition_manager_selected_partition = 0;
static char partition_manager_labels[8][96];
static int window_switcher_frames = 0;
static char window_switcher_title[64] = "No windows";
static int secure_attention_open = 0;
static int secure_attention_selection = 0;

#define SECURE_ACTION_CANCEL 0
#define SECURE_ACTION_RESTART 1
#define SECURE_ACTION_SHUTDOWN 2
/* ===================================================================== */
/* Wallpaper Manager                                                     */
/* ===================================================================== */
#define NUM_WALLPAPERS 10
static int current_wallpaper = 0; /* 0 = Landscape (default image) */

/* Wallpaper types: 0 = gradient, 1 = image */
/* Wallpaper types: 0 = gradient, 1 = image */
static struct {
  int type;           /* 0 = gradient, 1 = JPEG image */
  uint8_t tr, tg, tb; /* Gradient: Top color */
  uint8_t br, bg, bb; /* Gradient: Bottom color */
  const char *name;   /* Display name */
  const char *path;   /* Image path (for type=1) */
} wallpapers[NUM_WALLPAPERS] = {
    {1, 38, 72, 120, 16, 30, 58, "Landscape", "/assets/wallpapers/landscape.jpg"},
    {1, 26, 92, 82, 9, 37, 48, "Nature", "/assets/wallpapers/nature.jpg"},
    {1, 84, 108, 148, 26, 33, 52, "City", "/assets/wallpapers/city.jpg"},
    {1, 124, 82, 126, 48, 28, 64, "Portrait", "/assets/wallpapers/portrait.jpg"},
    {1, 58, 88, 118, 22, 28, 46, "Wallpaper", "/assets/wallpapers/wallpaper.jpg"},
    {0, 30, 27, 75, 15, 27, 62, "Indigo Night", NULL},
    {0, 20, 60, 100, 10, 30, 60, "Ocean Blue", NULL},
    {0, 60, 20, 60, 30, 15, 45, "Purple Haze", NULL},
    {0, 20, 20, 20, 5, 5, 10, "Midnight", NULL},
    {0, 80, 60, 30, 40, 30, 20, "Golden Hour", NULL},
};

/* Cached wallpaper image for desktop background */
static media_image_t wallpaper_image = {0, 0, NULL};
static int wallpaper_loaded = -1; /* Which wallpaper is currently loaded */

/* Cached thumbnails for Background Settings window */
static media_image_t thumbnail_cache[NUM_WALLPAPERS] = {{0}};
static int thumbnails_loaded = 0;

/* Load all thumbnails once */
static void load_thumbnails(void) {
  if (thumbnails_loaded)
    return;

  for (int i = 0; i < NUM_WALLPAPERS; i++) {
    if (wallpapers[i].type == 1 && wallpapers[i].path) {
      uint8_t *data = NULL;
      size_t size = 0;
      if (media_load_file(wallpapers[i].path, &data, &size) == 0) {
        media_decode_jpeg(data, size, &thumbnail_cache[i]);
        media_free_file(data);
      }
    }
  }
  thumbnails_loaded = 1;
}
/* Static buffer for wallpaper to avoid heap fragmentation (4MB for 1024x1024)
 */
static uint32_t wallpaper_buffer[1024 * 1024];

/* Load wallpaper image if needed */
static void wallpaper_ensure_loaded(void) {
  if (wallpapers[current_wallpaper].type != 1)
    return; /* Gradient, no load */
  if (wallpaper_loaded == current_wallpaper && wallpaper_image.pixels != NULL)
    return; /* Already loaded */

  /* Reset previous image state (don't free valid static buffer, just reuse it)
   */
  wallpaper_image.width = 0;
  wallpaper_image.height = 0;
  wallpaper_loaded = -1;

  /* Load new image */
  const char *path = wallpapers[current_wallpaper].path;
  uint8_t *data = NULL;
  size_t size = 0;

  if (media_load_file(path, &data, &size) == 0) {
    if (media_decode_jpeg_buffer(data, size, &wallpaper_image, wallpaper_buffer,
                                 sizeof(wallpaper_buffer)) == 0) {
      wallpaper_loaded = current_wallpaper;
    } else {
      /* Fallback to gradient if decode fails */
      wallpapers[current_wallpaper].type = 0;
      wallpaper_loaded = -1;
    }
    media_free_file(data);
  } else {
    /* Fallback to gradient if load fails */
    wallpapers[current_wallpaper].type = 0;
  }
}

/* Get wallpaper pixel color at position */
static uint32_t wallpaper_get_pixel(int x, int y, int height) {
  int idx = current_wallpaper;

  /* Image wallpaper */
  if (wallpapers[idx].type == 1 && wallpaper_image.pixels) {
    /* Scale image to fit screen */
    int img_x = (x * wallpaper_image.width) / 1024; /* Assuming 1024 width */
    int img_y = (y * wallpaper_image.height) / height;
    if (img_x >= 0 && img_x < (int)wallpaper_image.width && img_y >= 0 &&
        img_y < (int)wallpaper_image.height) {
      return wallpaper_image.pixels[img_y * wallpaper_image.width + img_x];
    }
  }

  /* Gradient fallback */
  int progress = (y * 256) / height;
  if (progress < 0)
    progress = 0;
  if (progress > 255)
    progress = 255;

  uint8_t r = wallpapers[idx].tr +
              ((wallpapers[idx].br - wallpapers[idx].tr) * progress) / 256;
  uint8_t g = wallpapers[idx].tg +
              ((wallpapers[idx].bg - wallpapers[idx].tg) * progress) / 256;
  uint8_t b = wallpapers[idx].tb +
              ((wallpapers[idx].bb - wallpapers[idx].tb) * progress) / 256;

  return (r << 16) | (g << 8) | b;
}

/* Calculator state (global for click handling) */
static long calc_display = 0;
static long calc_pending = 0;
static char calc_op = 0;
static int calc_clear_next = 0;

static void calc_button_click(char key) {
  if (key >= '0' && key <= '9') {
    int digit = key - '0';
    if (calc_clear_next) {
      calc_display = digit;
      calc_clear_next = 0;
    } else {
      calc_display = calc_display * 10 + digit;
    }
  } else if (key == 'C') {
    calc_display = 0;
    calc_pending = 0;
    calc_op = 0;
    calc_clear_next = 0;
  } else if (key == '=') {
    if (calc_op == '+')
      calc_display = calc_pending + calc_display;
    else if (calc_op == '-')
      calc_display = calc_pending - calc_display;
    else if (calc_op == '*')
      calc_display = calc_pending * calc_display;
    else if (calc_op == '/' && calc_display != 0)
      calc_display = calc_pending / calc_display;
    calc_op = 0;
    calc_clear_next = 1;
  } else if (key == '+' || key == '-' || key == '*' || key == '/') {
    if (calc_op) {
      /* Chain operations */
      if (calc_op == '+')
        calc_display = calc_pending + calc_display;
      else if (calc_op == '-')
        calc_display = calc_pending - calc_display;
      else if (calc_op == '*')
        calc_display = calc_pending * calc_display;
      else if (calc_op == '/' && calc_display != 0)
        calc_display = calc_pending / calc_display;
    }
    calc_pending = calc_display;
    calc_op = key;
    calc_clear_next = 1;
  }
}

/* Notepad state (global for keyboard input) */
#define NOTEPAD_MAX_TEXT 2048
static char notepad_text[NOTEPAD_MAX_TEXT];
static char notepad_filepath[256]; /* Track open file */
static int notepad_cursor = 0;
static char notepad_status[96] = "Ready";
static int notepad_dirty = 0;

#define NOTEPAD_DIALOG_NONE 0
#define NOTEPAD_DIALOG_OPEN 1
#define NOTEPAD_DIALOG_SAVE 2
static int notepad_dialog_mode = NOTEPAD_DIALOG_NONE;
static char notepad_dialog_dir[256] = "/Documents";
static char notepad_dialog_input[256];
static char notepad_dialog_selected[64];
static struct window *notepad_find_window(void);
static void notepad_update_window_title(void);

/* Rename State */
static char rename_text[256];
static char rename_path[512];
static int rename_cursor = 0;

/* System Clipboard */
#define CLIPBOARD_MAX 1024
static char clipboard_buffer[CLIPBOARD_MAX];
static int clipboard_len = 0;

/* Terminal state (global for keyboard input) */
#define TERM_INPUT_MAX 256
#define TERM_HISTORY_LINES 16
static char term_input[TERM_INPUT_MAX];
static int term_input_len = 0;
static char term_history[TERM_HISTORY_LINES][80];
static int term_history_count = 0;
static int term_scroll = 0;

/* Snake game state */
#define SNAKE_MAX_LEN 100
#define SNAKE_GRID_W 20
#define SNAKE_GRID_H 12
static int snake_x[SNAKE_MAX_LEN];
static int snake_y[SNAKE_MAX_LEN];
static int snake_len = 4;
static int snake_dir = 1; /* 0=up, 1=right, 2=down, 3=left */
static int snake_food_x = 10;
static int snake_food_y = 6;
static int snake_score = 0;
static int snake_game_over = 0;

/* Mouse state (global for hover effects) */
static int mouse_x = 512, mouse_y = 384;
static int mouse_buttons = 0;
static int settings_active_tab = 0;
static char settings_status[96] = "Tune your desktop experience.";

typedef struct {
  uint16_t width;
  uint16_t height;
  const char *label;
} settings_resolution_option_t;

static const settings_resolution_option_t settings_resolution_options[] = {
    {1024, 768, "1024x768"},
    {1280, 720, "1280x720"},
    {1600, 900, "1600x900"},
    {1920, 1080, "1920x1080"},
};

#define SETTINGS_RESOLUTION_OPTION_COUNT \
  ((int)(sizeof(settings_resolution_options) / \
         sizeof(settings_resolution_options[0])))

static int settings_resolution_current_idx = -1;
static int settings_resolution_pending_idx = -1;
static uint32_t *g_saved_backbuffer;
static int wallpaper_cached;
static int wallpaper_cached_idx;
static void gui_clamp_windows_to_display(void);
static int gui_apply_resolution(uint32_t width, uint32_t height);

static void notepad_append_to_buf(char *dst, int max, const char *src) {
  int idx = 0;

  if (!dst || max <= 0)
    return;
  while (dst[idx] && idx < max - 1)
    idx++;
  for (int i = 0; src && src[i] && idx < max - 1; i++)
    dst[idx++] = src[i];
  dst[idx] = '\0';
}

static const char *notepad_basename(const char *path) {
  const char *name = path;

  if (!path || !path[0])
    return "Untitled";
  for (int i = 0; path[i]; i++) {
    if (path[i] == '/' && path[i + 1])
      name = &path[i + 1];
  }
  return name;
}

static void notepad_extract_parent_dir(const char *path, char *out, int out_max) {
  int last_slash = -1;

  if (!out || out_max <= 0)
    return;
  if (!path || !path[0]) {
    str_copy_safe(out, "/Documents", out_max);
    return;
  }

  for (int i = 0; path[i]; i++) {
    if (path[i] == '/')
      last_slash = i;
  }

  if (last_slash <= 0) {
    str_copy_safe(out, "/", out_max);
    return;
  }

  for (int i = 0; i < last_slash && i < out_max - 1; i++)
    out[i] = path[i];
  out[last_slash < out_max ? last_slash : out_max - 1] = '\0';
}

static void notepad_set_status(const char *msg) {
  str_copy_safe(notepad_status, msg, sizeof(notepad_status));
}

static void notepad_reset_document(void) {
  notepad_text[0] = '\0';
  notepad_cursor = 0;
  notepad_filepath[0] = '\0';
  notepad_dirty = 0;
  notepad_dialog_mode = NOTEPAD_DIALOG_NONE;
  notepad_dialog_selected[0] = '\0';
  notepad_dialog_input[0] = '\0';
  str_copy_safe(notepad_dialog_dir, "/Documents", sizeof(notepad_dialog_dir));
  notepad_set_status("New document");
  notepad_update_window_title();
}

static int notepad_load_file(const char *path) {
  struct file *f;
  char resolved_path[256];
  const char *open_path;
  int bytes;

  if (!path || !path[0]) {
    notepad_set_status("Open failed: no file selected");
    return -1;
  }

  open_path = resolve_user_storage_path(path, resolved_path,
                                        sizeof(resolved_path));
  f = vfs_open(open_path, O_RDONLY, 0);
  if (!f) {
    notepad_set_status("Open failed");
    return -1;
  }

  bytes = vfs_read(f, notepad_text, NOTEPAD_MAX_TEXT - 1);
  vfs_close(f);
  if (bytes < 0) {
    notepad_set_status("Open failed");
    return -1;
  }

  notepad_text[bytes] = '\0';
  notepad_cursor = bytes;
  str_copy_safe(notepad_filepath, path, sizeof(notepad_filepath));
  notepad_dirty = 0;
  notepad_dialog_mode = NOTEPAD_DIALOG_NONE;
  notepad_dialog_selected[0] = '\0';
  notepad_dialog_input[0] = '\0';
  notepad_extract_parent_dir(path, notepad_dialog_dir, sizeof(notepad_dialog_dir));
  notepad_set_status("File opened");
  notepad_update_window_title();
  return 0;
}

static int notepad_save_to_path(const char *path) {
  if (!path || !path[0]) {
    notepad_set_status("Save failed: no path");
    return -1;
  }

  installer_ensure_parent_dirs(path);
  if (write_text_file(path, notepad_text) != 0) {
    notepad_set_status("Save failed");
    return -1;
  }

  str_copy_safe(notepad_filepath, path, sizeof(notepad_filepath));
  notepad_extract_parent_dir(path, notepad_dialog_dir, sizeof(notepad_dialog_dir));
  notepad_dirty = 0;
  notepad_dialog_mode = NOTEPAD_DIALOG_NONE;
  notepad_dialog_selected[0] = '\0';
  notepad_set_status("File saved");
  notepad_update_window_title();
  return 0;
}

static void notepad_begin_dialog(int mode) {
  char default_name[256];

  notepad_dialog_mode = mode;
  notepad_dialog_selected[0] = '\0';

  if (notepad_filepath[0]) {
    str_copy_safe(notepad_dialog_input, notepad_filepath,
                  sizeof(notepad_dialog_input));
    notepad_extract_parent_dir(notepad_filepath, notepad_dialog_dir,
                               sizeof(notepad_dialog_dir));
  } else {
    str_copy_safe(notepad_dialog_dir, "/Documents", sizeof(notepad_dialog_dir));
    str_copy_safe(default_name, notepad_dialog_dir, sizeof(default_name));
    notepad_append_to_buf(default_name, sizeof(default_name), "/untitled.txt");
    str_copy_safe(notepad_dialog_input, default_name, sizeof(notepad_dialog_input));
  }

  notepad_set_status(mode == NOTEPAD_DIALOG_OPEN ? "Choose a file to open"
                                                 : "Choose where to save");
}

static void notepad_close_dialog(void) {
  notepad_dialog_mode = NOTEPAD_DIALOG_NONE;
  notepad_dialog_selected[0] = '\0';
  notepad_dialog_input[0] = '\0';
}

static void notepad_confirm_dialog(void) {
  if (notepad_dialog_mode == NOTEPAD_DIALOG_OPEN) {
    if (notepad_load_file(notepad_dialog_input) == 0)
      notepad_close_dialog();
  } else if (notepad_dialog_mode == NOTEPAD_DIALOG_SAVE) {
    if (notepad_save_to_path(notepad_dialog_input) == 0)
      notepad_close_dialog();
  }
}

/* Trig tables for Clock (fixed point 8.8, scale 256) */
/* 0..59 corresponds to 0..360 degrees clockwise from top */
/* x = sin(angle), y = -cos(angle) */
static const int clock_sin[60] = {
    0,    26,   53,   79,   104,  128,  150,  171,  189,  205,  219,  231,
    240,  248,  253,  256,  253,  248,  240,  231,  219,  205,  189,  171,
    150,  128,  104,  79,   53,   26,   0,    -26,  -53,  -79,  -104, -128,
    -150, -171, -189, -205, -219, -231, -240, -248, -253, -256, -253, -248,
    -240, -231, -219, -205, -189, -171, -150, -128, -104, -79,  -53,  -26};
static const int clock_cos[60] = {
    -256, -253, -248, -240, -231, -219, -205, -189, -171, -150, -128, -104,
    -79,  -53,  -26,  0,    26,   53,   79,   104,  128,  150,  171,  189,
    205,  219,  231,  240,  248,  253,  256,  253,  248,  240,  231,  219,
    205,  189,  171,  150,  128,  104,  79,   53,   26,   0,    -26,  -53,
    -79,  -104, -128, -150, -171, -189, -205, -219, -231, -240, -248, -253};

#if defined(ARCH_X86_64) || defined(ARCH_X86)
static uint8_t clock_cmos_read(uint8_t reg) {
  outb(0x70, reg);
  io_wait();
  return inb(0x71);
}

static int clock_bcd_to_int(uint8_t value) {
  return ((value >> 4) * 10) + (value & 0x0F);
}

static void clock_read_rtc_time(int *hours24, int *minutes, int *seconds) {
  uint8_t sec;
  uint8_t min;
  uint8_t hour;
  uint8_t reg_b;

  if (!hours24 || !minutes || !seconds)
    return;

  while (clock_cmos_read(0x0A) & 0x80) {
  }

  sec = clock_cmos_read(0x00);
  min = clock_cmos_read(0x02);
  hour = clock_cmos_read(0x04);
  reg_b = clock_cmos_read(0x0B);

  if (!(reg_b & 0x04)) {
    sec = (uint8_t)clock_bcd_to_int(sec);
    min = (uint8_t)clock_bcd_to_int(min);
    hour = (uint8_t)(((hour & 0x80) ? 0x80 : 0) | clock_bcd_to_int(hour & 0x7F));
  }

  if (!(reg_b & 0x02)) {
    int pm = hour & 0x80;
    hour &= 0x7F;
    if (pm && hour < 12)
      hour = (uint8_t)(hour + 12);
    else if (!pm && hour == 12)
      hour = 0;
  }

  *seconds = sec % 60;
  *minutes = min % 60;
  *hours24 = hour % 24;
}
#endif

static void clock_get_time(int *hours24, int *minutes, int *seconds) {
#if defined(ARCH_X86_64) || defined(ARCH_X86)
  clock_read_rtc_time(hours24, minutes, seconds);
  return;
#else
  int64_t secs;
  volatile uint32_t *pl031_data = (volatile uint32_t *)0x09010000;
  secs = *pl031_data;

  while (secs < 0) {
    secs += 24 * 3600;
  }

  if (hours24)
    *hours24 = (int)((secs / 3600) % 24);
  if (minutes)
    *minutes = (int)((secs / 60) % 60);
  if (seconds)
    *seconds = (int)(secs % 60);
#endif
}

static void clock_format_time(char *buf, int hours24, int minutes, int seconds) {
  buf[0] = '0' + (hours24 / 10);
  buf[1] = '0' + (hours24 % 10);
  buf[2] = ':';
  buf[3] = '0' + (minutes / 10);
  buf[4] = '0' + (minutes % 10);
  buf[5] = ':';
  buf[6] = '0' + (seconds / 10);
  buf[7] = '0' + (seconds % 10);
  buf[8] = '\0';
}

static void draw_clock_face(int cx, int cy, int radius, uint32_t face_color,
                            uint32_t rim_color, uint32_t tick_color) {
  gui_draw_circle(cx, cy, radius, face_color, true);
  gui_draw_circle(cx, cy, radius, rim_color, false);
  gui_draw_circle(cx, cy, radius - 1, rim_color, false);

  for (int i = 0; i < 60; i++) {
    int outer = radius - 4;
    int inner = (i % 5 == 0) ? radius - 14 : radius - 8;
    int x1 = cx + inner * clock_sin[i] / 256;
    int y1 = cy + inner * clock_cos[i] / 256;
    int x2 = cx + outer * clock_sin[i] / 256;
    int y2 = cy + outer * clock_cos[i] / 256;
    gui_draw_line(x1, y1, x2, y2, tick_color);
  }
}

static void draw_clock_hands(int cx, int cy, int radius, int hours24,
                             int minutes, int seconds) {
  int hours12 = hours24 % 12;
  int h_idx = (hours12 * 5 + minutes / 12) % 60;
  int hour_len = radius * 52 / 100;
  int minute_len = radius * 76 / 100;
  int second_len = radius * 84 / 100;

  int hx = cx + hour_len * clock_sin[h_idx] / 256;
  int hy = cy + hour_len * clock_cos[h_idx] / 256;
  int mx = cx + minute_len * clock_sin[minutes] / 256;
  int my = cy + minute_len * clock_cos[minutes] / 256;
  int sx = cx + second_len * clock_sin[seconds] / 256;
  int sy = cy + second_len * clock_cos[seconds] / 256;

  gui_draw_line(cx - 1, cy, hx - 1, hy, 0x202020);
  gui_draw_line(cx, cy, hx, hy, 0x202020);
  gui_draw_line(cx + 1, cy, hx + 1, hy, 0x202020);

  gui_draw_line(cx, cy, mx, my, 0x404040);
  gui_draw_line(cx + 1, cy, mx + 1, my, 0x404040);

  gui_draw_line(cx, cy, sx, sy, 0xD02020);
  gui_draw_circle(cx, cy, 4, 0xD02020, true);
}

static void draw_clock_widget(int content_x, int content_y, int content_w,
                              int content_h, uint32_t panel_bg) {
  int hours24, minutes, seconds;
  char time_str[9];
  int cx = content_x + content_w / 2;
  int cy = content_y + content_h / 2 - 8;
  int radius = (content_w < content_h ? content_w : content_h) / 2 - 20;

  if (radius < 28) {
    radius = 28;
  }

  clock_get_time(&hours24, &minutes, &seconds);
  clock_format_time(time_str, hours24, minutes, seconds);

  draw_clock_face(cx, cy, radius, 0xF8FAFC, 0x3B82F6, 0x334155);
  draw_clock_hands(cx, cy, radius, hours24, minutes, seconds);

  gui_draw_string(cx - 32, cy + radius + 10, time_str, 0xFFFFFF, panel_bg);
}

/* Initialize snake game */
static void snake_init(void) {
  snake_len = 4;
  snake_dir = 1;
  snake_score = 0;
  snake_game_over = 0;
  /* Start in middle */
  for (int i = 0; i < snake_len; i++) {
    snake_x[i] = 5 - i;
    snake_y[i] = 6;
  }
  snake_food_x = 15;
  snake_food_y = 6;
}

/* Move snake one step */
static void snake_move(void) {
  if (snake_game_over)
    return;

  /* Calculate new head position */
  int new_x = snake_x[0];
  int new_y = snake_y[0];

  switch (snake_dir) {
  case 0:
    new_y--;
    break; /* up */
  case 1:
    new_x++;
    break; /* right */
  case 2:
    new_y++;
    break; /* down */
  case 3:
    new_x--;
    break; /* left */
  }

  /* Wrap around */
  if (new_x < 0)
    new_x = SNAKE_GRID_W - 1;
  if (new_x >= SNAKE_GRID_W)
    new_x = 0;
  if (new_y < 0)
    new_y = SNAKE_GRID_H - 1;
  if (new_y >= SNAKE_GRID_H)
    new_y = 0;

  /* Check self-collision */
  for (int i = 0; i < snake_len; i++) {
    if (snake_x[i] == new_x && snake_y[i] == new_y) {
      snake_game_over = 1;
      return;
    }
  }

  /* Check food collision */
  int ate_food = (new_x == snake_food_x && new_y == snake_food_y);
  if (ate_food) {
    snake_score += 10;
    if (snake_len < SNAKE_MAX_LEN - 1) {
      snake_len++;
    }
    /* New food position (simple pseudo-random) */
    snake_food_x = (snake_food_x * 7 + 3) % SNAKE_GRID_W;
    snake_food_y = (snake_food_y * 5 + 7) % SNAKE_GRID_H;
  }

  /* Move body */
  for (int i = snake_len - 1; i > 0; i--) {
    snake_x[i] = snake_x[i - 1];
    snake_y[i] = snake_y[i - 1];
  }
  snake_x[0] = new_x;
  snake_y[0] = new_y;
}

/* Snake key handler */
static void snake_key(int key) {
  if (snake_game_over) {
    /* Any key restarts */
    snake_init();
    return;
  }

  int new_dir = snake_dir;

  /* Arrow keys (special codes from virtio keyboard) */
  if (key == 0x100 || key == 'w' || key == 'W')
    new_dir = 0; /* Up */
  else if (key == 0x103 || key == 'd' || key == 'D')
    new_dir = 1; /* Right */
  else if (key == 0x101 || key == 's' || key == 'S')
    new_dir = 2; /* Down */
  else if (key == 0x102 || key == 'a' || key == 'A')
    new_dir = 3; /* Left */

  /* Prevent 180-degree turns */
  if ((snake_dir == 0 && new_dir == 2) || (snake_dir == 2 && new_dir == 0) ||
      (snake_dir == 1 && new_dir == 3) || (snake_dir == 3 && new_dir == 1)) {
    return;
  }

  snake_dir = new_dir;
  snake_move(); /* Move immediately on key press */
}

static void notepad_key(int key) {
  if (notepad_dialog_mode != NOTEPAD_DIALOG_NONE) {
    if (key == 27) {
      notepad_close_dialog();
      notepad_set_status("Dialog closed");
      return;
    }
    if (key == '\n' || key == '\r') {
      notepad_confirm_dialog();
      return;
    }
    if (key == '\b' || key == 127) {
      int len = 0;
      while (notepad_dialog_input[len])
        len++;
      if (len > 0)
        notepad_dialog_input[len - 1] = '\0';
      return;
    }
    if (key >= 32 && key < 127) {
      int len = 0;
      while (notepad_dialog_input[len])
        len++;
      if (len < (int)sizeof(notepad_dialog_input) - 1) {
        notepad_dialog_input[len++] = (char)key;
        notepad_dialog_input[len] = '\0';
      }
    }
    return;
  }

  if (key == 14) { /* Ctrl+N */
    notepad_reset_document();
    return;
  }

  if (key == 15) { /* Ctrl+O */
    notepad_begin_dialog(NOTEPAD_DIALOG_OPEN);
    return;
  }

  if (key == 19) { /* Ctrl+S */
    if (notepad_filepath[0])
      notepad_save_to_path(notepad_filepath);
    else
      notepad_begin_dialog(NOTEPAD_DIALOG_SAVE);
    return;
  }

  /* Ctrl+C - Copy all text to clipboard */
  if (key == 3) { /* ASCII 3 = Ctrl+C */
    clipboard_len = 0;
    for (int i = 0; i < notepad_cursor && i < CLIPBOARD_MAX - 1; i++) {
      clipboard_buffer[i] = notepad_text[i];
      clipboard_len++;
    }
    clipboard_buffer[clipboard_len] = '\0';
    return;
  }

  /* Ctrl+V - Paste from clipboard */
  if (key == 22) { /* ASCII 22 = Ctrl+V */
    for (int i = 0; i < clipboard_len && notepad_cursor < NOTEPAD_MAX_TEXT - 1;
         i++) {
      notepad_text[notepad_cursor++] = clipboard_buffer[i];
    }
    notepad_text[notepad_cursor] = '\0';
    notepad_dirty = 1;
    notepad_set_status("Pasted from clipboard");
    notepad_update_window_title();
    return;
  }

  /* Ctrl+A - Select all (copy to clipboard) */
  if (key == 1) { /* ASCII 1 = Ctrl+A */
    clipboard_len = 0;
    for (int i = 0; i < notepad_cursor && i < CLIPBOARD_MAX - 1; i++) {
      clipboard_buffer[i] = notepad_text[i];
      clipboard_len++;
    }
    clipboard_buffer[clipboard_len] = '\0';
    return;
  }

  if (key == 24) { /* Ctrl+X */
    clipboard_len = 0;
    for (int i = 0; i < notepad_cursor && i < CLIPBOARD_MAX - 1; i++) {
      clipboard_buffer[i] = notepad_text[i];
      clipboard_len++;
    }
    clipboard_buffer[clipboard_len] = '\0';
    notepad_text[0] = '\0';
    notepad_cursor = 0;
    notepad_dirty = 1;
    notepad_set_status("Cut document to clipboard");
    notepad_update_window_title();
    return;
  }

  if (key == '\b' || key == 127) { /* Backspace */
    if (notepad_cursor > 0) {
      notepad_cursor--;
      notepad_text[notepad_cursor] = '\0';
      notepad_dirty = 1;
      notepad_set_status("Edited");
      notepad_update_window_title();
    }
  } else if (key >= 32 && key < 127) { /* Printable */
    if (notepad_cursor < NOTEPAD_MAX_TEXT - 1) {
      notepad_text[notepad_cursor++] = (char)key;
      notepad_text[notepad_cursor] = '\0';
      notepad_dirty = 1;
      notepad_set_status("Edited");
      notepad_update_window_title();
    }
  } else if (key == '\n' || key == '\r') { /* Enter */
    if (notepad_cursor < NOTEPAD_MAX_TEXT - 1) {
      notepad_text[notepad_cursor++] = '\n';
      notepad_text[notepad_cursor] = '\0';
      notepad_dirty = 1;
      notepad_set_status("Edited");
      notepad_update_window_title();
    }
  }
}

static void rename_key(int key) {
  if (key == '\b' || key == 127) { /* Backspace */
    if (rename_cursor > 0) {
      rename_cursor--;
      rename_text[rename_cursor] = '\0';
    }
  } else if (key >= 32 && key < 127) { /* Printable */
    if (rename_cursor < 255) {
      rename_text[rename_cursor++] = (char)key;
      rename_text[rename_cursor] = '\0';
    }
  }
}

/* Terminal key handler */
static void terminal_key(int key) {
  if (key == '\b' || key == 127) { /* Backspace */
    if (term_input_len > 0) {
      term_input_len--;
      term_input[term_input_len] = '\0';
    }
  } else if (key == '\n' || key == '\r') { /* Enter - execute command */
    if (term_input_len > 0) {
      /* Save to history */
      if (term_history_count < TERM_HISTORY_LINES) {
        for (int i = 0; i < term_input_len && i < 79; i++) {
          term_history[term_history_count][i] = term_input[i];
        }
        term_history[term_history_count]
                    [term_input_len < 79 ? term_input_len : 79] = '\0';
        term_history_count++;
      }

      /* Check for commands */
      if (term_input[0] == 'h' && term_input[1] == 'e' &&
          term_input[2] == 'l' && term_input[3] == 'p') {
        /* Help command */
      } else if (term_input[0] == 'c' && term_input[1] == 'l' &&
                 term_input[2] == 'e' && term_input[3] == 'a' &&
                 term_input[4] == 'r') {
        term_history_count = 0;
      }
      /* Clear input */
      term_input_len = 0;
      term_input[0] = '\0';
    }
  } else if (key >= 32 && key < 127) { /* Printable */
    if (term_input_len < TERM_INPUT_MAX - 1) {
      term_input[term_input_len++] = (char)key;
      term_input[term_input_len] = '\0';
    }
  }
}

/* ===================================================================== */
/* Display Driver Interface */
/* ===================================================================== */

struct display {
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  uint32_t bpp;
  uint32_t *framebuffer;
  uint32_t *backbuffer;
};

static struct display primary_display = {0};

struct gui_clip_state {
  int enabled;
  int x0;
  int y0;
  int x1;
  int y1;
};

static struct gui_clip_state g_clip = {0, 0, 0, 0, 0};

static struct gui_clip_state gui_set_clip_rect(int x, int y, int w, int h) {
  struct gui_clip_state prev = g_clip;

  g_clip.enabled = 1;
  g_clip.x0 = x;
  g_clip.y0 = y;
  g_clip.x1 = x + w;
  g_clip.y1 = y + h;

  return prev;
}

static void gui_restore_clip_rect(struct gui_clip_state prev) { g_clip = prev; }

/* ===================================================================== */
/* Basic Drawing Functions */
/* ===================================================================== */

static inline void draw_pixel(int x, int y, uint32_t color) {
  if (x < 0 || x >= (int)primary_display.width)
    return;
  if (y < 0 || y >= (int)primary_display.height)
    return;
  if (g_clip.enabled &&
      (x < g_clip.x0 || x >= g_clip.x1 || y < g_clip.y0 || y >= g_clip.y1))
    return;

  uint32_t *target = primary_display.backbuffer ? primary_display.backbuffer
                                                : primary_display.framebuffer;
  if (target) {
    target[y * (primary_display.pitch / 4) + x] = color;
  }
}

static inline void draw_pixel_alpha(int x, int y, uint32_t color) {
  if (x < 0 || x >= (int)primary_display.width)
    return;
  if (y < 0 || y >= (int)primary_display.height)
    return;
  if (g_clip.enabled &&
      (x < g_clip.x0 || x >= g_clip.x1 || y < g_clip.y0 || y >= g_clip.y1))
    return;

  uint32_t *target = primary_display.backbuffer ? primary_display.backbuffer
                                                : primary_display.framebuffer;
  if (!target)
    return;

  uint32_t alpha = (color >> 24) & 0xFF;
  if (alpha == 0) {
    return;
  }
  if (alpha == 0xFF) {
    target[y * (primary_display.pitch / 4) + x] = color & 0xFFFFFF;
    return;
  }

  uint32_t *dst = &target[y * (primary_display.pitch / 4) + x];
  uint32_t dst_color = *dst;
  uint32_t src_r = (color >> 16) & 0xFF;
  uint32_t src_g = (color >> 8) & 0xFF;
  uint32_t src_b = color & 0xFF;
  uint32_t dst_r = (dst_color >> 16) & 0xFF;
  uint32_t dst_g = (dst_color >> 8) & 0xFF;
  uint32_t dst_b = dst_color & 0xFF;
  uint32_t inv_alpha = 255 - alpha;
  uint32_t out_r = (src_r * alpha + dst_r * inv_alpha) / 255;
  uint32_t out_g = (src_g * alpha + dst_g * inv_alpha) / 255;
  uint32_t out_b = (src_b * alpha + dst_b * inv_alpha) / 255;
  *dst = (out_r << 16) | (out_g << 8) | out_b;
}

static inline uint32_t *gui_draw_target(void) {
  return primary_display.backbuffer ? primary_display.backbuffer
                                    : primary_display.framebuffer;
}

static inline void draw_image_pixel(int x, int y, uint32_t color) {
  if ((color >> 24) != 0)
    draw_pixel_alpha(x, y, color);
  else
    draw_pixel(x, y, color);
}

static void gui_fill_rect_alpha(int x, int y, int w, int h, uint32_t color) {
  for (int row = y; row < y + h; row++) {
    for (int col = x; col < x + w; col++) {
      draw_pixel_alpha(col, row, color);
    }
  }
}

static void gui_apply_backdrop_blur(int x, int y, int w, int h, int stride) {
  uint32_t *target = gui_draw_target();
  if (!target || w <= 0 || h <= 0)
    return;

  int pitch = (int)(primary_display.pitch / 4);
  if (stride < 1)
    stride = 1;

  for (int row = y; row < y + h; row += stride) {
    for (int col = x; col < x + w; col += stride) {
      if (col < 0 || col >= (int)primary_display.width || row < 0 ||
          row >= (int)primary_display.height)
        continue;

      uint32_t sum_r = 0, sum_g = 0, sum_b = 0, count = 0;
      for (int sy = -2; sy <= 2; sy += 2) {
        for (int sx = -2; sx <= 2; sx += 2) {
          int sample_x = col + sx;
          int sample_y = row + sy;
          if (sample_x < 0 || sample_x >= (int)primary_display.width ||
              sample_y < 0 || sample_y >= (int)primary_display.height)
            continue;
          uint32_t px = target[sample_y * pitch + sample_x];
          sum_r += (px >> 16) & 0xFF;
          sum_g += (px >> 8) & 0xFF;
          sum_b += px & 0xFF;
          count++;
        }
      }

      if (!count)
        continue;

      uint32_t blurred =
          (((sum_r / count) & 0xFF) << 16) | (((sum_g / count) & 0xFF) << 8) |
          ((sum_b / count) & 0xFF);

      for (int fy = 0; fy < stride && row + fy < y + h; fy++) {
        for (int fx = 0; fx < stride && col + fx < x + w; fx++) {
          int out_x = col + fx;
          int out_y = row + fy;
          if (out_x < 0 || out_x >= (int)primary_display.width || out_y < 0 ||
              out_y >= (int)primary_display.height)
            continue;
          target[out_y * pitch + out_x] = blurred;
        }
      }
    }
  }
}

static void gui_draw_glass_panel(int x, int y, int w, int h, uint32_t tint,
                                 uint32_t glow, uint32_t border,
                                 int blur_stride) {
  if (g_blur_effects_enabled) {
    gui_apply_backdrop_blur(x, y, w, h, blur_stride);
  }
  gui_fill_rect_alpha(x, y, w, h, tint);
  gui_fill_rect_alpha(x, y, w, 1, glow);
  gui_fill_rect_alpha(x, y, 1, h, glow);
  gui_fill_rect_alpha(x, y + h - 1, w, 1, border);
  gui_fill_rect_alpha(x + w - 1, y, 1, h, border);
}

void gui_draw_rect(int x, int y, int w, int h, uint32_t color) {
  for (int row = y; row < y + h; row++) {
    for (int col = x; col < x + w; col++) {
      draw_pixel(col, row, color);
    }
  }
}

void gui_draw_rect_outline(int x, int y, int w, int h, uint32_t color,
                           int thickness) {
  /* Top */
  gui_draw_rect(x, y, w, thickness, color);
  /* Bottom */
  gui_draw_rect(x, y + h - thickness, w, thickness, color);
  /* Left */
  gui_draw_rect(x, y, thickness, h, color);
  /* Right */
  gui_draw_rect(x + w - thickness, y, thickness, h, color);
}

void gui_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
  int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
  int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
  int sx = (x0 < x1) ? 1 : -1;
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx - dy;

  while (1) {
    draw_pixel(x0, y0, color);
    if (x0 == x1 && y0 == y1)
      break;
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void gui_draw_circle(int cx, int cy, int r, uint32_t color, bool filled) {
  int x = 0, y = r;
  int d = 3 - 2 * r;

  while (y >= x) {
    if (filled) {
      gui_draw_line(cx - x, cy + y, cx + x, cy + y, color);
      gui_draw_line(cx - x, cy - y, cx + x, cy - y, color);
      gui_draw_line(cx - y, cy + x, cx + y, cy + x, color);
      gui_draw_line(cx - y, cy - x, cx + y, cy - x, color);
    } else {
      draw_pixel(cx + x, cy + y, color);
      draw_pixel(cx - x, cy + y, color);
      draw_pixel(cx + x, cy - y, color);
      draw_pixel(cx - x, cy - y, color);
      draw_pixel(cx + y, cy + x, color);
      draw_pixel(cx - y, cy + x, color);
      draw_pixel(cx + y, cy - x, color);
      draw_pixel(cx - y, cy - x, color);
    }
    x++;
    if (d > 0) {
      y--;
      d = d + 4 * (x - y) + 10;
    } else {
      d = d + 4 * x + 6;
    }
  }
}

static void gui_draw_os_logo(int x, int y, int scale, uint32_t fg,
                             uint32_t accent, uint32_t bg) {
  int s = scale < 1 ? 1 : scale;
  int outer = 14 * s;
  int inset = 3 * s;
  int inner = outer - inset * 2;
  int cutout = inner / 2;
  int cutout_x = x + (outer - cutout) / 2;
  int cutout_y = y + (outer - cutout) / 2;
  int has_bg = bg != 0x00000000;

  if (has_bg)
    gui_draw_rect(x, y, outer, outer, bg);
  gui_draw_rect_outline(x, y, outer, outer, fg, s);
  gui_draw_rect(x + inset, y + inset, inner, inner, accent);

  if (has_bg) {
    gui_draw_rect(cutout_x, cutout_y, cutout, cutout, bg);
  } else {
    gui_draw_rect_outline(cutout_x, cutout_y, cutout, cutout, fg, s > 1 ? 2 : 1);
  }

  for (int i = 0; i < s; i++) {
    gui_draw_line(x + 2 * s, y + 2 * s + i, x + outer - 3 * s, y + outer - 3 * s + i,
                  fg);
    gui_draw_line(x + outer - 3 * s, y + 2 * s + i, x + 2 * s, y + outer - 3 * s + i,
                  fg);
  }
}

/* ===================================================================== */
/* 8x16 Font - use external complete font */
/* ===================================================================== */

#define FONT_WIDTH 8
#define FONT_HEIGHT 16

/* External font data from font.c - 256 characters */
extern const uint8_t font_data[256][16];

void gui_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
  if (x >= (int)primary_display.width || y >= (int)primary_display.height ||
      x + FONT_WIDTH <= 0 || y + FONT_HEIGHT <= 0) {
    return;
  }

  unsigned char idx = (unsigned char)c;
  const uint8_t *glyph = font_data[idx];

  for (int row = 0; row < FONT_HEIGHT; row++) {
    uint8_t line = glyph[row];
    for (int col = 0; col < FONT_WIDTH; col++) {
      uint32_t color = (line & (0x80 >> col)) ? fg : bg;
      if ((color >> 24) != 0) {
        draw_pixel_alpha(x + col, y + row, color);
      } else if ((line & (0x80 >> col)) || bg != 0x00000000) {
        draw_pixel(x + col, y + row, color);
      }
    }
  }
}

void gui_draw_string(int x, int y, const char *str, uint32_t fg, uint32_t bg) {
  int start_x = x;
  uint32_t effective_bg = ((bg >> 24) != 0) ? bg : 0x00000000;
  while (*str) {
    if (*str == '\n') {
      x = start_x;
      y += FONT_HEIGHT;
    } else {
      gui_draw_char(x, y, *str, fg, effective_bg);
      x += FONT_WIDTH;
    }
    str++;
  }
}

static void append_decimal(char *buf, int *idx, int value) {
  char digits[16];
  int count = 0;

  if (value == 0) {
    buf[(*idx)++] = '0';
    return;
  }

  if (value < 0) {
    buf[(*idx)++] = '-';
    value = -value;
  }

  while (value > 0 && count < (int)sizeof(digits)) {
    digits[count++] = '0' + (value % 10);
    value /= 10;
  }

  while (count > 0) {
    buf[(*idx)++] = digits[--count];
  }
}

static void build_resolution_string(char *buf, uint32_t width, uint32_t height) {
  int idx = 0;
  append_decimal(buf, &idx, (int)width);
  buf[idx++] = ' ';
  buf[idx++] = 'x';
  buf[idx++] = ' ';
  append_decimal(buf, &idx, (int)height);
  buf[idx] = '\0';
}

static void settings_sync_resolution_picker(void) {
  settings_resolution_current_idx = -1;
  for (int i = 0; i < SETTINGS_RESOLUTION_OPTION_COUNT; i++) {
    if (settings_resolution_options[i].width == primary_display.width &&
        settings_resolution_options[i].height == primary_display.height) {
      settings_resolution_current_idx = i;
      break;
    }
  }

  if (settings_resolution_pending_idx < 0 ||
      settings_resolution_pending_idx >= SETTINGS_RESOLUTION_OPTION_COUNT) {
    settings_resolution_pending_idx = settings_resolution_current_idx >= 0
                                          ? settings_resolution_current_idx
                                          : 0;
  }
}

static int settings_resolution_button_bounds(int panel_x, int panel_y, int index,
                                             int *x, int *y, int *w, int *h) {
  int card_y = panel_y + 72 + 104 + 84;

  if (index < 0 || index >= SETTINGS_RESOLUTION_OPTION_COUNT)
    return 0;

  if (x)
    *x = panel_x + 16 + index * 92;
  if (y)
    *y = card_y + 42;
  if (w)
    *w = 84;
  if (h)
    *h = 22;
  return 1;
}

static void build_device_ports_string(char *buf, int connected, int total) {
  int idx = 0;
  append_decimal(buf, &idx, connected);
  buf[idx++] = '/';
  append_decimal(buf, &idx, total);
  buf[idx++] = ' ';
  buf[idx++] = 'p';
  buf[idx++] = 'o';
  buf[idx++] = 'r';
  buf[idx++] = 't';
  if (total != 1) {
    buf[idx++] = 's';
  }
  buf[idx] = '\0';
}

/* ===================================================================== */
/* Window System */
/* ===================================================================== */

#define MAX_WINDOWS 64
#define TITLEBAR_HEIGHT 28
#define BORDER_WIDTH 2

typedef enum {
  WINDOW_NORMAL,
  WINDOW_MINIMIZED,
  WINDOW_MAXIMIZED,
  WINDOW_FULLSCREEN
} window_state_t;

struct window {
  int id;
  char title[64];
  int x, y;
  int width, height;
  window_state_t state;
  bool visible;
  bool focused;
  bool has_titlebar;
  bool resizable;
  uint32_t *content_buffer;
  void *userdata;

  /* Saved position for restore from maximize */
  int saved_x, saved_y;
  int saved_width, saved_height;

  /* Callbacks */
  void (*on_draw)(struct window *win);
  void (*on_key)(struct window *win, int key);
  void (*on_mouse)(struct window *win, int x, int y, int buttons);
  void (*on_close)(struct window *win);

  struct window *next;
};

static struct window windows[MAX_WINDOWS];
static struct window *window_stack = NULL; /* Z-order, top is focused */
static struct window *focused_window = NULL;
static int startup_window_opening = 0;
static int next_window_id = 1;

static void gui_clamp_windows_to_display(void) {
  int max_y = (int)primary_display.height - dock_reserved_height() - 12;

  for (struct window *win = window_stack; win; win = win->next) {
    if (!win->visible)
      continue;

    if (win->state == WINDOW_MAXIMIZED) {
      win->x = 0;
      win->y = MENU_BAR_HEIGHT;
      win->width = primary_display.width;
      win->height = primary_display.height - MENU_BAR_HEIGHT -
                    dock_reserved_height() - 12;
      continue;
    }

    if (win->width > (int)primary_display.width)
      win->width = primary_display.width;
    if (win->height > (int)primary_display.height - dock_reserved_height())
      win->height = primary_display.height - dock_reserved_height();
    if (win->x + win->width > (int)primary_display.width)
      win->x = (int)primary_display.width - win->width;
    if (win->x < 0)
      win->x = 0;
    if (win->y < MENU_BAR_HEIGHT)
      win->y = MENU_BAR_HEIGHT;
    if (win->y + win->height > max_y)
      win->y = max_y - win->height;
    if (win->y < MENU_BAR_HEIGHT)
      win->y = MENU_BAR_HEIGHT;
  }
}

static int gui_apply_resolution(uint32_t width, uint32_t height) {
  uint32_t *new_framebuffer = NULL;
  uint32_t new_width = 0;
  uint32_t new_height = 0;
  uint32_t *new_backbuffer = NULL;
  uint32_t new_pitch;
  uint32_t *old_backbuffer = primary_display.backbuffer;

  if (width == primary_display.width && height == primary_display.height)
    return 0;

#if !defined(ARCH_X86_64) && !defined(ARCH_X86)
  return -1;
#endif

  if (str_cmp(g_gpu_backend_name, "bochs-vbe") != 0 &&
      !pci_find_device(0x1234, 0x1111))
    return -1;

  if (bochs_init(width, height) != 0)
    return -1;

  bochs_get_info(&new_framebuffer, &new_width, &new_height);
  if (!new_framebuffer || new_width != width || new_height != height)
    return -1;

  new_pitch = new_width * 4;
  new_backbuffer = kmalloc(new_pitch * new_height);

  primary_display.framebuffer = new_framebuffer;
  primary_display.width = new_width;
  primary_display.height = new_height;
  primary_display.pitch = new_pitch;
  primary_display.backbuffer = new_backbuffer;
  g_saved_backbuffer = new_backbuffer;

  if (old_backbuffer)
    kfree(old_backbuffer);

  wallpaper_cached = 0;
  wallpaper_cached_idx = -1;

  mouse_x = (int)new_width / 2;
  mouse_y = (int)new_height / 2;
  gui_clamp_windows_to_display();
  compositor_mark_full_redraw();

  printk(KERN_INFO "GUI: Resolution changed to %ux%u\n", new_width, new_height);
  return 0;
}

static struct window *notepad_find_window(void) {
  for (int i = 0; i < MAX_WINDOWS; i++) {
    if (windows[i].id != 0 && windows[i].visible && windows[i].title[0] == 'N' &&
        windows[i].title[1] == 'o' && windows[i].title[2] == 't') {
      return &windows[i];
    }
  }
  return NULL;
}

static void notepad_update_window_title(void) {
  struct window *win = notepad_find_window();
  int idx = 0;

  if (!win)
    return;

  str_copy_safe(win->title, "Notepad", sizeof(win->title));
  idx = 7;
  if (notepad_filepath[0] && idx < (int)sizeof(win->title) - 3) {
    win->title[idx++] = ' ';
    win->title[idx++] = '-';
    win->title[idx++] = ' ';
    win->title[idx] = '\0';
    notepad_append_to_buf(win->title, sizeof(win->title),
                          notepad_basename(notepad_filepath));
  }
  if (notepad_dirty) {
    notepad_append_to_buf(win->title, sizeof(win->title), " *");
  }
}

static int window_title_equals(const struct window *win, const char *title) {
  int i = 0;

  if (!win || !title)
    return 0;

  while (win->title[i] && title[i]) {
    if (win->title[i] != title[i])
      return 0;
    i++;
  }

  return win->title[i] == '\0' && title[i] == '\0';
}

static int window_close_disabled(const struct window *win) {
  return gui_is_installer_mode() && window_title_equals(win, "Installer");
}

static int window_minimize_disabled(const struct window *win) {
  return gui_is_installer_mode() && window_title_equals(win, "Installer");
}


static void build_windows_string(char *buf) {
  int idx = 0;
  int count = 0;
  struct window *iter = window_stack;

  while (iter) {
    if (iter->visible) {
      count++;
    }
    iter = iter->next;
  }

  append_decimal(buf, &idx, count);
  buf[idx++] = ' ';
  buf[idx++] = 'o';
  buf[idx++] = 'p';
  buf[idx++] = 'e';
  buf[idx++] = 'n';
  buf[idx++] = ' ';
  buf[idx++] = 'w';
  buf[idx++] = 'i';
  buf[idx++] = 'n';
  buf[idx++] = 'd';
  buf[idx++] = 'o';
  buf[idx++] = 'w';
  if (count != 1) {
    buf[idx++] = 's';
  }
  buf[idx] = '\0';
}

/* Create a new window */
struct window *gui_create_window(const char *title, int x, int y, int w,
                                 int h) {
  if (startup_setup_account_active() && !startup_window_opening) {
    printk(KERN_INFO "GUI: Blocked window '%s' during account setup\n", title);
    return NULL;
  }

  /* Find free slot */
  struct window *win = NULL;
  for (int i = 0; i < MAX_WINDOWS; i++) {
    if (windows[i].id == 0) {
      win = &windows[i];
      break;
    }
  }

  if (!win) {
    printk(KERN_ERR "GUI: No free window slots\n");
    return NULL;
  }

  win->id = next_window_id++;
  for (int i = 0; i < 63 && title[i]; i++) {
    win->title[i] = title[i];
    win->title[i + 1] = '\0';
  }
  win->x = x;
  win->y = y;
  win->width = w;
  win->height = h;
  win->state = WINDOW_NORMAL;
  win->visible = true;
  win->focused = false;
  win->has_titlebar = true;
  win->resizable = true;

  /* Reset all callbacks and userdata - critical to prevent stale pointers */
  win->on_draw = NULL;
  win->on_mouse = NULL;
  win->on_key = NULL;
  win->on_close = NULL;
  win->userdata = NULL;

  /* Allocate content buffer */
  int content_h = h - TITLEBAR_HEIGHT - BORDER_WIDTH * 2;
  int content_w = w - BORDER_WIDTH * 2;
  win->content_buffer = kmalloc(content_w * content_h * 4);

  /* Add to stack */
  win->next = window_stack;
  window_stack = win;

  printk(KERN_INFO "GUI: Created window '%s' (%dx%d)\n", title, w, h);

  return win;
}

void gui_set_window_userdata(struct window *win, void *data) {
  if (win) {
    win->userdata = data;
  }
}

void gui_destroy_window(struct window *win) {
  if (!win || win->id == 0)
    return;

  if (win->on_close) {
    win->on_close(win);
  }

  /* Remove from stack */
  if (window_stack == win) {
    window_stack = win->next;
  } else {
    struct window *prev = window_stack;
    while (prev && prev->next != win) {
      prev = prev->next;
    }
    if (prev) {
      prev->next = win->next;
    }
  }

  if (win->content_buffer) {
    kfree(win->content_buffer);
  }

  win->id = 0;
}

void gui_focus_window(struct window *win) {
  if (!win)
    return;

  if (focused_window) {
    focused_window->focused = false;
  }

  /* Move to top of stack */
  if (window_stack != win) {
    struct window *prev = window_stack;
    while (prev && prev->next != win) {
      prev = prev->next;
    }
    if (prev) {
      prev->next = win->next;
      win->next = window_stack;
      window_stack = win;
    }
  }

  win->focused = true;
  focused_window = win;

  if (win->title[0] == 'T' && win->title[1] == 'e' && win->title[2] == 'r' &&
      win->userdata) {
    term_set_active((struct terminal *)win->userdata);
  }
}

static void copy_window_title(char *dst, const char *src) {
  int i = 0;
  if (!dst)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  for (; i < 63 && src[i]; i++) {
    dst[i] = src[i];
  }
  dst[i] = '\0';
}

static int count_visible_windows(void) {
  int count = 0;
  for (struct window *win = window_stack; win; win = win->next) {
    if (win->visible)
      count++;
  }
  return count;
}

static struct window *find_next_switchable_window(void) {
  struct window *first_visible = NULL;
  int seen_focused = 0;

  for (struct window *win = window_stack; win; win = win->next) {
    if (!win->visible)
      continue;
    if (!first_visible)
      first_visible = win;
    if (seen_focused)
      return win;
    if (win == focused_window)
      seen_focused = 1;
  }

  return first_visible;
}

static void activate_window_switcher(void) {
  if (count_visible_windows() <= 0)
    return;

  struct window *target = find_next_switchable_window();
  if (!target)
    return;

  gui_focus_window(target);
  copy_window_title(window_switcher_title, target->title);
  window_switcher_frames = 75;
  compositor_mark_full_redraw();
}

static void execute_secure_attention_action(int action) {
  if (action == SECURE_ACTION_CANCEL) {
    secure_attention_open = 0;
    compositor_mark_full_redraw();
    return;
  }

  if (action == SECURE_ACTION_RESTART) {
    extern void arch_reboot(void);
    arch_reboot();
    return;
  }

  if (action == SECURE_ACTION_SHUTDOWN) {
    extern void arch_poweroff(void);
    arch_poweroff();
    return;
  }
}

static void open_secure_attention(void) {
  secure_attention_open = 1;
  secure_attention_selection = SECURE_ACTION_CANCEL;
  window_switcher_frames = 0;
  compositor_mark_full_redraw();
}

static int secure_attention_button_hit(int x, int y) {
  int panel_w = 420;
  int panel_h = 220;
  int panel_x = ((int)primary_display.width - panel_w) / 2;
  int panel_y = ((int)primary_display.height - panel_h) / 2;
  int button_y = panel_y + 156;
  int button_w = 108;
  int button_h = 34;
  int button_gap = 18;
  int start_x = panel_x + (panel_w - (button_w * 3 + button_gap * 2)) / 2;

  for (int i = 0; i < 3; i++) {
    int bx = start_x + i * (button_w + button_gap);
    if (x >= bx && x < bx + button_w && y >= button_y && y < button_y + button_h)
      return i;
  }

  return -1;
}

static void draw_window_switcher_overlay(void) {
  if (window_switcher_frames <= 0)
    return;

  int panel_w = 360;
  int panel_h = 136;
  int panel_x = ((int)primary_display.width - panel_w) / 2;
  int panel_y = MENU_BAR_HEIGHT + 36;
  char info[64];

  gui_fill_rect_alpha(0, 0, primary_display.width, primary_display.height,
                      0x18000000);
  gui_draw_glass_panel(panel_x, panel_y, panel_w, panel_h, 0x9A303A50,
                       0x38FFFFFF, 0x90728298, 2);
  gui_draw_string(panel_x + 22, panel_y + 20, "Window Switcher", 0xFFFFFF,
                  0x00000000);
  gui_draw_string(panel_x + 22, panel_y + 56, window_switcher_title, 0xEAF2FF,
                  0x00000000);

  build_windows_string(info);
  gui_draw_string(panel_x + 22, panel_y + 88, info, 0xB8C4D8, 0x00000000);
  gui_draw_string(panel_x + 22, panel_y + 108, "Press Alt+Tab again to cycle",
                  0x95A4BC, 0x00000000);
}

static void draw_secure_attention_overlay(void) {
  if (!secure_attention_open)
    return;

  int panel_w = 420;
  int panel_h = 220;
  int panel_x = ((int)primary_display.width - panel_w) / 2;
  int panel_y = ((int)primary_display.height - panel_h) / 2;
  int button_y = panel_y + 156;
  int button_w = 108;
  int button_h = 34;
  int button_gap = 18;
  int start_x = panel_x + (panel_w - (button_w * 3 + button_gap * 2)) / 2;
  const char *labels[3] = {"Cancel", "Restart", "Shut Down"};

  gui_fill_rect_alpha(0, 0, primary_display.width, primary_display.height,
                      0x58000000);
  gui_draw_glass_panel(panel_x, panel_y, panel_w, panel_h, 0xAE2A3448,
                       0x42FFFFFF, 0xA07E8CA2, 2);

  gui_draw_string(panel_x + 24, panel_y + 24, "Ctrl+Alt+Delete", 0xFFFFFF,
                  0x00000000);
  gui_draw_string(panel_x + 24, panel_y + 64, "System controls", 0xD7E3F6,
                  0x00000000);
  gui_draw_string(panel_x + 24, panel_y + 92,
                  "Choose an action for this session.", 0xB6C3D8, 0x00000000);

  if (focused_window && focused_window->visible) {
    gui_draw_string(panel_x + 24, panel_y + 118, "Active window:", 0x8FA0BA,
                    0x00000000);
    gui_draw_string(panel_x + 126, panel_y + 118, focused_window->title,
                    0xEFF5FF, 0x00000000);
  }

  for (int i = 0; i < 3; i++) {
    int bx = start_x + i * (button_w + button_gap);
    uint32_t fill = (i == secure_attention_selection) ? 0xB04E6DA0 : 0x70404A5E;
    uint32_t border = (i == secure_attention_selection) ? 0xC4D8E7FF : 0x8E7A8BA4;
    gui_draw_glass_panel(bx, button_y, button_w, button_h, fill, 0x26FFFFFF,
                         border, 1);
    gui_draw_string(bx + 18, button_y + 10, labels[i], 0xFFFFFF, 0x00000000);
  }
}

/* Draw a filled circle (for traffic light buttons) */
static void draw_circle(int cx, int cy, int r, uint32_t color) {
  for (int y = -r; y <= r; y++) {
    for (int x = -r; x <= r; x++) {
      if (x * x + y * y <= r * r) {
        draw_pixel(cx + x, cy + y, color);
      }
    }
  }
}

/* Draw a single window */
/* ===================================================================== */
/* Render Helpers */
/* ===================================================================== */

extern struct file *vfs_open(const char *path, int flags, mode_t mode);
extern int vfs_close(struct file *file);
extern int vfs_readdir(struct file *file, void *ctx,
                       int (*filldir)(void *, const char *, int, loff_t, ino_t,
                                      unsigned));

/* Forward declaration */
/* Helper for string compare */
static int str_cmp(const char *s1, const char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static char to_lower(char c) {
  if (c >= 'A' && c <= 'Z')
    return (char)(c + 32);
  return c;
}

static int str_ends_with_ci(const char *name, const char *ext) {
  if (!name || !ext)
    return 0;
  int nlen = 0;
  int elen = 0;
  while (name[nlen])
    nlen++;
  while (ext[elen])
    elen++;
  if (elen == 0 || nlen < elen)
    return 0;
  for (int i = 0; i < elen; i++) {
    if (to_lower(name[nlen - elen + i]) != to_lower(ext[i]))
      return 0;
  }
  return 1;
}

static void str_copy_safe(char *dst, const char *src, int max) {
  int i = 0;
  if (!dst || max <= 0)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  while (src[i] && i < max - 1) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

typedef enum gui_app_kind {
  GUI_APP_TERMINAL,
  GUI_APP_FILES,
  GUI_APP_CALCULATOR,
  GUI_APP_NOTES,
  GUI_APP_SETTINGS,
  GUI_APP_CLOCK,
  GUI_APP_SNAKE,
  GUI_APP_HELP,
  GUI_APP_BROWSER,
  GUI_APP_APPSTORE
} gui_app_kind_t;

static int window_matches_app_kind(const struct window *win,
                                   gui_app_kind_t kind) {
  if (!win || !win->visible)
    return 0;

  switch (kind) {
  case GUI_APP_TERMINAL:
    return win->title[0] == 'T' && win->title[1] == 'e' && win->title[2] == 'r';
  case GUI_APP_FILES:
    return win->title[0] == 'F' && win->title[1] == 'i' &&
           win->title[2] == 'l';
  case GUI_APP_CALCULATOR:
    return win->title[0] == 'C' && win->title[1] == 'a' &&
           win->title[2] == 'l';
  case GUI_APP_NOTES:
    return win->title[0] == 'N' && win->title[1] == 'o' &&
           win->title[2] == 't';
  case GUI_APP_SETTINGS:
    return win->title[0] == 'S' && win->title[1] == 'e' &&
           win->title[2] == 't';
  case GUI_APP_CLOCK:
    return win->title[0] == 'C' && win->title[1] == 'l' &&
           win->title[2] == 'o';
  case GUI_APP_SNAKE:
    return win->title[0] == 'S' && win->title[1] == 'n' &&
           win->title[2] == 'a';
  case GUI_APP_HELP:
    return win->title[0] == 'H' && win->title[1] == 'e';
  case GUI_APP_BROWSER:
    return win->title[0] == 'B' && win->title[1] == 'r' &&
           win->title[2] == 'o';
  case GUI_APP_APPSTORE:
    return win->title[0] == 'A' && win->title[1] == 'p' &&
           win->title[2] == 'p' && win->title[3] == ' ';
  }
  return 0;
}

static int count_windows_for_app_kind(gui_app_kind_t kind) {
  int count = 0;
  for (struct window *win = window_stack; win; win = win->next) {
    if (window_matches_app_kind(win, kind))
      count++;
  }
  return count;
}

static struct window *find_window_for_app_kind(gui_app_kind_t kind) {
  for (struct window *win = window_stack; win; win = win->next) {
    if (window_matches_app_kind(win, kind))
      return win;
  }
  return NULL;
}

typedef struct {
  char id[32];
  char label[48];
  char shortcut_name[48];
  gui_app_kind_t kind;
  const uint32_t *icon_data;
  uint32_t icon_color;
  int default_dock;
  int visible_in_store;
} dock_app_def_t;

/* Forward declarations for startup/app-state helpers referenced before their
 * definitions. */
static void ensure_gui_app_dirs(void);
static void ensure_app_manifest(const dock_app_def_t *app);
static void dock_add_item(const dock_app_def_t *app);
static void save_dock_config(void);
static void dock_add_all_system_apps(void);

typedef struct {
  const char *id;
  const char *label;
  const char *shortcut_name;
  gui_app_kind_t kind;
  int default_dock;
  int visible_in_store;
} system_app_seed_t;

#define GUI_SYSTEM_DIR "/System"
#define GUI_SYSTEM_APPS_DIR "/System/Apps"
#define GUI_APPS_DIR "/Applications"
#define GUI_SYSTEM_APPS_FOLDER "/Desktop/System Apps"
#define GUI_DOCK_CONFIG_PATH "/System/dock.cfg"
#define GUI_SETUP_STATE_PATH "/System/setup-state.cfg"
#define GUI_ACCOUNT_PATH "/System/account.cfg"
#define GUI_ACCOUNTS_DIR "/System/Accounts"
#define GUI_VERSION_PATH "/System/version.cfg"
#define MAX_SYSTEM_APPS 24
#define MAX_DOCK_ITEMS 16
#define APP_STORE_CARD_HEIGHT 54

static const system_app_seed_t app_catalog_seed[] = {
    {"terminal", "Terminal", "Terminal.app", GUI_APP_TERMINAL, 1, 1},
    {"files", "Files", "Files.app", GUI_APP_FILES, 1, 1},
    {"calculator", "Calculator", "Calculator.app", GUI_APP_CALCULATOR, 1, 1},
    {"notes", "Notes", "Notes.app", GUI_APP_NOTES, 1, 1},
    {"settings", "Settings", "Settings.app", GUI_APP_SETTINGS, 0, 1},
    {"clock", "Clock", "Clock.app", GUI_APP_CLOCK, 0, 1},
    {"snake", "Snake", "Snake.app", GUI_APP_SNAKE, 0, 1},
    {"help", "Help", "Help.app", GUI_APP_HELP, 0, 1},
    {"browser", "Browser", "Browser.app", GUI_APP_BROWSER, 0, 1},
    {"appstore", "App Store", "App Store.app", GUI_APP_APPSTORE, 1, 0},
};

#define APP_CATALOG_SEED_COUNT                                                \
  ((int)(sizeof(app_catalog_seed) / sizeof(app_catalog_seed[0])))

static dock_app_def_t app_catalog[MAX_SYSTEM_APPS];
static int app_catalog_count = 0;
static int app_catalog_loaded = 0;
static const dock_app_def_t *dock_items[MAX_DOCK_ITEMS];
static int dock_item_count = 0;
static int dock_loaded = 0;

typedef enum {
  STARTUP_FLOW_NONE = 0,
  STARTUP_FLOW_SETUP_ACCOUNT,
  STARTUP_FLOW_LOGIN
} startup_flow_t;

typedef enum {
  STARTUP_SETUP_PAGE_WELCOME = 0,
  STARTUP_SETUP_PAGE_ACCOUNT,
  STARTUP_SETUP_PAGE_STORAGE
} startup_setup_page_t;

static startup_flow_t startup_flow = STARTUP_FLOW_NONE;
static startup_setup_page_t startup_setup_page = STARTUP_SETUP_PAGE_WELCOME;
static char account_username[32] = "";
static char account_password[33] = "";
static char account_partition_label[32] = "";
static char account_disk_location[32] = "";
static char startup_input_username[32] = "";
static char startup_input_password[32] = "";
static int startup_active_field = 0;
static char startup_status[96] = "";
static struct window *startup_window = NULL;

static uint64_t parse_u64(const char *text) {
  uint64_t value = 0;
  int i = 0;
  while (text && text[i] >= '0' && text[i] <= '9') {
    value = value * 10 + (uint64_t)(text[i] - '0');
    i++;
  }
  return value;
}

static void load_system_app_catalog(void);

static const uint32_t *icon_data_for_kind(gui_app_kind_t kind) {
  switch (kind) {
  case GUI_APP_TERMINAL:
    return dock_icon_terminal;
  case GUI_APP_FILES:
    return dock_icon_folder;
  case GUI_APP_CALCULATOR:
    return dock_icon_calculator;
  case GUI_APP_NOTES:
    return dock_icon_notes;
  case GUI_APP_SETTINGS:
    return dock_icon_settings;
  case GUI_APP_CLOCK:
    return dock_icon_clock;
  case GUI_APP_SNAKE:
    return dock_icon_notes;
  case GUI_APP_HELP:
    return dock_icon_settings;
  case GUI_APP_BROWSER:
  case GUI_APP_APPSTORE:
    return dock_icon_folder;
  }
  return dock_icon_terminal;
}

static uint32_t icon_color_for_kind(gui_app_kind_t kind) {
  switch (kind) {
  case GUI_APP_TERMINAL:
    return 0x1E1E1E;
  case GUI_APP_FILES:
    return 0x3B82F6;
  case GUI_APP_CALCULATOR:
    return 0xFF9500;
  case GUI_APP_NOTES:
    return 0xFFCC00;
  case GUI_APP_SETTINGS:
    return 0x8E8E93;
  case GUI_APP_CLOCK:
    return 0x000000;
  case GUI_APP_SNAKE:
    return 0x34D399;
  case GUI_APP_HELP:
    return 0x3B82F6;
  case GUI_APP_BROWSER:
    return 0x0EA5E9;
  case GUI_APP_APPSTORE:
    return 0x7C3AED;
  }
  return 0x3B82F6;
}

static void draw_system_app_icon_kind(gui_app_kind_t kind, int x, int y,
                                      int size);

static const char *kind_to_string(gui_app_kind_t kind) {
  switch (kind) {
  case GUI_APP_TERMINAL:
    return "terminal";
  case GUI_APP_FILES:
    return "files";
  case GUI_APP_CALCULATOR:
    return "calculator";
  case GUI_APP_NOTES:
    return "notes";
  case GUI_APP_SETTINGS:
    return "settings";
  case GUI_APP_CLOCK:
    return "clock";
  case GUI_APP_SNAKE:
    return "snake";
  case GUI_APP_HELP:
    return "help";
  case GUI_APP_BROWSER:
    return "browser";
  case GUI_APP_APPSTORE:
    return "appstore";
  }
  return "terminal";
}

static gui_app_kind_t kind_from_string(const char *kind) {
  if (!kind)
    return GUI_APP_TERMINAL;
  if (str_cmp(kind, "files") == 0)
    return GUI_APP_FILES;
  if (str_cmp(kind, "calculator") == 0)
    return GUI_APP_CALCULATOR;
  if (str_cmp(kind, "notes") == 0)
    return GUI_APP_NOTES;
  if (str_cmp(kind, "settings") == 0)
    return GUI_APP_SETTINGS;
  if (str_cmp(kind, "clock") == 0)
    return GUI_APP_CLOCK;
  if (str_cmp(kind, "snake") == 0)
    return GUI_APP_SNAKE;
  if (str_cmp(kind, "help") == 0)
    return GUI_APP_HELP;
  if (str_cmp(kind, "browser") == 0)
    return GUI_APP_BROWSER;
  if (str_cmp(kind, "appstore") == 0)
    return GUI_APP_APPSTORE;
  return GUI_APP_TERMINAL;
}

static void fill_runtime_app(dock_app_def_t *app, const char *id,
                             const char *label, const char *shortcut_name,
                             gui_app_kind_t kind, int default_dock,
                             int visible_in_store) {
  str_copy_safe(app->id, id, sizeof(app->id));
  str_copy_safe(app->label, label, sizeof(app->label));
  str_copy_safe(app->shortcut_name, shortcut_name, sizeof(app->shortcut_name));
  app->kind = kind;
  app->icon_data = icon_data_for_kind(kind);
  app->icon_color = icon_color_for_kind(kind);
  app->default_dock = default_dock;
  app->visible_in_store = visible_in_store;
}

static const dock_app_def_t *find_catalog_app(const char *id) {
  load_system_app_catalog();
  if (!id)
    return NULL;
  for (int i = 0; i < app_catalog_count; i++) {
    if (str_cmp(app_catalog[i].id, id) == 0)
      return &app_catalog[i];
  }
  return NULL;
}

static int app_manifest_path(const dock_app_def_t *app, char *path, int max) {
  if (!app || !path || max < 32)
    return -1;
  str_copy_safe(path, GUI_APPS_DIR, max);
  int idx = 0;
  while (path[idx])
    idx++;
  if (idx >= max - 1)
    return -1;
  path[idx++] = '/';
  for (int i = 0; app->id[i] && idx < max - 5; i++) {
    path[idx++] = app->id[i];
  }
  path[idx++] = '.';
  path[idx++] = 'a';
  path[idx++] = 'p';
  path[idx++] = 'p';
  path[idx] = '\0';
  return 0;
}

static int build_app_shortcut_path(const char *dir, const char *shortcut_name,
                                   char *path, int max) {
  int idx = 0;

  if (!dir || !shortcut_name || !path || max < 8)
    return -1;

  str_copy_safe(path, dir, max);
  while (path[idx])
    idx++;
  if (idx >= max - 1)
    return -1;
  if (idx > 0 && path[idx - 1] != '/')
    path[idx++] = '/';
  for (int i = 0; shortcut_name[i] && idx < max - 1; i++)
    path[idx++] = shortcut_name[i];
  path[idx] = '\0';
  return 0;
}

static int write_text_file(const char *path, const char *content) {
  return media_install_text_file(path, content);
}

static int installer_get_persistent_root(char *buf, int max) {
  static const char *roots[] = {"/Persist", "/persist", "/disk", "/mnt/disk"};
  struct file *dir;

  if (!buf || max <= 0)
    return -1;

  for (int i = 0; i < (int)(sizeof(roots) / sizeof(roots[0])); i++) {
    dir = vfs_open(roots[i], O_RDONLY, 0);
    if (!dir)
      continue;
    vfs_close(dir);
    str_copy_safe(buf, roots[i], max);
    return 0;
  }

  buf[0] = '\0';
  return -1;
}

static int path_starts_with(const char *path, const char *prefix) {
  int i = 0;

  if (!path || !prefix)
    return 0;

  while (prefix[i]) {
    if (path[i] != prefix[i])
      return 0;
    i++;
  }

  return 1;
}

static int account_storage_root_path(char *buf, int max) {
  int idx = 0;

  if (!buf || max <= 0)
    return -1;
  buf[0] = '\0';

  if (!account_disk_location[0] || !account_partition_label[0])
    return -1;

  str_copy_safe(buf, "/Installed", max);
  while (buf[idx] && idx < max - 1)
    idx++;
  if (idx < max - 1)
    buf[idx++] = '/';
  for (int i = 0; account_disk_location[i] && idx < max - 1; i++)
    buf[idx++] = account_disk_location[i];
  if (idx < max - 1)
    buf[idx++] = '/';
  for (int i = 0; account_partition_label[i] && idx < max - 1; i++)
    buf[idx++] = account_partition_label[i];
  buf[idx] = '\0';
  return 0;
}

static int path_is_active_account_home(const char *path) {
  char home_prefix[96];
  int idx = 0;

  if (!path || !account_username[0])
    return 0;

  str_copy_safe(home_prefix, "/Users/", sizeof(home_prefix));
  idx = 7;
  for (int i = 0; account_username[i] && idx < (int)sizeof(home_prefix) - 1;
       i++)
    home_prefix[idx++] = account_username[i];
  home_prefix[idx] = '\0';

  if (!path_starts_with(path, home_prefix))
    return 0;

  return path[idx] == '\0' || path[idx] == '/';
}

static int path_is_user_storage(const char *path) {
  if (!path)
    return 0;
  return str_cmp(path, "/Users") == 0 ||
         (path[0] == '/' && path[1] == 'U' && path[2] == 's' &&
          path[3] == 'e' && path[4] == 'r' && path[5] == 's' &&
          path[6] == '/');
}

static const char *resolve_user_storage_path(const char *path, char *buf,
                                             int max) {
  char account_root[160];
  char home_prefix[96];
  char persistent_root[64];
  int path_idx = 0;
  int idx = 0;

  if (!path || !buf || max <= 0)
    return path;
  if (path_is_active_account_home(path) &&
      account_storage_root_path(account_root, sizeof(account_root)) == 0) {
    str_copy_safe(home_prefix, "/Users/", sizeof(home_prefix));
    path_idx = 7;
    while (home_prefix[path_idx] && path_idx < (int)sizeof(home_prefix) - 1)
      path_idx++;
    for (int i = 0; account_username[i] &&
                    path_idx < (int)sizeof(home_prefix) - 1;
         i++) {
      home_prefix[path_idx++] = account_username[i];
    }
    home_prefix[path_idx] = '\0';
    path_idx = 0;
    while (home_prefix[path_idx] && path[path_idx] == home_prefix[path_idx])
      path_idx++;

    for (int i = 0; account_root[i] && idx < max - 1; i++)
      buf[idx++] = account_root[i];
    while (path[path_idx] && idx < max - 1)
      buf[idx++] = path[path_idx++];
    buf[idx] = '\0';
    return buf;
  }
  if (!path_is_user_storage(path))
    return path;
  if (installer_get_persistent_root(persistent_root,
                                    sizeof(persistent_root)) != 0) {
    return path;
  }

  for (int i = 0; persistent_root[i] && idx < max - 1; i++)
    buf[idx++] = persistent_root[i];
  for (int i = 0; path[i] && idx < max - 1; i++)
    buf[idx++] = path[i];
  buf[idx] = '\0';
  return buf;
}

static void ensure_user_storage_dirs(void) {
  char account_root[160];
  char user_home[96];
  char persistent_path[160];
  int idx = 0;

  vfs_mkdir("/Users", 0755);
  resolve_user_storage_path("/Users", persistent_path, sizeof(persistent_path));
  if (str_cmp(persistent_path, "/Users") != 0)
    vfs_mkdir(persistent_path, 0755);

  if (!account_username[0])
    return;

  str_copy_safe(user_home, "/Users/", sizeof(user_home));
  idx = 7;
  for (int i = 0; account_username[i] && idx < (int)sizeof(user_home) - 1; i++)
    user_home[idx++] = account_username[i];
  user_home[idx] = '\0';

  vfs_mkdir(user_home, 0755);
  resolve_user_storage_path(user_home, persistent_path, sizeof(persistent_path));
  if (str_cmp(persistent_path, user_home) != 0)
    vfs_mkdir(persistent_path, 0755);
  if (account_storage_root_path(account_root, sizeof(account_root)) == 0) {
    installer_ensure_parent_dirs(account_root);
    vfs_mkdir(account_root, 0755);
  }
}

static int user_storage_mkdir(const char *path, mode_t mode) {
  char resolved[256];
  const char *target = resolve_user_storage_path(path, resolved, sizeof(resolved));

  if (path_is_user_storage(path))
    vfs_mkdir("/Users", 0755);
  return vfs_mkdir(target, mode);
}

static int user_storage_unlink(const char *path) {
  char resolved[256];
  const char *target = resolve_user_storage_path(path, resolved, sizeof(resolved));
  return vfs_unlink(target);
}

static int user_storage_rmdir(const char *path) {
  char resolved[256];
  extern int vfs_rmdir(const char *path);
  const char *target = resolve_user_storage_path(path, resolved, sizeof(resolved));
  return vfs_rmdir(target);
}

static int user_storage_rename(const char *old_path, const char *new_path) {
  char resolved_old[256];
  char resolved_new[256];
  extern int vfs_rename(const char *old, const char *new);
  const char *old_target =
      resolve_user_storage_path(old_path, resolved_old, sizeof(resolved_old));
  const char *new_target =
      resolve_user_storage_path(new_path, resolved_new, sizeof(resolved_new));
  return vfs_rename(old_target, new_target);
}

static int installer_translate_target_path(const char *path, char *buf,
                                           int max) {
  char persistent_root[64];
  int prefix_len = 0;
  int idx = 0;

  if (!path || !buf || max <= 0 || !installer_target_root[0])
    return -1;
  if (installer_get_persistent_root(persistent_root,
                                    sizeof(persistent_root)) != 0)
    return -1;

  while (installer_target_root[prefix_len])
    prefix_len++;
  for (int i = 0; i < prefix_len; i++) {
    if (path[i] != installer_target_root[i])
      return -1;
  }
  if (path[prefix_len] && path[prefix_len] != '/')
    return -1;

  for (int i = 0; persistent_root[i] && idx < max - 1; i++)
    buf[idx++] = persistent_root[i];
  for (int i = prefix_len; path[i] && idx < max - 1; i++)
    buf[idx++] = path[i];
  buf[idx] = '\0';
  return 0;
}

static int installer_write_raw_file(const char *path, const uint8_t *data,
                                    size_t size) {
  struct file *f;
  ssize_t written;

  if (!path || (!data && size > 0))
    return -1;

  installer_ensure_parent_dirs(path);
  vfs_unlink(path);
  f = vfs_open(path, O_CREAT | O_WRONLY, 0644);
  if (!f)
    return -1;
  written = vfs_write(f, (const char *)data, size);
  vfs_close(f);
  return (written < 0) ? (int)written : 0;
}

static int installer_write_target_file(const char *logical_path,
                                       const uint8_t *data, size_t size) {
  char physical_path[256];

  if (installer_write_raw_file(logical_path, data, size) != 0)
    return -1;
  if (installer_translate_target_path(logical_path, physical_path,
                                      sizeof(physical_path)) == 0) {
    if (installer_write_raw_file(physical_path, data, size) != 0)
      return -1;
  }
  return 0;
}

static int installer_write_target_text_file(const char *logical_path,
                                            const char *content) {
  size_t len = 0;
  if (!content)
    return -1;
  while (content[len])
    len++;
  return installer_write_target_file(logical_path, (const uint8_t *)content,
                                     len);
}

static int read_text_file(const char *path, char *buf, int max) {
  uint8_t *data;
  size_t size;

  if (!path || !buf || max <= 1)
    return -1;

  if (media_load_file(path, &data, &size) != 0)
    return -1;
  if ((int)size >= max)
    size = (size_t)(max - 1);
  for (size_t i = 0; i < size; i++) {
    buf[i] = (char)data[i];
  }
  buf[size] = '\0';
  media_free_file(data);
  return (int)size;
}

static int manifest_get_value(const char *manifest, const char *key, char *out,
                              int max) {
  int key_len = 0;
  int i = 0;

  if (!manifest || !key || !out || max <= 0)
    return -1;

  while (key[key_len])
    key_len++;

  while (manifest[i]) {
    int j = 0;
    while (j < key_len && manifest[i + j] == key[j])
      j++;
    if (j == key_len && manifest[i + j] == '=') {
      int out_idx = 0;
      i += key_len + 1;
      while (manifest[i] && manifest[i] != '\n' && manifest[i] != '\r' &&
             out_idx < max - 1) {
        out[out_idx++] = manifest[i++];
      }
      out[out_idx] = '\0';
      return 0;
    }
    while (manifest[i] && manifest[i] != '\n')
      i++;
    if (manifest[i] == '\n')
      i++;
  }

  out[0] = '\0';
  return -1;
}

static int app_is_installed(const dock_app_def_t *app) {
  char path[128];
  uint8_t *data = NULL;
  size_t size = 0;
  if (!app)
    return 0;
  if (app_manifest_path(app, path, sizeof(path)) != 0)
    return 0;
  if (media_load_file(path, &data, &size) != 0)
    return 0;
  media_free_file(data);
  return 1;
}

static int load_app_id_from_manifest_file(const char *path, const char *fallback,
                                          char *app_id, int max) {
  char manifest[160];

  if (!app_id || max <= 0)
    return -1;
  app_id[0] = '\0';

  if (path && read_text_file(path, manifest, sizeof(manifest)) >= 0 &&
      manifest_get_value(manifest, "id", app_id, max) == 0 && app_id[0]) {
    return 0;
  }

  if (fallback) {
    int out = 0;
    for (int i = 0; fallback[i] && fallback[i] != '.' && out < max - 1; i++) {
      char c = fallback[i];
      if (c >= 'A' && c <= 'Z')
        c = (char)(c + 32);
      if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        app_id[out++] = c;
    }
    app_id[out] = '\0';
  }

  return app_id[0] ? 0 : -1;
}

static int account_manifest_path(const char *username, char *path, int max) {
  int idx = 0;

  if (!username || !username[0] || !path || max < 32)
    return -1;

  str_copy_safe(path, GUI_ACCOUNTS_DIR, max);
  while (path[idx])
    idx++;
  if (idx >= max - 1)
    return -1;
  path[idx++] = '/';
  for (int i = 0; username[i] && idx < max - 5; i++)
    path[idx++] = username[i];
  path[idx++] = '.';
  path[idx++] = 'c';
  path[idx++] = 'f';
  path[idx++] = 'g';
  path[idx] = '\0';
  return 0;
}

static int read_account_manifest(const char *username, char *manifest, int max) {
  char path[160];

  if (!manifest || max <= 0)
    return -1;

  if (username && username[0] &&
      account_manifest_path(username, path, sizeof(path)) == 0 &&
      read_text_file(path, manifest, max) >= 0) {
    return 0;
  }

  if (read_text_file(GUI_ACCOUNT_PATH, manifest, max) >= 0)
    return 0;
  return -1;
}

static int parse_account_manifest(const char *manifest, const char *fallback_name,
                                  char *username, int username_max,
                                  char *password_hash, int password_hash_max,
                                  char *partition_label, int partition_label_max,
                                  char *disk_location, int disk_location_max) {
  char legacy_password[32];

  if (!manifest)
    return -1;

  if (username && username_max > 0)
    username[0] = '\0';
  if (password_hash && password_hash_max > 0)
    password_hash[0] = '\0';
  if (partition_label && partition_label_max > 0)
    partition_label[0] = '\0';
  if (disk_location && disk_location_max > 0)
    disk_location[0] = '\0';

  if (username && username_max > 0) {
    manifest_get_value(manifest, "username", username, username_max);
    if (!username[0] && fallback_name)
      str_copy_safe(username, fallback_name, username_max);
  }
  if (partition_label && partition_label_max > 0)
    manifest_get_value(manifest, "partition_label", partition_label,
                       partition_label_max);
  if (disk_location && disk_location_max > 0)
    manifest_get_value(manifest, "disk_location", disk_location,
                       disk_location_max);
  if (password_hash && password_hash_max > 0 &&
      manifest_get_value(manifest, "password_hash", password_hash,
                         password_hash_max) == 0 &&
      password_hash[0]) {
    return 0;
  }

  legacy_password[0] = '\0';
  if (password_hash && password_hash_max > 0 && username && username[0] &&
      manifest_get_value(manifest, "password", legacy_password,
                         sizeof(legacy_password)) == 0 &&
      legacy_password[0]) {
    vib_password_hash_hex(username, legacy_password, password_hash,
                          password_hash_max);
    return 0;
  }

  return -1;
}

static void set_startup_status(const char *message) {
  str_copy_safe(startup_status, message, sizeof(startup_status));
}

static int startup_flow_active(void) {
  return startup_flow != STARTUP_FLOW_NONE;
}

static int startup_setup_account_active(void) {
  return startup_flow == STARTUP_FLOW_SETUP_ACCOUNT;
}

static int startup_setup_welcome_active(void) {
  return startup_setup_account_active() &&
         startup_setup_page == STARTUP_SETUP_PAGE_WELCOME;
}

static int startup_setup_account_form_active(void) {
  return startup_setup_account_active() &&
         startup_setup_page == STARTUP_SETUP_PAGE_ACCOUNT;
}

static int startup_setup_storage_active(void) {
  return startup_setup_account_active() &&
         startup_setup_page == STARTUP_SETUP_PAGE_STORAGE;
}

static void startup_close_other_windows(void) {
  for (int i = 0; i < MAX_WINDOWS; i++) {
    if (windows[i].id != 0) {
      gui_destroy_window(&windows[i]);
    }
  }
  focused_window = NULL;
  startup_window = NULL;
}

static void startup_get_setup_layout(int content_x, int content_y, int content_w,
                                     int content_h, int *panel_x,
                                     int *panel_y, int *panel_w, int *panel_h,
                                     int *rail_w, int *card_x, int *card_y,
                                     int *card_w, int *card_h) {
  int margin_x = content_w > 900 ? 64 : 24;
  int margin_y = content_h > 640 ? 48 : 24;
  int local_panel_x = content_x + margin_x;
  int local_panel_y = content_y + margin_y;
  int local_panel_w = content_w - margin_x * 2;
  int local_panel_h = content_h - margin_y * 2;
  int local_rail_w = local_panel_w > 760 ? 260 : 210;
  int local_card_x;
  int local_card_y;
  int local_card_w;
  int local_card_h;

  if (local_panel_w < 320)
    local_panel_w = 320;
  if (local_panel_h < 220)
    local_panel_h = 220;

  local_card_x = local_panel_x + local_rail_w + 24;
  local_card_y = local_panel_y + 24;
  local_card_w = local_panel_w - local_rail_w - 48;
  local_card_h = local_panel_h - 48;

  if (local_card_w < 220)
    local_card_w = 220;
  if (local_card_h < 160)
    local_card_h = 160;

  if (panel_x)
    *panel_x = local_panel_x;
  if (panel_y)
    *panel_y = local_panel_y;
  if (panel_w)
    *panel_w = local_panel_w;
  if (panel_h)
    *panel_h = local_panel_h;
  if (rail_w)
    *rail_w = local_rail_w;
  if (card_x)
    *card_x = local_card_x;
  if (card_y)
    *card_y = local_card_y;
  if (card_w)
    *card_w = local_card_w;
  if (card_h)
    *card_h = local_card_h;
}

static void startup_get_setup_button_rect(int content_x, int content_y,
                                          int content_w, int content_h, int *x,
                                          int *y, int *w, int *h) {
  int card_x = 0, card_y = 0, card_w = 0, card_h = 0;
  startup_get_setup_layout(content_x, content_y, content_w, content_h, NULL,
                           NULL, NULL, NULL, NULL, &card_x, &card_y, &card_w,
                           &card_h);
  if (x)
    *x = card_x + 36;
  if (y)
    *y = card_y + card_h - 78;
  if (w)
    *w = 220;
  if (h)
    *h = 42;
}

static void startup_get_setup_field_rect(int content_x, int content_y,
                                         int content_w, int content_h,
                                         int field_index, int *x, int *y,
                                         int *w, int *h) {
  int card_x = 0, card_y = 0, card_w = 0;
  startup_get_setup_layout(content_x, content_y, content_w, content_h, NULL,
                           NULL, NULL, NULL, NULL, &card_x, &card_y, &card_w,
                           NULL);
  if (x)
    *x = card_x + 36;
  if (y)
    *y = card_y + (field_index == 0 ? 132 : 216);
  if (w)
    *w = card_w - 72;
  if (h)
    *h = 42;
}

static void installer_clear_first_boot_setup_flag(void) {
  write_text_file("/System/installer-state.txt",
                  "installed=1\nprofile=system-image\nsource=installer-iso\n"
                  "first_boot_setup=0\n");
}

static void mask_secret(const char *src, char *dst, int max) {
  int idx = 0;
  if (!dst || max <= 0)
    return;
  while (src && src[idx] && idx < max - 1) {
    dst[idx] = '*';
    idx++;
  }
  dst[idx] = '\0';
}

static void append_input_char(char *buf, int max, int key) {
  int len = 0;
  if (!buf || max <= 1)
    return;
  while (buf[len])
    len++;
  if (key == 8) {
    if (len > 0)
      buf[len - 1] = '\0';
    return;
  }
  if (key < 32 || key > 126 || len >= max - 1)
    return;
  buf[len++] = (char)key;
  buf[len] = '\0';
}

static void load_account_state(void) {
  char manifest[256];

  account_username[0] = '\0';
  account_password[0] = '\0';
  account_partition_label[0] = '\0';
  account_disk_location[0] = '\0';
  if (read_account_manifest(NULL, manifest, sizeof(manifest)) != 0)
    return;
  parse_account_manifest(manifest, NULL, account_username,
                         sizeof(account_username), account_password,
                         sizeof(account_password), account_partition_label,
                         sizeof(account_partition_label), account_disk_location,
                         sizeof(account_disk_location));
}

static void load_setup_state(int *setup_complete, int *apps_seeded,
                             int *state_found) {
  char manifest[192];
  char value[16];

  if (setup_complete)
    *setup_complete = 0;
  if (apps_seeded)
    *apps_seeded = 0;
  if (state_found)
    *state_found = 0;

  if (read_text_file(GUI_SETUP_STATE_PATH, manifest, sizeof(manifest)) < 0)
    return;

  if (state_found)
    *state_found = 1;

  if (setup_complete &&
      manifest_get_value(manifest, "setup_complete", value, sizeof(value)) ==
          0) {
    *setup_complete = value[0] == '1';
  }
  if (apps_seeded &&
      manifest_get_value(manifest, "apps_seeded", value, sizeof(value)) == 0) {
    *apps_seeded = value[0] == '1';
  }
}

static void save_setup_state(int setup_complete, int apps_seeded) {
  char manifest[96];
  int idx = 0;
  const char *setup_value = setup_complete ? "1" : "0";
  const char *apps_value = apps_seeded ? "1" : "0";

  for (const char *p = "setup_complete=";
       *p && idx < (int)sizeof(manifest) - 1; p++)
    manifest[idx++] = *p;
  manifest[idx++] = setup_value[0];
  manifest[idx++] = '\n';

  for (const char *p = "apps_seeded="; *p && idx < (int)sizeof(manifest) - 1;
       p++)
    manifest[idx++] = *p;
  manifest[idx++] = apps_value[0];
  manifest[idx++] = '\n';
  manifest[idx] = '\0';

  write_text_file(GUI_SETUP_STATE_PATH, manifest);
}

static void save_account_state(void) {
  char manifest[256];
  char per_user_path[160];
  int idx = 0;

  for (const char *p = "username="; *p && idx < (int)sizeof(manifest) - 1; p++)
    manifest[idx++] = *p;
  for (int i = 0; account_username[i] && idx < (int)sizeof(manifest) - 2; i++)
    manifest[idx++] = account_username[i];
  manifest[idx++] = '\n';
  for (const char *p = "password_hash=";
       *p && idx < (int)sizeof(manifest) - 1; p++)
    manifest[idx++] = *p;
  for (int i = 0; account_password[i] && idx < (int)sizeof(manifest) - 2; i++)
    manifest[idx++] = account_password[i];
  manifest[idx++] = '\n';
  for (const char *p = "partition_label=";
       *p && idx < (int)sizeof(manifest) - 1; p++)
    manifest[idx++] = *p;
  for (int i = 0; account_partition_label[i] &&
                  idx < (int)sizeof(manifest) - 2;
       i++)
    manifest[idx++] = account_partition_label[i];
  manifest[idx++] = '\n';
  for (const char *p = "disk_location=";
       *p && idx < (int)sizeof(manifest) - 1; p++)
    manifest[idx++] = *p;
  for (int i = 0; account_disk_location[i] &&
                  idx < (int)sizeof(manifest) - 2;
       i++)
    manifest[idx++] = account_disk_location[i];
  manifest[idx++] = '\n';
  manifest[idx] = '\0';

  ensure_gui_app_dirs();
  write_text_file(GUI_ACCOUNT_PATH, manifest);
  if (account_manifest_path(account_username, per_user_path,
                            sizeof(per_user_path)) == 0) {
    write_text_file(per_user_path, manifest);
  }
}

static int load_install_target_disk_location(char *buf, int max) {
  char manifest[256];
  int fallback_disk = -1;

  if (!buf || max <= 0)
    return -1;
  buf[0] = '\0';

  if (read_text_file("/System/install-target.cfg", manifest, sizeof(manifest)) <
      0) {
    extern int storage_get_disk_count(void);
    extern int storage_get_disk_kind(int index);
    extern int storage_get_disk_location(int index, char *buf, int max);
    int disk_count = storage_get_disk_count();

    for (int i = 0; i < disk_count; i++) {
      int kind = storage_get_disk_kind(i);
      if (kind == STORAGE_KIND_CDROM || kind == STORAGE_KIND_USB_MASS_STORAGE)
        continue;
      fallback_disk = i;
      break;
    }

    if (fallback_disk >= 0 &&
        storage_get_disk_location(fallback_disk, buf, max) == 0 && buf[0]) {
      return 0;
    }
    return -1;
  }

  if (manifest_get_value(manifest, "disk_location", buf, max) != 0 || !buf[0])
    return -1;
  return 0;
}

static void startup_default_data_label(int ordinal, char *buf, int max) {
  if (!buf || max <= 0)
    return;
  buf[0] = '\0';
  str_copy_safe(buf, "Data", max);
  if (ordinal > 0) {
    int idx = 0;
    while (buf[idx] && idx < max - 1)
      idx++;
    if (idx < max - 1)
      buf[idx++] = ' ';
    append_decimal(buf, &idx, ordinal + 1);
  }
}

static int startup_assign_account_partition(void) {
  char disk_location[32];
  char partition_label[32];
  int disk_index;
  int data_partitions;

  extern int storage_get_disk_index_by_location(const char *location);
  extern int storage_disk_supports_partition_writes(int disk_index);
  extern int storage_count_partitions_of_kind(int disk_index,
                                              storage_partition_kind_t kind);
  extern int storage_create_partition(int disk_index,
                                      storage_partition_kind_t kind,
                                      uint32_t size_mib);
  extern int storage_get_disk_location(int index, char *buf, int max);

  if (account_partition_label[0] && account_disk_location[0])
    return 0;

  if (load_install_target_disk_location(disk_location, sizeof(disk_location)) !=
      0) {
    set_startup_status("Setup warning: install disk not recorded.");
    return -1;
  }

  disk_index = storage_get_disk_index_by_location(disk_location);
  if (disk_index < 0) {
    set_startup_status("Setup warning: target disk is unavailable.");
    return -1;
  }

  data_partitions =
      storage_count_partitions_of_kind(disk_index, STORAGE_PARTITION_DATA);
  if (data_partitions == 0) {
    if (!storage_disk_supports_partition_writes(disk_index) ||
        storage_create_partition(disk_index, STORAGE_PARTITION_DATA, 4096) !=
            0) {
      set_startup_status("Setup warning: user partition could not be created.");
      return -1;
    }
    startup_default_data_label(0, partition_label, sizeof(partition_label));
  } else {
    startup_default_data_label(data_partitions - 1, partition_label,
                               sizeof(partition_label));
  }

  str_copy_safe(account_partition_label, partition_label,
                sizeof(account_partition_label));
  storage_get_disk_location(disk_index, account_disk_location,
                            sizeof(account_disk_location));
  return 0;
}

static void seed_all_system_apps_once(void) {
  int apps_seeded = 0;
  int setup_complete = 0;
  int state_found = 0;

  load_setup_state(&setup_complete, &apps_seeded, &state_found);
  if (state_found && apps_seeded) {
    return;
  }

  dock_item_count = 0;
  dock_loaded = 1;
  save_dock_config();
  save_setup_state(1, 1);
}

static void startup_open_modal_window(void) {
  int setup_active = startup_setup_account_active();
  int win_w = setup_active ? (int)primary_display.width : 520;
  int win_h = setup_active ? (int)primary_display.height : 280;
  int win_x = setup_active ? 0 : ((int)primary_display.width - win_w) / 2;
  int win_y = setup_active ? 0 : ((int)primary_display.height - win_h) / 2;
  const char *title = startup_setup_welcome_active()
                          ? "Welcome"
                          : startup_setup_storage_active()
                                ? "Prepare Storage"
                          : startup_flow == STARTUP_FLOW_SETUP_ACCOUNT
                                ? "Setup Account"
                          : "Login";

  if (setup_active) {
    desktop_hide_context_menu();
    secure_attention_open = 0;
    startup_close_other_windows();
  }

  startup_window_opening = 1;
  startup_window = gui_create_window(title, win_x, win_y, win_w, win_h);
  startup_window_opening = 0;
  if (startup_window) {
    startup_window->has_titlebar = false;
    startup_window->resizable = false;
    gui_focus_window(startup_window);
  }
}

static void startup_begin_login_flow(const char *message, int preserve_username) {
  session_authenticated = 0;
  startup_flow = STARTUP_FLOW_LOGIN;
  startup_setup_page = STARTUP_SETUP_PAGE_WELCOME;
  startup_input_password[0] = '\0';
  startup_active_field = preserve_username ? 1 : 0;
  if (!preserve_username)
    startup_input_username[0] = '\0';
  set_startup_status(message ? message : "");
  desktop_hide_context_menu();
  secure_attention_open = 0;
  startup_close_other_windows();
  startup_open_modal_window();
}

static void ensure_startup_flow(void) {
  int needs_account_setup = 0;
  int setup_complete = 0;
  int apps_seeded = 0;
  int setup_state_found = 0;
  int live_disk_boot = 0;
  int account_configured = 0;

  if (gui_is_installer_mode())
    return;

  {
    extern int boot_is_usb_boot(void);
    live_disk_boot = boot_is_usb_boot();
  }

  load_account_state();
  load_setup_state(&setup_complete, &apps_seeded, &setup_state_found);
  account_configured = account_username[0] && account_password[0];
  startup_input_username[0] = '\0';
  startup_input_password[0] = '\0';
  startup_setup_page = STARTUP_SETUP_PAGE_WELCOME;
  startup_active_field = 0;
  set_startup_status("");
  ensure_user_storage_dirs();

  if (!setup_state_found) {
    setup_complete = account_configured;
  }

  if (live_disk_boot) {
    session_authenticated = 1;
    startup_flow = STARTUP_FLOW_NONE;
    installer_clear_first_boot_setup_flag();
    if (!apps_seeded)
      seed_all_system_apps_once();
    if (startup_window) {
      gui_destroy_window(startup_window);
      startup_window = NULL;
    }
    desktop_refresh();
    return;
  }

  if (account_configured) {
    setup_complete = 1;
    installer_clear_first_boot_setup_flag();
  }

  needs_account_setup = !account_configured || !setup_complete;
  if (!needs_account_setup) {
    if (!apps_seeded)
      seed_all_system_apps_once();
    startup_begin_login_flow("", 0);
    return;
  }

  session_authenticated = 0;
  startup_flow = STARTUP_FLOW_SETUP_ACCOUNT;
  startup_setup_page = STARTUP_SETUP_PAGE_WELCOME;
  startup_window = NULL;
  save_setup_state(0, 0);
  startup_open_modal_window();
}

static void complete_startup_auth(void) {
  session_authenticated = 1;
  startup_flow = STARTUP_FLOW_NONE;
  set_startup_status("");
  save_setup_state(1, 0);
  installer_clear_first_boot_setup_flag();
  if (startup_window) {
    gui_destroy_window(startup_window);
    startup_window = NULL;
  }
  seed_all_system_apps_once();
  desktop_refresh();
}

static void submit_startup_flow(void) {
  if (startup_flow == STARTUP_FLOW_SETUP_ACCOUNT) {
    if (startup_setup_welcome_active()) {
      startup_setup_page = STARTUP_SETUP_PAGE_ACCOUNT;
      startup_active_field = 0;
      set_startup_status("Create your account to continue.");
      return;
    }

    if (startup_setup_storage_active()) {
      if (startup_assign_account_partition() == 0) {
        ensure_user_storage_dirs();
        save_account_state();
        startup_begin_login_flow("Setup complete. Sign in to finish booting.",
                                 1);
      }
      return;
    }

    if (!startup_setup_account_form_active()) {
      set_startup_status("Setup is waiting for the next screen.");
      return;
    }

    char password_hash[33];

    if (!startup_input_username[0] || !startup_input_password[0]) {
      set_startup_status("Enter both a username and password.");
      return;
    }
    str_copy_safe(account_username, startup_input_username,
                  sizeof(account_username));
    vib_password_hash_hex(account_username, startup_input_password,
                          password_hash, sizeof(password_hash));
    str_copy_safe(account_password, password_hash, sizeof(account_password));
    startup_setup_page = STARTUP_SETUP_PAGE_STORAGE;
    startup_active_field = 0;
    set_startup_status("Prepare a personal data partition for this account.");
    return;
  }

  {
    char manifest[256];
    char login_username[32];
    char login_password_hash[33];
    char login_partition_label[32];
    char login_disk_location[32];
    char password_hash[33];

    if (read_account_manifest(startup_input_username, manifest,
                              sizeof(manifest)) == 0 &&
        parse_account_manifest(manifest, startup_input_username, login_username,
                               sizeof(login_username), login_password_hash,
                               sizeof(login_password_hash),
                               login_partition_label,
                               sizeof(login_partition_label),
                               login_disk_location,
                               sizeof(login_disk_location)) == 0) {
      vib_password_hash_hex(startup_input_username, startup_input_password,
                            password_hash, sizeof(password_hash));
      if (vib_secure_string_eq(password_hash, login_password_hash)) {
        str_copy_safe(account_username, login_username, sizeof(account_username));
        str_copy_safe(account_password, login_password_hash,
                      sizeof(account_password));
        str_copy_safe(account_partition_label, login_partition_label,
                      sizeof(account_partition_label));
        str_copy_safe(account_disk_location, login_disk_location,
                      sizeof(account_disk_location));
        ensure_user_storage_dirs();
        complete_startup_auth();
        return;
      }
    }
  }

  set_startup_status("Login failed. Check your username and password.");
}

static void startup_handle_key(int key) {
  char *target;

  if (startup_setup_welcome_active()) {
    if (key == '\r' || key == '\n' || key == ' ')
      submit_startup_flow();
    return;
  }

  if (startup_setup_storage_active()) {
    if (key == '\r' || key == '\n' || key == ' ')
      submit_startup_flow();
    return;
  }

  target = startup_active_field == 0 ? startup_input_username
                                     : startup_input_password;

  if (key == '\t') {
    startup_active_field = 1 - startup_active_field;
    return;
  }
  if (key == '\r' || key == '\n') {
    submit_startup_flow();
    return;
  }
  append_input_char(target, 32, key);
}

int gui_requires_login(void) { return startup_flow_active() || !session_authenticated; }

static void ensure_gui_app_dirs(void) {
  vfs_mkdir(GUI_SYSTEM_DIR, 0755);
  vfs_mkdir(GUI_ACCOUNTS_DIR, 0755);
  vfs_mkdir(GUI_SYSTEM_APPS_DIR, 0755);
  vfs_mkdir(GUI_APPS_DIR, 0755);
  vfs_mkdir("/Desktop", 0755);
  vfs_mkdir(GUI_SYSTEM_APPS_FOLDER, 0755);
}

static int system_manifest_path_by_id(const char *app_id, char *path, int max) {
  int idx = 0;

  if (!app_id || !path || max < 32)
    return -1;

  str_copy_safe(path, GUI_SYSTEM_APPS_DIR, max);
  while (path[idx])
    idx++;
  if (idx >= max - 1)
    return -1;
  path[idx++] = '/';
  for (int i = 0; app_id[i] && idx < max - 5; i++) {
    path[idx++] = app_id[i];
  }
  path[idx++] = '.';
  path[idx++] = 'a';
  path[idx++] = 'p';
  path[idx++] = 'p';
  path[idx] = '\0';
  return 0;
}

static void write_system_app_seed(const system_app_seed_t *seed) {
  char path[128];
  char manifest[256];
  int idx = 0;
  const char *kind_str;

  if (!seed)
    return;
  if (system_manifest_path_by_id(seed->id, path, sizeof(path)) != 0)
    return;
  {
    struct file *existing = vfs_open(path, O_RDONLY, 0);
    if (existing) {
      vfs_close(existing);
      return;
    }
  }

  kind_str = kind_to_string(seed->kind);
  idx = 0;

  str_copy_safe(manifest, "id=", sizeof(manifest));
  idx = 3;
  for (int i = 0; seed->id[i] && idx < (int)sizeof(manifest) - 2; i++)
    manifest[idx++] = seed->id[i];
  manifest[idx++] = '\n';

  for (const char *p = "label="; *p && idx < (int)sizeof(manifest) - 1; p++)
    manifest[idx++] = *p;
  for (int i = 0; seed->label[i] && idx < (int)sizeof(manifest) - 2; i++)
    manifest[idx++] = seed->label[i];
  manifest[idx++] = '\n';

  for (const char *p = "shortcut="; *p && idx < (int)sizeof(manifest) - 1; p++)
    manifest[idx++] = *p;
  for (int i = 0; seed->shortcut_name[i] && idx < (int)sizeof(manifest) - 2;
       i++)
    manifest[idx++] = seed->shortcut_name[i];
  manifest[idx++] = '\n';

  for (const char *p = "kind="; *p && idx < (int)sizeof(manifest) - 1; p++)
    manifest[idx++] = *p;
  for (int i = 0; kind_str[i] && idx < (int)sizeof(manifest) - 2; i++)
    manifest[idx++] = kind_str[i];
  manifest[idx++] = '\n';

  for (const char *p = "default_dock=";
       *p && idx < (int)sizeof(manifest) - 1; p++)
    manifest[idx++] = *p;
  manifest[idx++] = seed->default_dock ? '1' : '0';
  manifest[idx++] = '\n';

  for (const char *p = "store="; *p && idx < (int)sizeof(manifest) - 1; p++)
    manifest[idx++] = *p;
  manifest[idx++] = seed->visible_in_store ? '1' : '0';
  manifest[idx++] = '\n';
  manifest[idx] = '\0';

  write_text_file(path, manifest);
}

static int load_system_app_from_manifest(const char *path) {
  char manifest[256];
  char id[32];
  char label[48];
  char shortcut_name[48];
  char kind_buf[32];
  char dock_buf[8];
  char store_buf[8];

  if (app_catalog_count >= MAX_SYSTEM_APPS)
    return -1;
  if (read_text_file(path, manifest, sizeof(manifest)) < 0)
    return -1;
  if (manifest_get_value(manifest, "id", id, sizeof(id)) != 0 || !id[0])
    return -1;
  for (int i = 0; i < app_catalog_count; i++) {
    if (str_cmp(app_catalog[i].id, id) == 0)
      return 0;
  }

  if (manifest_get_value(manifest, "label", label, sizeof(label)) != 0)
    str_copy_safe(label, id, sizeof(label));
  if (manifest_get_value(manifest, "shortcut", shortcut_name,
                         sizeof(shortcut_name)) != 0) {
    str_copy_safe(shortcut_name, label, sizeof(shortcut_name));
  }
  if (manifest_get_value(manifest, "kind", kind_buf, sizeof(kind_buf)) != 0)
    str_copy_safe(kind_buf, "terminal", sizeof(kind_buf));
  if (manifest_get_value(manifest, "default_dock", dock_buf, sizeof(dock_buf)) !=
      0)
    str_copy_safe(dock_buf, "0", sizeof(dock_buf));
  if (manifest_get_value(manifest, "store", store_buf, sizeof(store_buf)) != 0)
    str_copy_safe(store_buf, "1", sizeof(store_buf));

  fill_runtime_app(&app_catalog[app_catalog_count++], id, label, shortcut_name,
                   kind_from_string(kind_buf), dock_buf[0] == '1',
                   store_buf[0] == '1');
  return 0;
}

static int system_app_dir_scan(void *ctx, const char *name, int len,
                               loff_t offset, ino_t ino, unsigned type) {
  char path[128];
  int idx = 0;

  (void)ctx;
  (void)offset;
  (void)ino;
  (void)type;

  if ((len == 1 && name[0] == '.') ||
      (len == 2 && name[0] == '.' && name[1] == '.')) {
    return 0;
  }
  if (!str_ends_with_ci(name, ".app"))
    return 0;

  str_copy_safe(path, GUI_SYSTEM_APPS_DIR, sizeof(path));
  while (path[idx])
    idx++;
  path[idx++] = '/';
  for (int i = 0; i < len && idx < (int)sizeof(path) - 1; i++)
    path[idx++] = name[i];
  path[idx] = '\0';

  load_system_app_from_manifest(path);
  return 0;
}

static void load_system_app_catalog(void) {
  struct file *dir;

  if (app_catalog_loaded)
    return;

  app_catalog_loaded = 1;
  app_catalog_count = 0;
  ensure_gui_app_dirs();

  for (int i = 0; i < APP_CATALOG_SEED_COUNT; i++) {
    write_system_app_seed(&app_catalog_seed[i]);
    if (app_catalog_count < MAX_SYSTEM_APPS) {
      fill_runtime_app(&app_catalog[app_catalog_count++], app_catalog_seed[i].id,
                       app_catalog_seed[i].label,
                       app_catalog_seed[i].shortcut_name,
                       app_catalog_seed[i].kind,
                       app_catalog_seed[i].default_dock,
                       app_catalog_seed[i].visible_in_store);
    }
  }

  dir = vfs_open(GUI_SYSTEM_APPS_DIR, O_RDONLY, 0);
  if (dir) {
    vfs_readdir(dir, NULL, system_app_dir_scan);
    vfs_close(dir);
  }
}

static void ensure_app_manifest(const dock_app_def_t *app) {
  char manifest_path[128];
  char shortcut_path[128];
  char folder_shortcut_path[160];
  char manifest[128] = "id=";
  int idx = 3;

  if (!app)
    return;
  ensure_gui_app_dirs();

  for (int i = 0; app->id[i] && idx < (int)sizeof(manifest) - 2; i++) {
    manifest[idx++] = app->id[i];
  }
  manifest[idx++] = '\n';
  manifest[idx] = '\0';

  if (app_manifest_path(app, manifest_path, sizeof(manifest_path)) == 0) {
    write_text_file(manifest_path, manifest);
  }

  if (build_app_shortcut_path("/Desktop", app->shortcut_name, shortcut_path,
                              sizeof(shortcut_path)) == 0) {
    vfs_unlink(shortcut_path);
  }
  if (build_app_shortcut_path(GUI_SYSTEM_APPS_FOLDER, app->shortcut_name,
                              folder_shortcut_path,
                              sizeof(folder_shortcut_path)) == 0) {
    write_text_file(folder_shortcut_path, manifest);
  }
}

static void dock_add_item(const dock_app_def_t *app) {
  if (!app || dock_item_count >= MAX_DOCK_ITEMS)
    return;
  for (int i = 0; i < dock_item_count; i++) {
    if (dock_items[i] == app)
      return;
  }
  dock_items[dock_item_count++] = app;
}

static void dock_add_all_system_apps(void) {
  for (int i = 0; i < app_catalog_count; i++) {
    ensure_app_manifest(&app_catalog[i]);
    dock_add_item(&app_catalog[i]);
  }
}

static void dock_add_missing_preinstalled_apps(void) {
  for (int i = 0; i < app_catalog_count; i++) {
    const dock_app_def_t *app = &app_catalog[i];
    if (!app->default_dock)
      continue;
    ensure_app_manifest(app);
    dock_add_item(app);
  }
}

static void save_dock_config(void) {
  char buf[512];
  int idx = 0;
  ensure_gui_app_dirs();
  for (int i = 0; i < dock_item_count && idx < (int)sizeof(buf) - 2; i++) {
    const char *id = dock_items[i]->id;
    for (int j = 0; id[j] && idx < (int)sizeof(buf) - 2; j++) {
      buf[idx++] = id[j];
    }
    buf[idx++] = '\n';
  }
  buf[idx] = '\0';
  write_text_file(GUI_DOCK_CONFIG_PATH, buf);
}

static void load_dock_config(void) {
  char buf[512];
  int bytes = -1;

  if (dock_loaded)
    return;

  load_system_app_catalog();
  dock_loaded = 1;
  dock_item_count = 0;
  ensure_gui_app_dirs();

  for (int i = 0; i < app_catalog_count; i++) {
    ensure_app_manifest(&app_catalog[i]);
  }

  struct file *file = vfs_open(GUI_DOCK_CONFIG_PATH, O_RDONLY, 0);
  if (file) {
    bytes = (int)vfs_read(file, buf, sizeof(buf) - 1);
    vfs_close(file);
  }

  if (bytes <= 0) {
    save_dock_config();
    return;
  }

  buf[bytes] = '\0';
  int start = 0;
  for (int i = 0;; i++) {
    if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\0') {
      if (i > start) {
        char id[32];
        int len = i - start;
        if (len >= (int)sizeof(id))
          len = sizeof(id) - 1;
        for (int j = 0; j < len; j++) {
          id[j] = buf[start + j];
        }
        id[len] = '\0';
        dock_add_item(find_catalog_app(id));
      }
      if (buf[i] == '\0')
        break;
      start = i + 1;
    }
  }

  if (dock_item_count == 0)
    save_dock_config();
}

static int install_app(const dock_app_def_t *app, int pin_to_dock) {
  if (!app)
    return -1;
  ensure_app_manifest(app);
  if (pin_to_dock) {
    load_dock_config();
    dock_add_item(app);
    save_dock_config();
  }
  desktop_refresh();
  return 0;
}

int gui_launch_app_by_id(const char *app_id) {
  static int spawn_x = 100;
  static int spawn_y = 80;
  const dock_app_def_t *app = find_catalog_app(app_id);

  if (startup_flow_active())
    return -1;
  if (!app)
    return -1;

  switch (app->kind) {
  case GUI_APP_TERMINAL: {
    struct window *win =
        gui_create_window("Terminal", spawn_x, spawn_y, 450, 320);
    int content_x = spawn_x + BORDER_WIDTH;
    int content_y = spawn_y + BORDER_WIDTH + TITLEBAR_HEIGHT;
    struct terminal *term = term_create(content_x, content_y, 55, 16);
    if (win && term) {
      win->userdata = term;
      term_set_active(term);
      term_set_content_pos(term, content_x, content_y);
    }
    break;
  }
  case GUI_APP_FILES:
    gui_create_file_manager(spawn_x + 30, spawn_y + 20);
    break;
  case GUI_APP_CALCULATOR:
    gui_create_window("Calculator", spawn_x + 60, spawn_y + 40, 260, 380);
    break;
  case GUI_APP_NOTES:
    gui_open_notepad(NULL);
    break;
  case GUI_APP_SETTINGS:
    gui_create_window("Settings", spawn_x + 20, spawn_y + 30, 560, 420);
    break;
  case GUI_APP_CLOCK:
    gui_create_window("Clock", spawn_x + 50, spawn_y + 40, 260, 200);
    break;
  case GUI_APP_SNAKE:
    snake_init();
    gui_create_window("Snake", spawn_x + 70, spawn_y + 50, 340, 280);
    break;
  case GUI_APP_HELP:
    gui_create_window("Help", spawn_x + 120, spawn_y + 80, 350, 280);
    break;
  case GUI_APP_BROWSER:
    gui_create_window("Browser", spawn_x + 150, spawn_y + 90, 600, 450);
    break;
  case GUI_APP_APPSTORE:
    gui_create_window("App Store", spawn_x + 40, spawn_y + 30, 520, 390);
    break;
  }

  spawn_x = (spawn_x + 40) % 250 + 80;
  spawn_y = (spawn_y + 30) % 150 + 60;
  return 0;
}

static int gui_focus_or_launch_app_by_id(const char *app_id) {
  const dock_app_def_t *app = find_catalog_app(app_id);
  struct window *existing;

  if (!app)
    return -1;

  existing = find_window_for_app_kind(app->kind);
  if (existing) {
    existing->visible = true;
    if (existing->state == WINDOW_MINIMIZED)
      existing->state = WINDOW_NORMAL;
    gui_focus_window(existing);
    return 0;
  }

  return gui_launch_app_by_id(app_id);
}

static void draw_app_store(int content_x, int content_y, int content_w,
                           int content_h) {
  load_system_app_catalog();
  int y = content_y + 12;
  (void)content_h;

  gui_draw_string(content_x + 12, y, "App Store", 0xFFFFFF, THEME_BG);
  y += 18;
  gui_draw_string(content_x + 12, y,
                  "Install apps to create shortcuts and pin them to the dock.",
                  0xA6ADC8, THEME_BG);
  y += 24;

  for (int i = 0; i < app_catalog_count; i++) {
    const dock_app_def_t *app = &app_catalog[i];
    if (!app->visible_in_store)
      continue;

    uint32_t row_bg = 0x252535;
    int installed = app_is_installed(app);
    int button_w = installed ? 72 : 88;
    int button_x = content_x + content_w - button_w - 18;
    int row_w = content_w - 24;

    gui_draw_rect(content_x + 12, y, row_w, APP_STORE_CARD_HEIGHT, row_bg);
    gui_draw_rect(content_x + 24, y + 10, 34, 34, app->icon_color);

    draw_system_app_icon_kind(app->kind, content_x + 29, y + 15, 24);

    gui_draw_string(content_x + 70, y + 11, app->label, 0xFFFFFF, row_bg);
    gui_draw_string(content_x + 70, y + 29,
                    installed ? "Installed" : "Available to install",
                    installed ? 0xA6E3A1 : 0xA6ADC8, row_bg);

    gui_draw_rect(button_x, y + 13, button_w, 28,
                  installed ? 0x3B82F6 : 0x22C55E);
    gui_draw_string(button_x + (installed ? 16 : 14), y + 19,
                    installed ? "Open" : "Install", 0xFFFFFF,
                    installed ? 0x3B82F6 : 0x22C55E);

    y += APP_STORE_CARD_HEIGHT + 8;
  }
}

static void installer_set_status(const char *message) {
  str_copy_safe(installer_status, message, sizeof(installer_status));
}

typedef struct {
  const char *src_root;
  char dst_root[256];
  int copied_files;
  int failed_files;
} installer_copy_ctx_t;

static char installer_log_buffer[4096];
static char installer_log_target_root[256];

static void installer_append_to_buf(char *buf, int max, const char *text) {
  int idx = 0;

  if (!buf || max <= 0 || !text)
    return;
  while (buf[idx] && idx < max - 1)
    idx++;
  for (int i = 0; text[i] && idx < max - 1; i++)
    buf[idx++] = text[i];
  buf[idx] = '\0';
}

static void installer_log_clear(void) {
  installer_log_buffer[0] = '\0';
  installer_log_target_root[0] = '\0';
}

static void installer_log_append_path(const char *path, const char *line) {
  char existing[4096];
  int idx = 0;

  if (!path || !line || !path[0])
    return;

  existing[0] = '\0';
  read_text_file(path, existing, sizeof(existing));
  while (existing[idx] && idx < (int)sizeof(existing) - 1)
    idx++;
  for (int i = 0; line[i] && idx < (int)sizeof(existing) - 1; i++)
    existing[idx++] = line[i];
  if (idx < (int)sizeof(existing) - 1)
    existing[idx++] = '\n';
  existing[idx] = '\0';
  write_text_file(path, existing);
}

static void installer_log_send_to_host(const char *line) {
  if (!line)
    return;
  uart_puts("[INSTALL] ");
  uart_puts(line);
  uart_puts("\n");
}

static void installer_log(const char *line) {
  int idx = 0;

  if (!line)
    return;

  printk(KERN_INFO "INSTALL: %s\n", line);
  installer_log_send_to_host(line);
  while (installer_log_buffer[idx] && idx < (int)sizeof(installer_log_buffer) - 1)
    idx++;
  for (int i = 0; line[i] && idx < (int)sizeof(installer_log_buffer) - 2; i++)
    installer_log_buffer[idx++] = line[i];
  if (idx < (int)sizeof(installer_log_buffer) - 1)
    installer_log_buffer[idx++] = '\n';
  installer_log_buffer[idx] = '\0';

  installer_log_append_path("/System/install.log", line);
  if (installer_log_target_root[0]) {
    char target_log[320];
    str_copy_safe(target_log, installer_log_target_root, sizeof(target_log));
    installer_append_to_buf(target_log, sizeof(target_log), "/install.log");
    installer_log_append_path(target_log, line);
  }
}

static void installer_normalize_path(const char *src, char *dst, int max) {
  int idx = 0;

  if (!dst || max <= 0)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }

  while (src[idx] && idx < max - 1) {
    dst[idx] = src[idx];
    idx++;
  }
  dst[idx] = '\0';

  while (idx > 1 && dst[idx - 1] == '/') {
    dst[idx - 1] = '\0';
    idx--;
  }
}

static int installer_try_make_dir(const char *path) {
  struct file *existing;
  char normalized[256];
  int ret;

  if (!path || !path[0])
    return 0;
  installer_normalize_path(path, normalized, sizeof(normalized));
  if (!normalized[0])
    return 0;
  existing = vfs_open(normalized, O_RDONLY, 0);
  if (existing) {
    vfs_close(existing);
    return 0;
  }
  installer_ensure_parent_dirs(normalized);
  ret = vfs_mkdir(normalized, 0755);
  if (ret < 0) {
    char msg[320];
    str_copy_safe(msg, "mkdir failed: ", sizeof(msg));
    installer_append_to_buf(msg, sizeof(msg), normalized);
    installer_log(msg);
  }
  return ret;
}

static void installer_selected_disk_id(char *buf, int max) {
  const char *label = installer_selected_disk_label();
  int idx = 0;
  int inside = 0;

  if (!buf || max <= 1) {
    return;
  }

  buf[0] = '\0';
  for (int i = 0; label[i] && idx < max - 1; i++) {
    if (label[i] == '[') {
      inside = 1;
      continue;
    }
    if (label[i] == ']')
      break;
    if (inside)
      buf[idx++] = label[i];
  }
  buf[idx] = '\0';

  if (idx == 0) {
    str_copy_safe(buf, "disk0", max);
  }
}

static void installer_target_root_path(char *buf, int max) {
  char disk_id[32];
  installer_selected_disk_id(disk_id, sizeof(disk_id));
  str_copy_safe(buf, "/Installed", max);
  {
    int idx = 0;
    while (buf[idx] && idx < max - 1)
      idx++;
    if (idx < max - 1)
      buf[idx++] = '/';
    for (int i = 0; disk_id[i] && idx < max - 1; i++)
      buf[idx++] = disk_id[i];
    buf[idx] = '\0';
  }
}

static void installer_partition_root_path(char *buf, int max,
                                          const char *partition_name) {
  installer_target_root_path(buf, max);
  if (!partition_name || !partition_name[0])
    return;
  installer_append_to_buf(buf, max, "/");
  installer_append_to_buf(buf, max, partition_name);
}

static void installer_ensure_parent_dirs(const char *path) {
  char partial[256];
  int idx = 0;
  int last_non_slash = 0;

  if (!path)
    return;

  while (path[last_non_slash])
    last_non_slash++;
  while (last_non_slash > 1 && path[last_non_slash - 1] == '/')
    last_non_slash--;

  for (int i = 0; path[i] && idx < (int)sizeof(partial) - 1; i++) {
    partial[idx++] = path[i];
    partial[idx] = '\0';
    if (i > 0 && path[i] == '/' && i < last_non_slash - 1)
      installer_try_make_dir(partial);
  }
}

static int installer_copy_file(const char *src_path, const char *dst_path) {
  uint8_t *data = NULL;
  size_t size = 0;
  char msg[320];
  int ret;

  if (media_load_file(src_path, &data, &size) != 0) {
    str_copy_safe(msg, "read failed: ", sizeof(msg));
    installer_append_to_buf(msg, sizeof(msg), src_path);
    installer_log(msg);
    return -1;
  }

  installer_ensure_parent_dirs(dst_path);
  ret = installer_write_target_file(dst_path, data, size);
  if (ret != 0 && size == 0) {
    struct file *dst = vfs_open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst) {
      vfs_close(dst);
      ret = 0;
    }
  }
  if (ret != 0) {
    str_copy_safe(msg, "write failed: ", sizeof(msg));
    installer_append_to_buf(msg, sizeof(msg), dst_path);
    installer_log(msg);
    media_free_file(data);
    return -1;
  }

  str_copy_safe(msg, "copied ", sizeof(msg));
  installer_append_to_buf(msg, sizeof(msg), src_path);
  installer_append_to_buf(msg, sizeof(msg), " -> ");
  installer_append_to_buf(msg, sizeof(msg), dst_path);
  installer_log(msg);
  media_free_file(data);
  return 0;
}

static int installer_copy_tree_callback(void *ctx, const char *name, int len,
                                        loff_t offset, ino_t ino,
                                        unsigned type) {
  installer_copy_ctx_t *copy = (installer_copy_ctx_t *)ctx;
  char src_path[256];
  char dst_path[256];
  int src_len = 0;
  int dst_len = 0;

  (void)offset;
  (void)ino;

  if (!copy || !name || len <= 0)
    return 0;
  if ((len == 1 && name[0] == '.') ||
      (len == 2 && name[0] == '.' && name[1] == '.'))
    return 0;
  if (len == 14 && name[0] == 'I' && name[1] == 'M' && name[2] == 'A' &&
      name[3] == 'G' && name[4] == 'E' && name[5] == '_' &&
      name[6] == 'I' && name[7] == 'N' && name[8] == 'F' &&
      name[9] == 'O' && name[10] == '.' && name[11] == 't' &&
      name[12] == 'x' && name[13] == 't')
    return 0;

  str_copy_safe(src_path, copy->src_root, sizeof(src_path));
  while (src_path[src_len])
    src_len++;
  if (src_len < (int)sizeof(src_path) - 1)
    src_path[src_len++] = '/';
  for (int i = 0; i < len && src_len < (int)sizeof(src_path) - 1; i++)
    src_path[src_len++] = name[i];
  src_path[src_len] = '\0';

  str_copy_safe(dst_path, copy->dst_root, sizeof(dst_path));
  while (dst_path[dst_len])
    dst_len++;
  if (dst_len < (int)sizeof(dst_path) - 1)
    dst_path[dst_len++] = '/';
  for (int i = 0; i < len && dst_len < (int)sizeof(dst_path) - 1; i++)
    dst_path[dst_len++] = name[i];
  dst_path[dst_len] = '\0';

  if (type == 4) {
    if (installer_try_make_dir(dst_path) != 0)
      copy->failed_files++;
    {
      struct file *dir = vfs_open(src_path, O_RDONLY, 0);
      if (dir) {
        installer_copy_ctx_t next = {src_path, "", copy->copied_files,
                                     copy->failed_files};
        str_copy_safe(next.dst_root, dst_path, sizeof(next.dst_root));
        vfs_readdir(dir, &next, installer_copy_tree_callback);
        copy->copied_files = next.copied_files;
        copy->failed_files = next.failed_files;
        vfs_close(dir);
      } else {
        char msg[320];
        str_copy_safe(msg, "open dir failed: ", sizeof(msg));
        installer_append_to_buf(msg, sizeof(msg), src_path);
        installer_log(msg);
        copy->failed_files++;
      }
    }
    return 0;
  }

  if (installer_copy_file(src_path, dst_path) == 0)
    copy->copied_files++;
  else
    copy->failed_files++;
  return 0;
}

static int installer_copy_system_image_to_root(const char *target_root,
                                               int *copied_files,
                                               int *failed_files) {
  static const char *installer_system_image_root = "/install/system-image";
  installer_copy_ctx_t ctx = {"/install/system-image", "", 0, 0};
  struct file *dir;
  char msg[320];

  if (!target_root || !target_root[0])
    return -1;

  ctx.src_root = installer_system_image_root;
  str_copy_safe(ctx.dst_root, target_root, sizeof(ctx.dst_root));
  if (installer_try_make_dir(target_root) != 0) {
    str_copy_safe(msg, "install failed: target root creation failed for ",
                  sizeof(msg));
    installer_append_to_buf(msg, sizeof(msg), target_root);
    installer_log(msg);
    return -1;
  }

  dir = vfs_open(ctx.src_root, O_RDONLY, 0);
  if (!dir) {
    installer_log("install failed: source payload missing");
    return -1;
  }

  str_copy_safe(msg, "copying system image from ", sizeof(msg));
  installer_append_to_buf(msg, sizeof(msg), installer_system_image_root);
  installer_append_to_buf(msg, sizeof(msg), " to ");
  installer_append_to_buf(msg, sizeof(msg), target_root);
  installer_log(msg);
  vfs_readdir(dir, &ctx, installer_copy_tree_callback);
  vfs_close(dir);

  if (copied_files)
    *copied_files += ctx.copied_files;
  if (failed_files)
    *failed_files += ctx.failed_files;
  return (ctx.copied_files > 0 && ctx.failed_files == 0) ? 0 : -1;
}

static int installer_payload_file_exists(const char *path) {
  struct file *f;

  if (!path || !path[0])
    return 0;
  f = vfs_open(path, O_RDONLY, 0);
  if (!f)
    return 0;
  vfs_close(f);
  return 1;
}

static int installer_payload_any_file_exists(const char **paths, int count) {
  if (!paths || count <= 0)
    return 0;

  for (int i = 0; i < count; i++) {
    if (installer_payload_file_exists(paths[i]))
      return 1;
  }
  return 0;
}

static int installer_validate_system_image_payload(void) {
  static const char *required_paths[] = {
      "/install/system-image/boot/main.sys",
      "/install/system-image/boot/bootloader.sys",
      "/install/system-image/boot/limine-bios.sys",
      "/install/system-image/boot/limine-bios-cd.bin",
      "/install/system-image/boot/limine-uefi-cd.bin",
      "/install/system-image/EFI/BOOT/BOOTX64.EFI",
  };
  static const char *limine_cfg_paths[] = {
      "/install/system-image/limine.conf",
      "/install/system-image/boot/limine.conf",
      "/install/system-image/limine/limine.conf",
      "/install/system-image/EFI/BOOT/limine.conf",
  };
  char msg[320];

  for (int i = 0; i < (int)(sizeof(required_paths) / sizeof(required_paths[0]));
       i++) {
    if (installer_payload_file_exists(required_paths[i]))
      continue;
    str_copy_safe(msg, "install payload missing: ", sizeof(msg));
    installer_append_to_buf(msg, sizeof(msg), required_paths[i]);
    installer_log(msg);
    return -1;
  }

  if (!installer_payload_any_file_exists(
          limine_cfg_paths,
          (int)(sizeof(limine_cfg_paths) / sizeof(limine_cfg_paths[0])))) {
    str_copy_safe(msg, "install payload missing: no Limine config in system image",
                  sizeof(msg));
    installer_log(msg);
    return -1;
  }

  return 0;
}

static const char *installer_system_disk_image_path(void) {
  static const char *paths[] = {
      "/install/system-image/dos/OSSYS.IMG",
      "/setup/install/system-image/dos/OSSYS.IMG",
      "/dos/OSSYS.IMG",
      "/setup/dos/OSSYS.IMG",
      "/External/cd0/dos/OSSYS.IMG",
      "/External/cd1/dos/OSSYS.IMG",
      "/External/cd2/dos/OSSYS.IMG",
      "/External/cd3/dos/OSSYS.IMG",
      "/Media/cd0/dos/OSSYS.IMG",
      "/Media/cd1/dos/OSSYS.IMG",
      "/Media/cd2/dos/OSSYS.IMG",
      "/Media/cd3/dos/OSSYS.IMG",
  };

  for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
    if (installer_payload_file_exists(paths[i]))
      return paths[i];
  }

  return NULL;
}

static int installer_write_raw_system_image_to_disk(int disk_index) {
  const char *image_path;
  uint8_t *data = NULL;
  size_t size = 0;
  char msg[320];

  extern int storage_write_disk_image(int disk_index, const uint8_t *data,
                                      size_t size);

  image_path = installer_system_disk_image_path();
  if (!image_path) {
    installer_log("install failed: raw system disk image missing");
    return -1;
  }

  if (media_load_file(image_path, &data, &size) != 0) {
    str_copy_safe(msg, "install failed: could not read ", sizeof(msg));
    installer_append_to_buf(msg, sizeof(msg), image_path);
    installer_log(msg);
    return -1;
  }

  if (size < 512 || data[510] != 0x55 || data[511] != 0xAA) {
    installer_log("install failed: raw system image is not BIOS bootable");
    media_free_file(data);
    return -1;
  }

  str_copy_safe(msg, "writing raw system image from ", sizeof(msg));
  installer_append_to_buf(msg, sizeof(msg), image_path);
  installer_log(msg);

  if (storage_write_disk_image(disk_index, data, size) != 0) {
    installer_log("install failed: raw disk image write failed");
    media_free_file(data);
    return -1;
  }

  media_free_file(data);
  installer_log("raw system image written to target disk");
  return 0;
}

static int installer_reboot_seconds_remaining(void) {
  uint64_t now;
  uint64_t remaining_ms;

  if (!installer_reboot_deadline_ms)
    return 0;

  now = arch_timer_get_ms();
  if (now >= installer_reboot_deadline_ms)
    return 0;

  remaining_ms = installer_reboot_deadline_ms - now;
  return (int)((remaining_ms + 999) / 1000);
}

static int installer_finalize_install(void) {
  char summary[96];
  const char *installer_state =
      "installed=1\nprofile=system-image\nsource=installer-iso\n"
      "first_boot_setup=1\n";
  int user_partition_result = 0;

  extern int storage_prepare_user_partition(int disk_index);

  dock_loaded = 0;
  load_dock_config();
  desktop_refresh();
  installer_write_target_config();

  user_partition_result =
      storage_prepare_user_partition(installer_selected_disk);
  if (user_partition_result > 0) {
    installer_log("created HDD user data partition for first boot");
  } else if (user_partition_result == 0) {
    installer_log("user data partition already present or not required");
  } else {
    installer_log("warning: could not prepare HDD user data partition");
  }

  write_text_file("/System/installer-state.txt", installer_state);

  summary[0] = '\0';
  str_copy_safe(summary, "Installed system image; first boot will run setup",
                sizeof(summary));
  installer_log("install complete");
  installer_log("selected hard disk now contains the bootable system image");
  installer_log("first boot will prompt for account creation");
  installer_set_status(summary);
  installer_log("reboot scheduled in 3 seconds");
  installer_has_run = 1;
  installer_show_restart_screen = 1;
  installer_active = 0;
  installer_phase = 0;
  installer_reboot_deadline_ms = arch_timer_get_ms() + 3000;
  return 0;
}

static void installer_start_background_install(void) {
  installer_log_clear();
  installer_refresh_disk_inventory();
  installer_has_run = 0;
  installer_show_restart_screen = 0;
  installer_active = 1;
  installer_phase = 1;
  installer_progress_done = 0;
  installer_progress_total = 4;
  installer_copied_files = 0;
  installer_failed_files = 0;
  installer_ensured_changes = 0;
  installer_reboot_deadline_ms = 0;
  installer_target_root[0] = '\0';
  installer_efi_root[0] = '\0';
  installer_update_root[0] = '\0';
  installer_set_status("Preparing install...");
  installer_log("starting system image install");
}

static void installer_fail_background(const char *status, const char *log_line) {
  installer_active = 0;
  installer_phase = 0;
  installer_show_restart_screen = 0;
  installer_set_status(status);
  if (log_line)
    installer_log(log_line);
}

static void installer_process_background_install(void) {
  if (installer_reboot_deadline_ms &&
      arch_timer_get_ms() >= installer_reboot_deadline_ms) {
    installer_reboot_deadline_ms = 0;
    {
      extern void arch_reboot(void);
      arch_reboot();
    }
    return;
  }

  if (!installer_active)
    return;

  switch (installer_phase) {
  case 1:
    installer_refresh_disk_inventory();
    if (installer_disk_count <= 0) {
      installer_fail_background("Install blocked. No real target disk is available.",
                                "install blocked: no real target disk");
      return;
    }
    installer_set_status("Checking target disk...");
    installer_progress_done = 1;
    installer_phase = 2;
    return;
  case 2: {
    extern int storage_disk_supports_partition_writes(int disk_index);
    if (!storage_disk_supports_partition_writes(installer_selected_disk)) {
      installer_fail_background("Install blocked. Target disk is not writable.",
                                "install blocked: target disk is not writable");
      return;
    }
    installer_target_root_path(installer_target_root,
                               sizeof(installer_target_root));
    installer_partition_root_path(installer_efi_root, sizeof(installer_efi_root),
                                  "EFI");
    installer_partition_root_path(installer_update_root,
                                  sizeof(installer_update_root), "boot");
    str_copy_safe(installer_log_target_root, installer_target_root,
                  sizeof(installer_log_target_root));
    if (installer_validate_system_image_payload() != 0) {
      installer_fail_background("Install blocked. Boot files are missing from the installer image.",
                                "install blocked: boot payload incomplete");
      return;
    }
    if (!installer_system_disk_image_path()) {
      installer_fail_background("Install blocked. Raw disk image is missing from the installer media.",
                                "install blocked: raw disk image missing");
      return;
    }
    installer_set_status("Writing system image to disk...");
    installer_progress_done = 2;
    installer_phase = 3;
    return;
  }
  case 3:
    if (installer_write_raw_system_image_to_disk(installer_selected_disk) != 0) {
      installer_fail_background("Install failed. Raw system image write failed.",
                                "install failed: raw system image write failed");
      return;
    }
    installer_copied_files = 1;
    installer_set_status("Finalizing install...");
    installer_progress_done = 3;
    installer_phase = 4;
    return;
  case 4:
    installer_progress_done = 4;
    if (installer_finalize_install() != 0) {
      installer_fail_background("Install failed during finalization.",
                                "install failed: finalization failed");
    }
    return;
  default:
    return;
  }
}

static void draw_installer_window(int content_x, int content_y, int content_w,
                                  int content_h) {
  installer_refresh_disk_inventory();
  int card_x = content_x + 24;
  int card_y = content_y + 22;
  int card_w = content_w - 48;
  int button_w = 180;
  int manage_w = 150;
  int button_h = 34;
  int button_x = content_x + 24;
  int manage_x = button_x + button_w + 12;
  int button_y = content_y + content_h - 64;
  uint32_t button_bg =
      installer_active ? 0x2563EB : (installer_has_run ? 0x4B5563 : 0x16A34A);
  const char *action_label =
      installer_active ? "Installing..." : "Install System Image";
  int disk_y = card_y + 102;
  int progress_y = card_y + 318;
  int progress_w = card_w - 36;
  int fill_w = installer_progress_total > 0
                   ? (progress_w * installer_progress_done) /
                         installer_progress_total
                   : 0;

  if (installer_show_restart_screen) {
    char countdown[96];
    int seconds = installer_reboot_seconds_remaining();
    int restart_x = content_x + 24;
    int restart_y = content_y + content_h - 64;
    int restart_w = 180;
    int restart_h = 34;

    gui_draw_rect(card_x, card_y, card_w, content_h - 110, 0x232337);
    gui_draw_rect(card_x + 18, card_y + 18, card_w - 36, 72, 0x123B2A);
    gui_draw_string(card_x + 34, card_y + 34, "Installation Complete",
                    0xFFFFFF, 0x123B2A);
    gui_draw_string(card_x + 34, card_y + 58,
                    "The system image is installed and ready to boot.",
                    0xD1FAE5, 0x123B2A);

    gui_draw_string(card_x + 18, card_y + 118, "Next step:", 0x89B4FA,
                    0x232337);
    gui_draw_string(card_x + 30, card_y + 142,
                    "Restart to boot from the installed disk.", 0xE5E7EB,
                    0x232337);
    gui_draw_string(card_x + 30, card_y + 162,
                    "Remove the installer media if your VM or machine keeps",
                    0xE5E7EB, 0x232337);
    gui_draw_string(card_x + 30, card_y + 182,
                    "booting back into setup after restart.", 0xE5E7EB,
                    0x232337);

    gui_draw_string(card_x + 18, card_y + 222, "Status:", 0x89B4FA, 0x232337);
    gui_draw_rect(card_x + 18, card_y + 242, card_w - 36, 34, 0x1B1B2B);
    gui_draw_string(card_x + 28, card_y + 253, installer_status, 0xFFFFFF,
                    0x1B1B2B);

    str_copy_safe(countdown, "Automatic restart in ", sizeof(countdown));
    {
      int idx = 0;
      while (countdown[idx] && idx < (int)sizeof(countdown) - 1)
        idx++;
      append_decimal(countdown, &idx, seconds > 0 ? seconds : 0);
      installer_append_to_buf(countdown, sizeof(countdown), " seconds...");
    }
    gui_draw_string(card_x + 18, card_y + 298, countdown, 0xA6E3A1, 0x232337);

    gui_draw_rect(restart_x, restart_y, restart_w, restart_h, 0x16A34A);
    gui_draw_string(restart_x + 42, restart_y + 10, "Restart Now", 0xFFFFFF,
                    0x16A34A);
    return;
  }

  gui_draw_rect(card_x, card_y, card_w, content_h - 110, 0x232337);
  gui_draw_string(card_x + 18, card_y + 18, "OS8 Installer",
                  0xFFFFFF, 0x232337);
  gui_draw_string(card_x + 18, card_y + 42,
                  "This ISO boots directly into the installer environment.",
                  0xCDD6F4, 0x232337);
  gui_draw_string(card_x + 18, card_y + 66,
                  "It carries a bundled bootable system image payload.",
                  0xA6ADC8, 0x232337);
  gui_draw_string(card_x + 18, disk_y, "Target disk:", 0x89B4FA, 0x232337);
  for (int i = 0; i < installer_disk_count && i < 3; i++) {
    int row_y = disk_y + 18 + i * 26;
    uint32_t row_bg = i == installer_selected_disk ? 0x334155 : 0x1B1B2B;
    gui_draw_rect(card_x + 18, row_y, card_w - 36, 22, row_bg);
    gui_draw_string(card_x + 28, row_y + 4, installer_disk_labels[i], 0xFFFFFF,
                    row_bg);
  }
  gui_draw_string(card_x + 18, disk_y + 104, "Install actions:", 0x89B4FA,
                  0x232337);
  gui_draw_string(card_x + 30, disk_y + 126,
                  "- overwrites the selected hard disk", 0xE5E7EB, 0x232337);
  gui_draw_string(card_x + 30, disk_y + 146,
                  "- writes the bundled raw system image", 0xE5E7EB, 0x232337);
  gui_draw_string(card_x + 30, disk_y + 166,
                  "- installs BIOS+UEFI boot data from that image", 0xE5E7EB,
                  0x232337);
  gui_draw_string(card_x + 30, disk_y + 186,
                  "- prepares a user data partition on the HDD", 0xE5E7EB,
                  0x232337);
  gui_draw_string(card_x + 30, disk_y + 206,
                  "- runs first-boot account setup after restart", 0xE5E7EB,
                  0x232337);
  gui_draw_string(card_x + 18, card_y + 254, "Status:", 0x89B4FA, 0x232337);
  gui_draw_rect(card_x + 18, card_y + 274, card_w - 36, 34, 0x1B1B2B);
  gui_draw_string(card_x + 28, card_y + 285, installer_status, 0xFFFFFF,
                  0x1B1B2B);
  gui_draw_string(card_x + 18, progress_y - 18, "Progress:", 0x89B4FA,
                  0x232337);
  gui_draw_rect(card_x + 18, progress_y, progress_w, 14, 0x1B1B2B);
  if (fill_w > 0)
    gui_draw_rect(card_x + 18, progress_y, fill_w, 14, 0x22C55E);

  gui_draw_rect(button_x, button_y, button_w, button_h, button_bg);
  gui_draw_string(button_x + 24, button_y + 10,
                  installer_has_run ? "Install Complete" : action_label,
                  0xFFFFFF, button_bg);
  gui_draw_rect(manage_x, button_y, manage_w, button_h, 0x2563EB);
  gui_draw_string(manage_x + 18, button_y + 10, "Partition Manager",
                  0xFFFFFF, 0x2563EB);
}

static void draw_startup_auth_window(struct window *win, int content_x,
                                     int content_y, int content_w,
                                     int content_h) {
  char masked_password[32];
  uint32_t user_bg = startup_active_field == 0 ? 0x31314A : 0x232337;
  uint32_t pass_bg = startup_active_field == 1 ? 0x31314A : 0x232337;
  uint32_t button_bg = 0x2563EB;
  const char *title =
      startup_flow == STARTUP_FLOW_SETUP_ACCOUNT ? "Setup Account"
                                                  : "Sign In";
  const char *button_label =
      startup_flow == STARTUP_FLOW_SETUP_ACCOUNT ? "Finish Setup" : "Login";

  (void)win;
  if (startup_setup_account_active()) {
    int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
    int rail_w = 0, card_x = 0, card_y = 0, card_w = 0, card_h = 0;
    int button_x = 0, button_y = 0, button_w = 0, button_h = 0;
    int user_x = 0, user_y = 0, user_w = 0, user_h = 0;
    int pass_x = 0, pass_y = 0, pass_w = 0, pass_h = 0;
    const char *step_title = startup_setup_welcome_active()
                                 ? "Welcome"
                                 : startup_setup_storage_active()
                                       ? "Create Storage"
                                       : "Create Account";
    const char *headline = startup_setup_welcome_active()
                               ? "Set up your OS8 account"
                               : startup_setup_storage_active()
                                     ? "Finish the storage setup"
                                     : "Create the owner account";
    const char *body_1 = startup_setup_welcome_active()
                             ? "This machine is not ready for sign-in yet."
                             : startup_setup_storage_active()
                                   ? "A private data area will be prepared for"
                                   : "Choose the username and password that";
    const char *body_2 = startup_setup_welcome_active()
                             ? "Setup runs in a locked screen with no extra"
                             : startup_setup_storage_active()
                                   ? account_username
                                   : "will be used every time this system boots.";
    const char *body_3 = startup_setup_welcome_active()
                             ? "windows, apps, or desktop menus available."
                             : startup_setup_storage_active()
                                   ? "The account partition keeps user data"
                                   : "The dock and background apps stay off";
    const char *body_4 = startup_setup_welcome_active()
                             ? "Continue to begin account creation."
                             : startup_setup_storage_active()
                                   ? "isolated on the HDD for later boots."
                                   : "until setup is finished.";
    const char *button_text = startup_setup_welcome_active()
                                  ? "Start Setup"
                                  : startup_setup_storage_active()
                                        ? "Create Storage"
                                        : "Continue";

    startup_get_setup_layout(content_x, content_y, content_w, content_h,
                             &panel_x, &panel_y, &panel_w, &panel_h, &rail_w,
                             &card_x, &card_y, &card_w, &card_h);
    startup_get_setup_button_rect(content_x, content_y, content_w, content_h,
                                  &button_x, &button_y, &button_w, &button_h);
    startup_get_setup_field_rect(content_x, content_y, content_w, content_h, 0,
                                 &user_x, &user_y, &user_w, &user_h);
    startup_get_setup_field_rect(content_x, content_y, content_w, content_h, 1,
                                 &pass_x, &pass_y, &pass_w, &pass_h);
    mask_secret(startup_input_password, masked_password, sizeof(masked_password));

    gui_draw_rect(content_x, content_y, content_w, content_h, 0x0B1020);
    gui_fill_rect_alpha(panel_x, panel_y, panel_w, panel_h, 0x10203CC8);
    gui_draw_rect_outline(panel_x, panel_y, panel_w, panel_h, 0x3B82F6, 1);

    gui_fill_rect_alpha(panel_x, panel_y, rail_w, panel_h, 0x0C1730CC);
    gui_draw_rect(panel_x + 24, panel_y + 28, 56, 56, 0x2563EB);
    gui_draw_string(panel_x + 100, panel_y + 36, "OS8 Setup", 0xFFFFFF,
                    0x0C1730);
    gui_draw_string(panel_x + 100, panel_y + 58, "Locked setup workspace",
                    0xBFDBFE, 0x0C1730);

    gui_draw_string(panel_x + 24, panel_y + 118, "Progress", 0x93C5FD,
                    0x0C1730);
    gui_draw_rect(panel_x + 24, panel_y + 146, rail_w - 48, 44,
                  startup_setup_welcome_active() ? 0x1D4ED8 : 0x15233D);
    gui_draw_string(panel_x + 40, panel_y + 160, "1  Welcome", 0xFFFFFF,
                    startup_setup_welcome_active() ? 0x1D4ED8 : 0x15233D);
    gui_draw_rect(panel_x + 24, panel_y + 198, rail_w - 48, 44,
                  startup_setup_account_form_active() ? 0x1D4ED8 : 0x15233D);
    gui_draw_string(panel_x + 40, panel_y + 212, "2  Account", 0xFFFFFF,
                    startup_setup_account_form_active() ? 0x1D4ED8 : 0x15233D);
    gui_draw_rect(panel_x + 24, panel_y + 250, rail_w - 48, 44,
                  startup_setup_storage_active() ? 0x1D4ED8 : 0x15233D);
    gui_draw_string(panel_x + 40, panel_y + 264, "3  Storage", 0xFFFFFF,
                    startup_setup_storage_active() ? 0x1D4ED8 : 0x15233D);

    gui_draw_string(panel_x + 24, panel_y + panel_h - 118, "Restrictions",
                    0x93C5FD, 0x0C1730);
    gui_draw_string(panel_x + 24, panel_y + panel_h - 90,
                    "Only this setup window is available.", 0xE2E8F0,
                    0x0C1730);
    gui_draw_string(panel_x + 24, panel_y + panel_h - 64,
                    "Right-click desktop menus stay disabled.", 0xE2E8F0,
                    0x0C1730);
    gui_draw_string(panel_x + 24, panel_y + panel_h - 38,
                    "Changes are saved to disk for later boots.", 0xE2E8F0,
                    0x0C1730);

    gui_fill_rect_alpha(card_x, card_y, card_w, card_h, 0x111827E4);
    gui_draw_rect_outline(card_x, card_y, card_w, card_h, 0x334155, 1);
    gui_draw_string(card_x + 36, card_y + 34, step_title, 0x93C5FD, 0x111827);
    gui_draw_string(card_x + 36, card_y + 64, headline, 0xFFFFFF, 0x111827);
    gui_draw_string(card_x + 36, card_y + 98, body_1, 0xCBD5E1, 0x111827);
    gui_draw_string(card_x + 36, card_y + 122, body_2, 0xFFFFFF, 0x111827);
    gui_draw_string(card_x + 36, card_y + 146, body_3, 0xCBD5E1, 0x111827);
    gui_draw_string(card_x + 36, card_y + 170, body_4, 0xCBD5E1, 0x111827);

    if (startup_setup_account_form_active()) {
      gui_draw_string(user_x, user_y - 22, "Username", 0x93C5FD, 0x111827);
      gui_draw_rect(user_x, user_y, user_w, user_h, user_bg);
      gui_draw_string(user_x + 12, user_y + 14,
                      startup_input_username[0] ? startup_input_username
                                                : "enter username",
                      startup_input_username[0] ? 0xFFFFFF : 0x64748B, user_bg);

      gui_draw_string(pass_x, pass_y - 22, "Password", 0x93C5FD, 0x111827);
      gui_draw_rect(pass_x, pass_y, pass_w, pass_h, pass_bg);
      gui_draw_string(pass_x + 12, pass_y + 14,
                      masked_password[0] ? masked_password : "enter password",
                      masked_password[0] ? 0xFFFFFF : 0x64748B, pass_bg);
    }

    gui_draw_rect(button_x, button_y, button_w, button_h, 0x2563EB);
    gui_draw_string(button_x + 22, button_y + 14, button_text, 0xFFFFFF,
                    0x2563EB);
    gui_draw_string(button_x + button_w + 18, button_y + 14, startup_status,
                    0xCBD5E1, 0x111827);
    return;
  }

  if (startup_setup_welcome_active()) {
    gui_draw_rect(content_x, content_y, content_w, 56, 0x181827);
    gui_draw_string(content_x + 20, content_y + 18, "Welcome to OS8",
                    0xFFFFFF, 0x181827);
    gui_draw_string(content_x + 20, content_y + 78,
                    "This setup will create your account before login.",
                    0xCDD6F4, THEME_BG);
    gui_draw_string(content_x + 20, content_y + 104,
                    "No other apps will open until setup is done.", 0xA6ADC8,
                    THEME_BG);
    gui_draw_string(content_x + 20, content_y + 130,
                    "Press Enter or click Continue to start.", 0xA6ADC8,
                    THEME_BG);
    gui_draw_rect(content_x + 20, content_y + 198, 170, 34, 0x2563EB);
    gui_draw_string(content_x + 56, content_y + 210, "Continue", 0xFFFFFF,
                    0x2563EB);
    gui_draw_string(content_x + 210, content_y + 209, startup_status, 0xCDD6F4,
                    THEME_BG);
    return;
  }

  if (startup_setup_storage_active()) {
    gui_draw_rect(content_x, content_y, content_w, 56, 0x181827);
    gui_draw_string(content_x + 20, content_y + 18, "Prepare Account Storage",
                    0xFFFFFF, 0x181827);
    gui_draw_string(content_x + 20, content_y + 78,
                    "First boot will create a personal data partition for",
                    0xCDD6F4, THEME_BG);
    gui_draw_string(content_x + 20, content_y + 102, account_username,
                    0xFFFFFF, THEME_BG);
    gui_draw_string(content_x + 20, content_y + 128,
                    "This keeps each account separated on the HDD.", 0xA6ADC8,
                    THEME_BG);
    gui_draw_string(content_x + 20, content_y + 154,
                    "Press Enter or click Continue to provision storage.",
                    0xA6ADC8, THEME_BG);
    gui_draw_rect(content_x + 20, content_y + 204, 170, 34, 0x2563EB);
    gui_draw_string(content_x + 56, content_y + 216, "Continue", 0xFFFFFF,
                    0x2563EB);
    gui_draw_string(content_x + 210, content_y + 215, startup_status, 0xCDD6F4,
                    THEME_BG);
    return;
  }

  mask_secret(startup_input_password, masked_password, sizeof(masked_password));

  gui_draw_rect(content_x, content_y, content_w, 56, 0x181827);
  gui_draw_string(content_x + 20, content_y + 18, title, 0xFFFFFF, 0x181827);

  gui_draw_string(content_x + 20, content_y + 60,
                  startup_setup_account_active()
                      ? "Setup account username"
                      : "Username",
                  0xA6ADC8,
                  THEME_BG);
  gui_draw_rect(content_x + 20, content_y + 80, content_w - 40, 34, user_bg);
  gui_draw_string(content_x + 30, content_y + 92,
                  startup_input_username[0] ? startup_input_username
                                            : "enter username",
                  startup_input_username[0] ? 0xFFFFFF : 0x6C7086, user_bg);

  gui_draw_string(content_x + 20, content_y + 128,
                  startup_setup_account_active()
                      ? "Setup account password"
                      : "Password",
                  0xA6ADC8,
                  THEME_BG);
  gui_draw_rect(content_x + 20, content_y + 148, content_w - 40, 34, pass_bg);
  gui_draw_string(content_x + 30, content_y + 160,
                  masked_password[0] ? masked_password : "enter password",
                  masked_password[0] ? 0xFFFFFF : 0x6C7086, pass_bg);

  gui_draw_rect(content_x + 20, content_y + 204, 170, 34, button_bg);
  gui_draw_string(content_x + 34, content_y + 216, button_label, 0xFFFFFF,
                  button_bg);
  gui_draw_string(content_x + 210, content_y + 215, startup_status, 0xCDD6F4,
                  THEME_BG);
}

static void draw_icon(int x, int y, int size, const unsigned char *icon,
                      uint32_t fg_color, uint32_t bg_color);

struct fm_state {
  char path[256];
  char selected[256];
  int scroll_y;
  int context_menu_visible;
  int context_menu_x;
  int context_menu_y;
  int context_menu_target_type;
  int context_menu_target_on_item;
  char context_menu_target[256];
};

#define FM_MAX_ITEMS 96

struct fm_item {
  char name[64];
  unsigned type;
};

struct fm_collect_ctx {
  struct fm_item *items;
  int count;
  int max_items;
};

static int fm_name_length(const char *name) {
  int len = 0;
  while (name && name[len])
    len++;
  return len;
}

static void fm_set_window_title(struct window *win, const char *path) {
  if (!win)
    return;

  str_copy_safe(win->title, "File Manager", sizeof(win->title));
  if (!path || !path[0])
    return;

  int idx = 12;
  if (idx < (int)sizeof(win->title) - 1) {
    win->title[idx++] = ' ';
    win->title[idx++] = '-';
    win->title[idx++] = ' ';
  }

  for (int i = 0; path[i] && idx < (int)sizeof(win->title) - 1; i++)
    win->title[idx++] = path[i];
  win->title[idx] = '\0';
}

static void fm_join_path(const char *base, const char *name, char *out,
                         int out_max) {
  int idx = 0;
  if (!out || out_max <= 0)
    return;
  out[0] = '\0';

  if (base) {
    while (base[idx] && idx < out_max - 1) {
      out[idx] = base[idx];
      idx++;
    }
  }

  if (idx == 0) {
    out[idx++] = '/';
  } else if (out[idx - 1] != '/' && idx < out_max - 1) {
    out[idx++] = '/';
  }

  for (int i = 0; name && name[i] && idx < out_max - 1; i++)
    out[idx++] = name[i];
  out[idx] = '\0';
}

static int fm_path_exists(const char *path) {
  char resolved_path[256];
  const char *open_path =
      resolve_user_storage_path(path, resolved_path, sizeof(resolved_path));
  struct file *f = vfs_open(open_path, O_RDONLY, 0);
  if (!f)
    return 0;
  vfs_close(f);
  return 1;
}

static void fm_build_unique_child_path(const char *dir_path, const char *base,
                                       const char *ext, char *out,
                                       int out_max) {
  char candidate[512];
  char name[96];

  for (int attempt = 0; attempt < 32; attempt++) {
    int idx = 0;
    const char *prefix = base ? base : "New Item";
    for (int i = 0; prefix[i] && idx < (int)sizeof(name) - 1; i++)
      name[idx++] = prefix[i];

    if (attempt > 0 && idx < (int)sizeof(name) - 3) {
      name[idx++] = ' ';
      if (attempt >= 10) {
        name[idx++] = (char)('0' + (attempt / 10));
      }
      name[idx++] = (char)('0' + (attempt % 10));
    }

    for (int i = 0; ext && ext[i] && idx < (int)sizeof(name) - 1; i++)
      name[idx++] = ext[i];
    name[idx] = '\0';

    fm_join_path(dir_path, name, candidate, sizeof(candidate));
    if (!fm_path_exists(candidate)) {
      str_copy_safe(out, candidate, out_max);
      return;
    }
  }

  fm_join_path(dir_path, "New Item", out, out_max);
}

static void fm_navigate_to(struct window *win, struct fm_state *st,
                           const char *path) {
  if (!st)
    return;
  if (!path || !path[0]) {
    st->path[0] = '/';
    st->path[1] = '\0';
  } else {
    str_copy_safe(st->path, path, sizeof(st->path));
  }
  st->selected[0] = '\0';
  st->scroll_y = 0;
  st->context_menu_visible = 0;
  fm_set_window_title(win, st->path);
}

static void fm_go_parent(struct window *win, struct fm_state *st) {
  int len = 0;
  if (!st)
    return;
  while (st->path[len])
    len++;
  if (len <= 1)
    return;

  while (len > 0 && st->path[len - 1] != '/')
    len--;
  if (len > 1)
    len--;
  st->path[len] = '\0';
  if (len == 0) {
    st->path[0] = '/';
    st->path[1] = '\0';
  }
  st->selected[0] = '\0';
  st->scroll_y = 0;
  st->context_menu_visible = 0;
  fm_set_window_title(win, st->path);
}

static void fm_hide_context_menu(struct fm_state *st) {
  if (!st)
    return;
  st->context_menu_visible = 0;
  st->context_menu_target_on_item = 0;
  st->context_menu_target_type = 0;
  st->context_menu_target[0] = '\0';
}

static int fm_collect_callback(void *ctx, const char *name, int len, loff_t off,
                               ino_t ino, unsigned type) {
  (void)off;
  (void)ino;
  struct fm_collect_ctx *collect = (struct fm_collect_ctx *)ctx;

  if (!collect || !name)
    return 0;
  if ((len == 1 && name[0] == '.') ||
      (len == 2 && name[0] == '.' && name[1] == '.'))
    return 0;
  if (collect->count >= collect->max_items)
    return 1;

  int insert_at = collect->count;
  if (type == 4) {
    insert_at = 0;
    while (insert_at < collect->count && collect->items[insert_at].type == 4)
      insert_at++;
    for (int i = collect->count; i > insert_at; i--)
      collect->items[i] = collect->items[i - 1];
  }

  int copy_len = len;
  if (copy_len >= (int)sizeof(collect->items[insert_at].name))
    copy_len = (int)sizeof(collect->items[insert_at].name) - 1;
  for (int i = 0; i < copy_len; i++)
    collect->items[insert_at].name[i] = name[i];
  collect->items[insert_at].name[copy_len] = '\0';
  collect->items[insert_at].type = type;
  collect->count++;
  return 0;
}

static int fm_collect_items(const char *path, struct fm_item *items,
                            int max_items) {
  char resolved_path[256];
  const char *open_path =
      resolve_user_storage_path(path, resolved_path, sizeof(resolved_path));
  struct file *dir = vfs_open(open_path, O_RDONLY, 0);
  if (!dir)
    return -1;

  struct fm_collect_ctx ctx;
  ctx.items = items;
  ctx.count = 0;
  ctx.max_items = max_items;
  vfs_readdir(dir, &ctx, fm_collect_callback);
  vfs_close(dir);
  return ctx.count;
}

static void fm_open_item(struct window *win, struct fm_state *st, const char *name,
                         unsigned type) {
  char full_path[512];

  if (!win || !st || !name || !name[0])
    return;

  fm_join_path(st->path, name, full_path, sizeof(full_path));

  if (type == 4) {
    fm_navigate_to(win, st, full_path);
    return;
  }

  if (str_ends_with_ci(name, ".txt") || str_ends_with_ci(name, ".log")) {
    gui_open_notepad(full_path);
  } else if (str_ends_with_ci(name, ".app")) {
    char app_id[32];
    if (load_app_id_from_manifest_file(full_path, name, app_id, sizeof(app_id)) ==
        0) {
      gui_launch_app_by_id(app_id);
    }
  } else if (str_ends_with_ci(name, ".jpg") || str_ends_with_ci(name, ".jpeg") ||
             str_ends_with_ci(name, ".png")) {
    gui_open_image_viewer(full_path);
  } else if (str_ends_with_ci(name, ".mp3")) {
    gui_play_mp3_file(full_path);
  } else if (str_ends_with_ci(name, ".py") || str_ends_with_ci(name, ".nano")) {
    extern void term_set_active(struct terminal * term);
    extern void term_puts(struct terminal * term, const char *str);
    extern void term_execute_command(struct terminal * term, const char *cmd);
    extern void term_set_content_pos(struct terminal * t, int x, int y);
    static int term_spawn_x = 120;
    static int term_spawn_y = 100;

    struct window *term_win =
        gui_create_window("Terminal", term_spawn_x, term_spawn_y, 500, 350);
    if (term_win) {
      int content_x = term_spawn_x + BORDER_WIDTH;
      int content_y = term_spawn_y + BORDER_WIDTH + TITLEBAR_HEIGHT;
      struct terminal *term = term_create(content_x, content_y, 60, 18);
      if (term) {
        term_win->userdata = term;
        term_set_active(term);
        term_set_content_pos(term, content_x, content_y);

        char run_cmd[300] = "run ";
        int j = 4;
        for (int i = 0; full_path[i] && j < 298; i++)
          run_cmd[j++] = full_path[i];
        run_cmd[j] = '\0';
        term_execute_command(term, run_cmd);
        term_puts(term, "\n\033[32mos-next-stage\033[0m:\033[34m~\033[0m$ ");
      }
    }

    term_spawn_x = (term_spawn_x + 40) % 300 + 80;
    term_spawn_y = (term_spawn_y + 35) % 200 + 70;
  }
}

static void fm_delete_context_target(struct fm_state *st) {
  char full_path[512];
  int ret;

  if (!st || !st->context_menu_target_on_item || !st->context_menu_target[0])
    return;

  fm_join_path(st->path, st->context_menu_target, full_path, sizeof(full_path));
  if (st->context_menu_target_type == 4) {
    ret = user_storage_rmdir(full_path);
  } else {
    ret = user_storage_unlink(full_path);
  }

  if (ret == 0 && str_cmp(st->selected, st->context_menu_target) == 0)
    st->selected[0] = '\0';
}

static const unsigned char *fm_icon_for_item(const char *name, unsigned type,
                                             uint32_t *color_out) {
  uint32_t color = 0xD1D5DB;
  const unsigned char *bmp = icon_notepad;

  if (type == 4) {
    bmp = icon_files;
    color = 0x60A5FA;
  } else if (str_ends_with_ci(name, ".app")) {
    bmp = icon_files;
    color = 0xC4B5FD;
  } else if (str_ends_with_ci(name, ".py")) {
    bmp = icon_python;
    color = 0xFACC15;
  } else if (str_ends_with_ci(name, ".nano")) {
    bmp = icon_nano;
    color = 0x4ADE80;
  } else if (str_ends_with_ci(name, ".jpg") || str_ends_with_ci(name, ".jpeg") ||
             str_ends_with_ci(name, ".png")) {
    color = 0xF9E2AF;
  } else if (str_ends_with_ci(name, ".mp3")) {
    color = 0x86EFAC;
  }

  if (color_out)
    *color_out = color;
  return bmp;
}

static const char *fm_type_label(const char *name, unsigned type) {
  if (type == 4)
    return "Folder";
  if (str_ends_with_ci(name, ".app"))
    return "App";
  if (str_ends_with_ci(name, ".txt") || str_ends_with_ci(name, ".log"))
    return "Text";
  if (str_ends_with_ci(name, ".py"))
    return "Python";
  if (str_ends_with_ci(name, ".nano"))
    return "NanoLang";
  if (str_ends_with_ci(name, ".jpg") || str_ends_with_ci(name, ".jpeg") ||
      str_ends_with_ci(name, ".png"))
    return "Image";
  if (str_ends_with_ci(name, ".mp3"))
    return "Audio";
  return "File";
}

static void fm_truncate_label(const char *src, char *dst, int max_chars) {
  int len = fm_name_length(src);
  if (!dst || max_chars <= 0)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }

  if (len <= max_chars) {
    str_copy_safe(dst, src, max_chars + 1);
    return;
  }

  int keep = max_chars - 3;
  if (keep < 1)
    keep = 1;
  int i = 0;
  for (; i < keep && src[i]; i++)
    dst[i] = src[i];
  dst[i++] = '.';
  dst[i++] = '.';
  dst[i++] = '.';
  dst[i] = '\0';
}

static void installer_refresh_disk_inventory(void) {
  extern int storage_get_disk_count(void);
  extern int storage_describe_disk(int index, char *buf, int max);
  int count = storage_get_disk_count();

  installer_disk_count = 0;
  for (int i = 0; i < count && installer_disk_count < 8; i++) {
    if (storage_describe_disk(i, installer_disk_labels[installer_disk_count],
                              sizeof(installer_disk_labels[0])) == 0) {
      installer_disk_count++;
    }
  }

  if (installer_disk_count == 0) {
    str_copy_safe(installer_disk_labels[0], "No real disks detected",
                  sizeof(installer_disk_labels[0]));
    installer_selected_disk = 0;
    return;
  }

  if (installer_selected_disk >= installer_disk_count)
    installer_selected_disk = installer_disk_count - 1;
  if (installer_selected_disk < 0)
    installer_selected_disk = 0;
}

static const char *installer_selected_disk_label(void) {
  installer_refresh_disk_inventory();
  if (installer_disk_count == 0)
    return "No real disks detected";
  if (installer_selected_disk < 0 || installer_selected_disk >= installer_disk_count)
    return "No real disks detected";
  return installer_disk_labels[installer_selected_disk];
}

static void installer_write_target_config(void) {
  char manifest[256];
  char target_root[96];
  char target_cfg[128];
  char disk_location[32];
  int idx = 0;
  const char *disk = installer_selected_disk_label();
  int partition_count = 0;

  extern int storage_get_disk_location(int index, char *buf, int max);

  for (const char *p = "disk="; *p && idx < (int)sizeof(manifest) - 1; p++)
    manifest[idx++] = *p;
  for (int i = 0; disk[i] && idx < (int)sizeof(manifest) - 2; i++)
    manifest[idx++] = disk[i];
  manifest[idx++] = '\n';
  disk_location[0] = '\0';
  if (storage_get_disk_location(installer_selected_disk, disk_location,
                                sizeof(disk_location)) == 0 &&
      disk_location[0]) {
    for (const char *p = "disk_location=";
         *p && idx < (int)sizeof(manifest) - 1; p++)
      manifest[idx++] = *p;
    for (int i = 0; disk_location[i] && idx < (int)sizeof(manifest) - 2; i++)
      manifest[idx++] = disk_location[i];
    manifest[idx++] = '\n';
  }
  {
    extern int storage_get_partition_count(int disk_index);
    extern int storage_has_efi_partition(int disk_index);
    partition_count = storage_get_partition_count(installer_selected_disk);
    for (const char *p = "partitions=";
         *p && idx < (int)sizeof(manifest) - 1; p++)
      manifest[idx++] = *p;
    append_decimal(manifest, &idx, partition_count);
    manifest[idx++] = '\n';
    for (const char *p = "efi="; *p && idx < (int)sizeof(manifest) - 1; p++)
      manifest[idx++] = *p;
    append_decimal(manifest, &idx,
                   storage_has_efi_partition(installer_selected_disk) ? 1 : 0);
    manifest[idx++] = '\n';
  }
  manifest[idx] = '\0';

  write_text_file("/System/install-target.cfg", manifest);
  installer_target_root_path(target_root, sizeof(target_root));
  str_copy_safe(target_cfg, target_root, sizeof(target_cfg));
  idx = 0;
  while (target_cfg[idx] && idx < (int)sizeof(target_cfg) - 1)
    idx++;
  if (idx < (int)sizeof(target_cfg) - 1)
    target_cfg[idx++] = '/';
  for (const char *p = "install-target.cfg";
       *p && idx < (int)sizeof(target_cfg) - 1; p++)
    target_cfg[idx++] = *p;
  target_cfg[idx] = '\0';
  write_text_file(target_cfg, manifest);
}

static void partition_manager_refresh_partitions(void) {
  extern int storage_get_partition_count(int disk_index);
  extern int storage_describe_partition(int disk_index, int partition_index,
                                        char *buf, int max);
  partition_manager_partition_count = 0;
  for (int i = 0; i < storage_get_partition_count(installer_selected_disk) &&
                  partition_manager_partition_count < 8;
       i++) {
    if (storage_describe_partition(installer_selected_disk, i,
                                   partition_manager_labels
                                       [partition_manager_partition_count],
                                   sizeof(partition_manager_labels[0])) == 0) {
      partition_manager_partition_count++;
    }
  }
  if (partition_manager_partition_count == 0) {
    str_copy_safe(partition_manager_labels[0], "No partitions on selected disk",
                  sizeof(partition_manager_labels[0]));
    partition_manager_selected_partition = 0;
    return;
  }
  if (partition_manager_selected_partition >= partition_manager_partition_count)
    partition_manager_selected_partition = partition_manager_partition_count - 1;
  if (partition_manager_selected_partition < 0)
    partition_manager_selected_partition = 0;
}

static void open_partition_manager_window(int x, int y) {
  installer_refresh_disk_inventory();
  partition_manager_refresh_partitions();
  gui_create_window("Partition Manager", x, y, 560, 360);
}

static void draw_partition_manager_window(int content_x, int content_y,
                                          int content_w, int content_h) {
  extern int storage_disk_supports_partition_writes(int disk_index);
  installer_refresh_disk_inventory();
  partition_manager_refresh_partitions();

  gui_draw_rect(content_x + 12, content_y + 12, content_w - 24, content_h - 24,
                0x232337);
  gui_draw_string(content_x + 24, content_y + 22, "Partition Manager",
                  0xFFFFFF, 0x232337);
  gui_draw_string(content_x + 24, content_y + 44,
                  "Create, edit, delete, and auto-layout partitions.",
                  0xCDD6F4, 0x232337);
  gui_draw_string(content_x + 24, content_y + 58,
                  storage_disk_supports_partition_writes(installer_selected_disk)
                      ? "Selected disk supports on-disk partition writes."
                      : "Selected disk has no real partition-write backend yet.",
                  0xF9E2AF, 0x232337);

  gui_draw_string(content_x + 24, content_y + 76, "Detected disks", 0x89B4FA,
                  0x232337);
  for (int i = 0; i < installer_disk_count && i < 6; i++) {
    int row_y = content_y + 96 + i * 28;
    uint32_t row_bg = i == installer_selected_disk ? 0x334155 : 0x1B1B2B;
    gui_draw_rect(content_x + 24, row_y, content_w - 48, 24, row_bg);
    gui_draw_string(content_x + 36, row_y + 5, installer_disk_labels[i],
                    0xFFFFFF, row_bg);
    if (i == installer_selected_disk) {
      gui_draw_string(content_x + content_w - 170, row_y + 5,
                      "Installer Target", 0xA6E3A1, row_bg);
    }
  }

  gui_draw_string(content_x + 24, content_y + 274, "Selected target:",
                  0x89B4FA, 0x232337);
  gui_draw_string(content_x + 150, content_y + 274,
                  installer_selected_disk_label(), 0xFFFFFF, 0x232337);

  gui_draw_string(content_x + 24, content_y + 176, "Partitions", 0x89B4FA,
                  0x232337);
  for (int i = 0; i < partition_manager_partition_count && i < 4; i++) {
    int row_y = content_y + 196 + i * 22;
    uint32_t row_bg =
        i == partition_manager_selected_partition ? 0x3B304A : 0x181826;
    gui_draw_rect(content_x + 24, row_y, content_w - 48, 18, row_bg);
    gui_draw_string(content_x + 34, row_y + 4, partition_manager_labels[i],
                    0xFFFFFF, row_bg);
  }

  gui_draw_rect(content_x + 24, content_y + 298, 120, 30, 0x2563EB);
  gui_draw_string(content_x + 42, content_y + 307, "Use For Install",
                  0xFFFFFF, 0x2563EB);
  gui_draw_rect(content_x + 152, content_y + 298, 78, 30, 0x0F766E);
  gui_draw_string(content_x + 170, content_y + 307, "New EFI", 0xFFFFFF,
                  0x0F766E);
  gui_draw_rect(content_x + 238, content_y + 298, 94, 30, 0x166534);
  gui_draw_string(content_x + 250, content_y + 307, "New System", 0xFFFFFF,
                  0x166534);
  gui_draw_rect(content_x + 340, content_y + 298, 84, 30, 0x1D4ED8);
  gui_draw_string(content_x + 356, content_y + 307, "New Data", 0xFFFFFF,
                  0x1D4ED8);
  gui_draw_rect(content_x + 432, content_y + 298, 96, 30, 0x7C2D12);
  gui_draw_string(content_x + 450, content_y + 307, "Delete", 0xFFFFFF,
                  0x7C2D12);

  gui_draw_rect(content_x + 24, content_y + 332, 100, 30, 0x6D28D9);
  gui_draw_string(content_x + 40, content_y + 341, "Edit Sel.", 0xFFFFFF,
                  0x6D28D9);
  gui_draw_rect(content_x + 132, content_y + 332, 110, 30, 0x3B82F6);
  gui_draw_string(content_x + 154, content_y + 341, "Auto Layout", 0xFFFFFF,
                  0x3B82F6);
  gui_draw_rect(content_x + 250, content_y + 332, 90, 30, 0x3B82F6);
  gui_draw_string(content_x + 276, content_y + 341, "Refresh", 0xFFFFFF,
                  0x3B82F6);
  gui_draw_rect(content_x + 348, content_y + 332, 110, 30, 0x4B5563);
  gui_draw_string(content_x + 371, content_y + 341, "Open Files", 0xFFFFFF,
                  0x4B5563);

  gui_draw_string(content_x + 24, content_y + content_h - 52,
                  partition_manager_status, 0xCDD6F4, 0x232337);
}

struct image_viewer_state {
  media_image_t image;
};

/* Forward declarations for modern image viewer (defined later in file) */
struct modern_image_viewer_state;
static struct {
  media_image_t image;
  media_image_t rotated;
  int loaded;
  int zoom_pct;
  int offset_x;
  int offset_y;
  int dragging;
  int drag_start_x;
  int drag_start_y;
  char current_file[256];
  int current_image_index;
  int rotation;
  int fullscreen;
  int show_toolbar;
  int toolbar_timer;
  int crop_mode;
  int crop_x1, crop_y1;
  int crop_x2, crop_y2;
  /* Folder-based navigation */
  char folder_path[256];
  char file_list[32][64]; /* Up to 32 files, 64 chars each */
  int file_count;
  int file_index; /* Current index in folder */
} g_imgview = {0};
static void image_viewer_on_draw(struct window *win);
static void image_viewer_on_mouse(struct window *win, int x, int y,
                                  int buttons);

void gui_open_image_viewer(const char *path);
static void gui_play_mp3_file(const char *path);

/* File Manager Mouse Handler */
static void fm_on_mouse(struct window *win, int x, int y, int buttons) {
  struct fm_state *st = (struct fm_state *)win->userdata;
  int is_right_click = (buttons & 2) != 0;
  if (!st)
    return;

  int content_x = BORDER_WIDTH;
  int content_y = BORDER_WIDTH + TITLEBAR_HEIGHT;
  int toolbar_h = 52;
  int info_h = 54;
  int sidebar_w = 118;
  int details_w = 130;
  int list_x = content_x + sidebar_w + 10;
  int list_y = content_y + toolbar_h + info_h + 8;
  int list_w = win->width - BORDER_WIDTH * 2 - sidebar_w - details_w - 24;
  int row_h = 44;
  int menu_x = st->context_menu_x;
  int menu_y = st->context_menu_y;
  int menu_w = st->context_menu_target_on_item ? 132 : 120;
  int menu_h = st->context_menu_target_on_item ? 78 : 54;

  if (menu_x + menu_w > win->width - BORDER_WIDTH)
    menu_x = win->width - BORDER_WIDTH - menu_w;
  if (menu_y + menu_h > win->height - BORDER_WIDTH)
    menu_y = win->height - BORDER_WIDTH - menu_h;
  if (menu_x < content_x + 4)
    menu_x = content_x + 4;
  if (menu_y < content_y + 4)
    menu_y = content_y + 4;

  if (st->context_menu_visible) {
    if (x >= menu_x && x < menu_x + menu_w && y >= menu_y && y < menu_y + menu_h) {
      int item_idx = (y - menu_y - 6) / 24;
      if (st->context_menu_target_on_item) {
        if (item_idx == 0) {
          fm_open_item(win, st, st->context_menu_target,
                       (unsigned)st->context_menu_target_type);
        } else if (item_idx == 1) {
          char full_path[512];
          extern void gui_open_rename(const char *path);
          fm_join_path(st->path, st->context_menu_target, full_path,
                       sizeof(full_path));
          gui_open_rename(full_path);
        } else if (item_idx == 2) {
          fm_delete_context_target(st);
        }
      } else {
        if (item_idx == 0) {
          char new_path[512];
          fm_build_unique_child_path(st->path, "New Folder", "", new_path,
                                     sizeof(new_path));
          user_storage_mkdir(new_path, 0755);
        } else if (item_idx == 1) {
          char new_path[512];
          fm_build_unique_child_path(st->path, "New File", ".txt", new_path,
                                     sizeof(new_path));
          write_text_file(new_path, "");
        }
      }
      fm_hide_context_menu(st);
      return;
    }

    if (!is_right_click)
      fm_hide_context_menu(st);
  }

  if (y >= content_y + 10 && y < content_y + 42) {
    if (is_right_click) {
      fm_hide_context_menu(st);
      return;
    }
    if (x >= content_x + 10 && x < content_x + 74) {
      fm_go_parent(win, st);
      return;
    }
    if (x >= content_x + 82 && x < content_x + 166) {
      char new_path[512];
      fm_build_unique_child_path(st->path, "New Folder", "", new_path,
                                 sizeof(new_path));
      user_storage_mkdir(new_path, 0755);
      return;
    }
    if (x >= content_x + 174 && x < content_x + 248) {
      char new_path[512];
      fm_build_unique_child_path(st->path, "New File", ".txt", new_path,
                                 sizeof(new_path));
      write_text_file(new_path, "");
      return;
    }
    if (x >= content_x + 256 && x < content_x + 328) {
      if (st->selected[0]) {
        char full_path[512];
        extern void gui_open_rename(const char *path);
        fm_join_path(st->path, st->selected, full_path, sizeof(full_path));
        gui_open_rename(full_path);
      }
      return;
    }
    if (x >= content_x + 336 && x < content_x + 410) {
      open_partition_manager_window(win->x + 40, win->y + 30);
      return;
    }
  }

  if (x >= content_x + 10 && x < content_x + sidebar_w - 10) {
    if (is_right_click) {
      fm_hide_context_menu(st);
      return;
    }
    if (y >= content_y + 88 && y < content_y + 116)
      fm_navigate_to(win, st, "/");
    else if (y >= content_y + 118 && y < content_y + 146)
      fm_navigate_to(win, st, "/Desktop");
    else if (y >= content_y + 148 && y < content_y + 176)
      fm_navigate_to(win, st, "/Documents");
    else if (y >= content_y + 178 && y < content_y + 206)
      fm_navigate_to(win, st, "/Pictures");
    else if (y >= content_y + 208 && y < content_y + 236)
      fm_navigate_to(win, st, "/Music");
    else if (y >= content_y + 238 && y < content_y + 266)
      fm_navigate_to(win, st, "/Users");
    else if (y >= content_y + 268 && y < content_y + 296)
      fm_navigate_to(win, st, "/External");
    else if (y >= content_y + 298 && y < content_y + 326)
      fm_navigate_to(win, st, GUI_SYSTEM_APPS_FOLDER);
    return;
  }

  struct fm_item items[FM_MAX_ITEMS];
  int item_count = fm_collect_items(st->path, items, FM_MAX_ITEMS);
  if (item_count <= 0) {
    if (is_right_click) {
      st->context_menu_visible = 1;
      st->context_menu_target_on_item = 0;
      st->context_menu_x = x;
      st->context_menu_y = y;
      st->selected[0] = '\0';
    }
    return;
  }

  for (int i = 0; i < item_count; i++) {
    int row_y = list_y + i * row_h;
    if (x >= list_x && x < list_x + list_w && y >= row_y && y < row_y + row_h) {
      int was_selected = str_cmp(st->selected, items[i].name) == 0;
      str_copy_safe(st->selected, items[i].name, sizeof(st->selected));

      if (is_right_click) {
        st->context_menu_visible = 1;
        st->context_menu_target_on_item = 1;
        st->context_menu_x = x;
        st->context_menu_y = y;
        st->context_menu_target_type = (int)items[i].type;
        str_copy_safe(st->context_menu_target, items[i].name,
                      sizeof(st->context_menu_target));
        return;
      }

      fm_hide_context_menu(st);

      if (!was_selected)
        return;

      fm_open_item(win, st, items[i].name, items[i].type);
      return;
    }
  }

  if (is_right_click) {
    st->context_menu_visible = 1;
    st->context_menu_target_on_item = 0;
    st->context_menu_x = x;
    st->context_menu_y = y;
    st->selected[0] = '\0';
    return;
  }

  fm_hide_context_menu(st);
}

static void image_viewer_on_close(struct window *win) {
  if (!win || !win->userdata)
    return;
  struct image_viewer_state *st = (struct image_viewer_state *)win->userdata;
  media_free_image(&st->image);
  kfree(st);
  win->userdata = NULL;
}

static void draw_image_viewer(struct window *win, int content_x, int content_y,
                              int content_w, int content_h) {
  if (!win || !win->userdata)
    return;
  struct image_viewer_state *st = (struct image_viewer_state *)win->userdata;
  if (!st->image.pixels || st->image.width == 0 || st->image.height == 0)
    return;

  int img_w = (int)st->image.width;
  int img_h = (int)st->image.height;
  int draw_w = img_w;
  int draw_h = img_h;

  if (draw_w > content_w) {
    draw_w = content_w;
    draw_h = (img_h * draw_w) / img_w;
  }
  if (draw_h > content_h) {
    draw_h = content_h;
    draw_w = (img_w * draw_h) / img_h;
  }
  if (draw_w <= 0 || draw_h <= 0)
    return;

  int offset_x = content_x + (content_w - draw_w) / 2;
  int offset_y = content_y + (content_h - draw_h) / 2;

  for (int y = 0; y < draw_h; y++) {
    int src_y = (y * img_h) / draw_h;
    for (int x = 0; x < draw_w; x++) {
      int src_x = (x * img_w) / draw_w;
      uint32_t color = st->image.pixels[src_y * img_w + src_x];
      draw_image_pixel(offset_x + x, offset_y + y, color);
    }
  }
}

void gui_open_image_viewer(const char *path) {
  if (!path)
    return;

  /* Load image file */
  uint8_t *data = NULL;
  size_t size = 0;
  if (media_load_file(path, &data, &size) != 0) {
    printk("Image Viewer: Failed to read %s\n", path);
    return;
  }

  /* Free previous image if loaded */
  if (g_imgview.loaded) {
    media_free_image(&g_imgview.image);
    g_imgview.loaded = 0;
  }

  /* Decode new image into global state - detect format by magic bytes */
  int decode_ret = -1;
  /* PNG magic: 0x89 'P' 'N' 'G' */
  if (size >= 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
      data[3] == 'G') {
    decode_ret = media_decode_png(data, size, &g_imgview.image);
    if (decode_ret != 0) {
      printk("Image Viewer: PNG decode failed\n");
    }
  } else {
    /* Assume JPEG */
    decode_ret = media_decode_jpeg(data, size, &g_imgview.image);
    if (decode_ret != 0) {
      printk("Image Viewer: JPEG decode failed\n");
    }
  }
  if (decode_ret != 0) {
    media_free_file(data);
    return;
  }
  media_free_file(data);

  /* Set up viewer state */
  g_imgview.loaded = 1;
  g_imgview.zoom_pct = 0; /* Auto-fit */
  g_imgview.offset_x = 0;
  g_imgview.offset_y = 0;
  g_imgview.rotation = 0;
  g_imgview.fullscreen = 0;
  g_imgview.current_image_index = -1; /* -1 means file-loaded, not bootstrap */

  /* Extract folder path and filename */
  int i = 0;
  int last_slash = -1;
  while (path[i]) {
    if (path[i] == '/')
      last_slash = i;
    i++;
  }

  /* Copy folder path */
  for (int j = 0; j <= last_slash && j < 255; j++) {
    g_imgview.folder_path[j] = path[j];
  }
  g_imgview.folder_path[last_slash + 1] = '\0';

  /* Copy filename */
  const char *filename = path + last_slash + 1;
  i = 0;
  while (filename[i] && i < 255) {
    g_imgview.current_file[i] = filename[i];
    i++;
  }
  g_imgview.current_file[i] = '\0';

  /* Scan folder for image files - use hardcoded list for /Pictures */
  g_imgview.file_count = 0;
  g_imgview.file_index = 0;

  /* Known image files in Pictures folder */
  static const char *pictures_files[] = {
      "test.png",      "pig.jpg",    "city.jpg",     "nature.jpg",
      "wallpaper.jpg", "square.jpg", "portrait.jpg", "landscape.jpg"};
  int num_pictures = sizeof(pictures_files) / sizeof(pictures_files[0]);

  /* Check if we're in Pictures folder */
  int is_pictures =
      (g_imgview.folder_path[0] == '/' && g_imgview.folder_path[1] == 'P' &&
       g_imgview.folder_path[2] == 'i' && g_imgview.folder_path[3] == 'c');

  if (is_pictures) {
    for (int j = 0; j < num_pictures && g_imgview.file_count < 32; j++) {
      /* Copy filename to list */
      int k = 0;
      while (pictures_files[j][k] && k < 63) {
        g_imgview.file_list[g_imgview.file_count][k] = pictures_files[j][k];
        k++;
      }
      g_imgview.file_list[g_imgview.file_count][k] = '\0';

      /* Check if this is current file */
      int match = 1;
      for (int m = 0; filename[m] || pictures_files[j][m]; m++) {
        if (filename[m] != pictures_files[j][m]) {
          match = 0;
          break;
        }
      }
      if (match) {
        g_imgview.file_index = g_imgview.file_count;
      }
      g_imgview.file_count++;
    }
  }

  printk("Image Viewer: Loaded %s (%dx%d) - %d images in folder\n",
         g_imgview.current_file, g_imgview.image.width, g_imgview.image.height,
         g_imgview.file_count);

  /* Create modern viewer window */
  struct window *win = gui_create_window("Image Viewer", 80, 60, 800, 600);
  if (win) {
    win->on_draw = image_viewer_on_draw;
    win->on_mouse = image_viewer_on_mouse;
  }
}

static void gui_play_mp3_file(const char *path) {
  if (!path)
    return;

  uint8_t *data = NULL;
  size_t size = 0;
  if (media_load_file(path, &data, &size) != 0) {
    printk("Audio: Failed to read %s\n", path);
    return;
  }

  media_audio_t audio;
  audio.samples = NULL;
  audio.sample_count = 0;
  audio.sample_rate = 0;
  audio.channels = 0;

  if (media_decode_mp3(data, size, &audio) != 0) {
    printk("Audio: MP3 decode failed\n");
    media_free_file(data);
    return;
  }
  media_free_file(data);

  extern int intel_hda_play_pcm(const void *data, uint32_t samples,
                                uint8_t channels, uint32_t sample_rate);
  intel_hda_play_pcm(audio.samples, audio.sample_count, audio.channels,
                     audio.sample_rate);
  media_free_audio(&audio);
}

static void draw_window(struct window *win) {
  // ... rest of function ...
  if (!win->visible)
    return;

  int x = win->x, y = win->y;
  int w = win->width, h = win->height;
  struct gui_clip_state prev_clip = gui_set_clip_rect(x, y, w, h);

  gui_draw_glass_panel(x, y, w, h,
                       win->focused ? 0x6A2C3446 : 0x58303440,
                       win->focused ? 0x42FFFFFF : 0x24FFFFFF, 0x8C75839A, 2);

  /* Draw border */
  gui_fill_rect_alpha(x, y, w, BORDER_WIDTH, 0x506C7A92);
  gui_fill_rect_alpha(x, y + h - BORDER_WIDTH, w, BORDER_WIDTH, 0x3C0C1018);
  gui_fill_rect_alpha(x, y, BORDER_WIDTH, h, 0x506C7A92);
  gui_fill_rect_alpha(x + w - BORDER_WIDTH, y, BORDER_WIDTH, h, 0x506C7A92);
  gui_draw_rect_outline(x, y, w, h, 0x8B8FA1BC, BORDER_WIDTH);

  if (win->has_titlebar) {
    uint32_t titlebar_bg = win->focused ? 0x344D6488 : 0x2C3B4458;
    gui_fill_rect_alpha(x + BORDER_WIDTH, y + BORDER_WIDTH, w - BORDER_WIDTH * 2,
                        TITLEBAR_HEIGHT, titlebar_bg);
    gui_fill_rect_alpha(x + BORDER_WIDTH, y + BORDER_WIDTH, w - BORDER_WIDTH * 2,
                        1, 0x55FFFFFF);
    gui_fill_rect_alpha(x + BORDER_WIDTH, y + BORDER_WIDTH + TITLEBAR_HEIGHT - 1,
                        w - BORDER_WIDTH * 2, 1, 0x440A0D12);

    /* Traffic light buttons on LEFT side - Modern rounded */
    int btn_cx = x + BORDER_WIDTH + 16; /* First circle center X */
    int btn_cy = y + BORDER_WIDTH + TITLEBAR_HEIGHT / 2; /* Center Y */
    int btn_r = 6;                                       /* Button radius */

    /* Close button - Red */
    if (window_close_disabled(win)) {
      draw_circle(btn_cx, btn_cy, btn_r, 0x6B7280);
      for (int i = -2; i <= 2; i++) {
        draw_pixel(btn_cx + i, btn_cy + i, 0x374151);
        draw_pixel(btn_cx + i, btn_cy - i, 0x374151);
      }
    } else {
      draw_circle(btn_cx, btn_cy, btn_r, COLOR_BTN_CLOSE);
      for (int i = -2; i <= 2; i++) {
        draw_pixel(btn_cx + i, btn_cy + i, 0x7F1D1D);
        draw_pixel(btn_cx + i, btn_cy - i, 0x7F1D1D);
      }
    }

    /* Minimize button - Amber */
    btn_cx += 18;
    if (window_minimize_disabled(win)) {
      draw_circle(btn_cx, btn_cy, btn_r, 0x6B7280);
      for (int i = -2; i <= 2; i++) {
        draw_pixel(btn_cx + i, btn_cy, 0x374151);
      }
    } else {
      draw_circle(btn_cx, btn_cy, btn_r, COLOR_BTN_MINIMIZE);
      for (int i = -2; i <= 2; i++) {
        draw_pixel(btn_cx + i, btn_cy, 0x78350F);
      }
    }

    /* Zoom button - Green */
    btn_cx += 18;
    draw_circle(btn_cx, btn_cy, btn_r, COLOR_BTN_ZOOM);
    /* Draw + icon */
    for (int i = -2; i <= 2; i++) {
      draw_pixel(btn_cx + i, btn_cy, 0x14532D);
      draw_pixel(btn_cx, btn_cy + i, 0x14532D);
    }

    /* Window title - centered with modern font styling */
    int title_len = 0;
    for (const char *p = win->title; *p; p++)
      title_len++;
    int title_x = x + (w - title_len * 8) / 2;
    gui_draw_string(title_x, y + BORDER_WIDTH + 7, win->title,
                    win->focused ? 0xFFF7FBFF : 0xD8DDE6F2, 0x00000000);
  }

  /* Draw content area */
  int content_x = x + BORDER_WIDTH;
  int content_y = y + BORDER_WIDTH + (win->has_titlebar ? TITLEBAR_HEIGHT : 0);
  int content_w = w - BORDER_WIDTH * 2;
  int content_h =
      h - BORDER_WIDTH * 2 - (win->has_titlebar ? TITLEBAR_HEIGHT : 0);

  gui_fill_rect_alpha(content_x, content_y, content_w, content_h, 0x98171A26);
  gui_fill_rect_alpha(content_x, content_y, content_w, 1, 0x28FFFFFF);

  /* Draw window-specific content based on title */
  /* Calculator - Modern Design */
  if (win->title[0] == 'C' && win->title[1] == 'a' && win->title[2] == 'l') {
    /* Modern dark background */
    gui_draw_rect(content_x, content_y, content_w, content_h, 0x1C1C1E);

    /* Display area - gradient effect (dark to slightly lighter) */
    int disp_h = 70;
    gui_draw_rect(content_x + 12, content_y + 12, content_w - 24, disp_h,
                  0x2C2C2E);
    gui_draw_rect(content_x + 12, content_y + 12, content_w - 24, 2, 0x3C3C3E);

    /* Display value - large right-aligned */
    char display[16];
    long v = calc_display;
    int is_neg = 0;
    if (v < 0) {
      is_neg = 1;
      v = -v;
    }
    int idx = 0;
    if (v == 0) {
      display[idx++] = '0';
    } else {
      char tmp[16];
      int ti = 0;
      while (v > 0 && ti < 14) {
        tmp[ti++] = '0' + (v % 10);
        v /= 10;
      }
      if (is_neg)
        display[idx++] = '-';
      while (ti > 0) {
        display[idx++] = tmp[--ti];
      }
    }
    display[idx] = '\0';
    /* Draw display value with simulated large font (double-draw) */
    int text_x = content_x + content_w - 20 - idx * 12;
    int text_y = content_y + 40;
    gui_draw_string(text_x, text_y, display, 0xFFFFFF, 0x2C2C2E);
    gui_draw_string(text_x + 1, text_y, display, 0xFFFFFF, 0x2C2C2E);

    /* Button grid - 4x5 layout with proper spacing */
    static const char *btns[5][4] = {{"C", "+/-", "%", "/"},
                                     {"7", "8", "9", "*"},
                                     {"4", "5", "6", "-"},
                                     {"1", "2", "3", "+"},
                                     {"0", "0", ".", "="}};

    int grid_x = content_x + 12;
    int grid_y = content_y + disp_h + 20;
    int grid_w = content_w - 24;
    int grid_h = content_h - disp_h - 32;
    int bw = (grid_w - 12) / 4; /* 3 gaps of 4px */
    int bh = (grid_h - 16) / 5; /* 4 gaps of 4px */
    int gap = 4;

    for (int row = 0; row < 5; row++) {
      for (int col = 0; col < 4; col++) {
        /* Skip duplicate "0" cell */
        if (row == 4 && col == 1)
          continue;

        int bx = grid_x + col * (bw + gap);
        int by = grid_y + row * (bh + gap);
        int btn_w = bw;

        /* "0" button spans 2 columns */
        if (row == 4 && col == 0) {
          btn_w = bw * 2 + gap;
        }

        /* Button colors */
        uint32_t bg, fg;
        char btn_char = btns[row][col][0];

        if (btn_char == '/' || btn_char == '*' || btn_char == '-' ||
            btn_char == '+' || btn_char == '=') {
          /* Orange operator buttons */
          bg = 0xFF9F0A;
          fg = 0xFFFFFF;
        } else if (btn_char == 'C' || btn_char == '+' || btn_char == '%') {
          /* Light gray function buttons */
          bg = 0xA5A5A5;
          fg = 0x000000;
        } else {
          /* Dark gray number buttons */
          bg = 0x333333;
          fg = 0xFFFFFF;
        }

        /* Draw button with rounded effect (lighter top edge) */
        gui_draw_rect(bx, by, btn_w, bh, bg);
        gui_draw_rect(bx, by, btn_w, 2, (bg & 0xFEFEFE) + 0x202020);

        /* Center text in button */
        const char *label = btns[row][col];
        int label_len = 0;
        while (label[label_len])
          label_len++;
        int tx = bx + (btn_w - label_len * 8) / 2;
        int ty = by + (bh - 16) / 2;
        gui_draw_string(tx, ty, label, fg, bg);
      }
    }
  }
  /* File Manager */
  else if (win->title[0] == 'F' && win->title[1] == 'i' &&
           win->title[2] == 'l') {
    struct fm_state *st = (struct fm_state *)win->userdata;
    const char *path = st ? st->path : "/";
    struct fm_item items[FM_MAX_ITEMS];
    int item_count = fm_collect_items(path, items, FM_MAX_ITEMS);
    int folder_count = 0;
    int selected_index = -1;

    if (item_count > 0) {
      for (int i = 0; i < item_count; i++) {
        if (items[i].type == 4)
          folder_count++;
        if (st && str_cmp(st->selected, items[i].name) == 0)
          selected_index = i;
      }
    }

    int toolbar_h = 52;
    int info_h = 54;
    int sidebar_w = 118;
    int details_w = 130;
    int sidebar_x = content_x + 8;
    int sidebar_y = content_y + toolbar_h + 8;
    int list_x = content_x + sidebar_w + 10;
    int list_y = content_y + toolbar_h + info_h + 8;
    int list_w = content_w - sidebar_w - details_w - 24;
    int row_h = 44;

    gui_draw_rect(content_x, content_y, content_w, content_h, 0x171A24);

    gui_draw_rect(content_x, content_y, content_w, toolbar_h, 0x111827);
    gui_draw_rect(content_x + 10, content_y + 10, 64, 30, 0x334155);
    gui_draw_string(content_x + 28, content_y + 19, "Back", 0xFFFFFF, 0x334155);
    gui_draw_rect(content_x + 82, content_y + 10, 84, 30, 0x1D4ED8);
    gui_draw_string(content_x + 96, content_y + 19, "Folder", 0xFFFFFF, 0x1D4ED8);
    gui_draw_rect(content_x + 174, content_y + 10, 74, 30, 0x0F766E);
    gui_draw_string(content_x + 192, content_y + 19, "File", 0xFFFFFF, 0x0F766E);
    gui_draw_rect(content_x + 256, content_y + 10, 72, 30, 0x6D28D9);
    gui_draw_string(content_x + 270, content_y + 19, "Rename", 0xFFFFFF, 0x6D28D9);
    gui_draw_rect(content_x + 336, content_y + 10, 74, 30, 0x2563EB);
    gui_draw_string(content_x + 356, content_y + 19, "Disks", 0xFFFFFF, 0x2563EB);

    gui_draw_rect(content_x + 8, content_y + toolbar_h + 4, content_w - 16, 42,
                  0x1F2937);
    gui_draw_string(content_x + 18, content_y + toolbar_h + 10, "Location",
                    0x93C5FD, 0x1F2937);
    gui_draw_string(content_x + 90, content_y + toolbar_h + 10, path, 0xFFFFFF,
                    0x1F2937);
    gui_draw_string(content_x + content_w - 130, content_y + toolbar_h + 10,
                    "Folders", 0x9CA3AF, 0x1F2937);

    char count_buf[16];
    count_buf[0] = '\0';
    int count_idx = 0;
    if (folder_count >= 10)
      count_buf[count_idx++] = (char)('0' + (folder_count / 10));
    count_buf[count_idx++] = (char)('0' + (folder_count % 10));
    count_buf[count_idx] = '\0';
    gui_draw_string(content_x + content_w - 74, content_y + toolbar_h + 10,
                    count_buf, 0xFFFFFF, 0x1F2937);

    gui_draw_rect(sidebar_x, sidebar_y, sidebar_w - 16, content_h - toolbar_h - 16,
                  0x111827);
    gui_draw_string(sidebar_x + 12, sidebar_y + 14, "Places", 0xFFFFFF, 0x111827);

    const char *places[] = {"/", "/Desktop", "/Documents", "/Pictures", "/Music",
                            "/Users", "/External", GUI_SYSTEM_APPS_FOLDER};
    const char *labels[] = {"Root", "Desktop", "Documents", "Pictures", "Music",
                            "Users", "External", "System Apps"};
    for (int i = 0; i < 8; i++) {
      int row_y = sidebar_y + 34 + i * 30;
      uint32_t row_bg = (st && str_cmp(st->path, places[i]) == 0) ? 0x1D4ED8 : 0x1F2937;
      gui_draw_rect(sidebar_x + 8, row_y, sidebar_w - 32, 24, row_bg);
      gui_draw_string(sidebar_x + 16, row_y + 6, labels[i], 0xFFFFFF, row_bg);
    }

    gui_draw_rect(list_x, content_y + toolbar_h + 8, list_w, content_h - toolbar_h - 16,
                  0x0F172A);
    gui_draw_rect(details_w > 0 ? content_x + content_w - details_w - 8 : content_x,
                  content_y + toolbar_h + 8, details_w, content_h - toolbar_h - 16,
                  0x111827);
    gui_draw_string(content_x + content_w - details_w + 8, content_y + toolbar_h + 22,
                    "Details", 0xFFFFFF, 0x111827);

    gui_draw_rect(list_x + 8, content_y + toolbar_h + 18, list_w - 16, 24, 0x172033);
    gui_draw_string(list_x + 18, content_y + toolbar_h + 24, "Name", 0x93C5FD, 0x172033);
    gui_draw_string(list_x + list_w - 74, content_y + toolbar_h + 24, "Type",
                    0x93C5FD, 0x172033);

    if (item_count < 0) {
      gui_draw_string(list_x + 16, list_y + 10, "Failed to open directory",
                      0xFCA5A5, 0x0F172A);
    } else if (item_count == 0) {
      gui_draw_string(list_x + 16, list_y + 10, "This folder is empty.",
                      0xCBD5E1, 0x0F172A);
    } else {
      for (int i = 0; i < item_count && i < 10; i++) {
        int row_y = list_y + i * row_h;
        int is_selected = (selected_index == i);
        uint32_t row_bg = is_selected ? 0x1D4ED8 : (i % 2 ? 0x111827 : 0x0F172A);
        uint32_t icon_color = 0xFFFFFF;
        const unsigned char *icon =
            fm_icon_for_item(items[i].name, items[i].type, &icon_color);
        char short_name[22];
        const char *type_label = fm_type_label(items[i].name, items[i].type);

        fm_truncate_label(items[i].name, short_name, 18);
        gui_draw_rect(list_x + 8, row_y, list_w - 16, row_h - 4, row_bg);
        draw_icon(list_x + 14, row_y + 6, 24, icon, icon_color, row_bg);
        gui_draw_string(list_x + 48, row_y + 14, short_name, 0xFFFFFF, row_bg);
        gui_draw_string(list_x + list_w - 78, row_y + 14, type_label,
                        is_selected ? 0xDBEAFE : 0x94A3B8, row_bg);
      }
    }

    gui_draw_string(content_x + content_w - details_w + 8, content_y + toolbar_h + 54,
                    "Items", 0x93C5FD, 0x111827);
    char items_buf[16];
    items_buf[0] = '\0';
    int items_idx = 0;
    if (item_count > 99)
      item_count = 99;
    if (item_count >= 10)
      items_buf[items_idx++] = (char)('0' + (item_count / 10));
    items_buf[items_idx++] = (char)('0' + (item_count % 10));
    items_buf[items_idx] = '\0';
    gui_draw_string(content_x + content_w - details_w + 58, content_y + toolbar_h + 54,
                    items_buf, 0xFFFFFF, 0x111827);

    gui_draw_string(content_x + content_w - details_w + 8, content_y + toolbar_h + 78,
                    "Selected", 0x93C5FD, 0x111827);
    if (selected_index >= 0) {
      char selected_short[18];
      fm_truncate_label(items[selected_index].name, selected_short, 14);
      gui_draw_string(content_x + content_w - details_w + 8,
                      content_y + toolbar_h + 100, selected_short, 0xFFFFFF,
                      0x111827);
      gui_draw_string(content_x + content_w - details_w + 8,
                      content_y + toolbar_h + 124,
                      fm_type_label(items[selected_index].name,
                                    items[selected_index].type),
                      0xA5B4FC, 0x111827);
      gui_draw_string(content_x + content_w - details_w + 8,
                      content_y + toolbar_h + 148,
                      items[selected_index].type == 4 ? "Open: click twice"
                                                     : "Open: click twice",
                      0x94A3B8, 0x111827);
    } else {
      gui_draw_string(content_x + content_w - details_w + 8,
                      content_y + toolbar_h + 100, "Nothing selected",
                      0x94A3B8, 0x111827);
    }

    gui_draw_string(content_x + content_w - details_w + 8,
                    content_y + content_h - 40, "Quick access", 0x93C5FD, 0x111827);
    gui_draw_string(content_x + content_w - details_w + 8,
                    content_y + content_h - 22, "Root Desktop Docs",
                    0xCBD5E1, 0x111827);

    if (st && st->context_menu_visible) {
      int menu_x = win->x + st->context_menu_x;
      int menu_y = win->y + st->context_menu_y;
      int menu_w = st->context_menu_target_on_item ? 132 : 120;
      int menu_h = st->context_menu_target_on_item ? 78 : 54;
      const char *row1 = st->context_menu_target_on_item ? "Open" : "New Folder";
      const char *row2 = st->context_menu_target_on_item ? "Rename" : "New File";
      const char *row3 = st->context_menu_target_on_item ? "Delete" : NULL;

      if (menu_x + menu_w > win->x + win->width - BORDER_WIDTH)
        menu_x = win->x + win->width - BORDER_WIDTH - menu_w;
      if (menu_y + menu_h > win->y + win->height - BORDER_WIDTH)
        menu_y = win->y + win->height - BORDER_WIDTH - menu_h;
      if (menu_x < content_x + 4)
        menu_x = content_x + 4;
      if (menu_y < content_y + 4)
        menu_y = content_y + 4;

      gui_fill_rect_alpha(menu_x + 2, menu_y + 2, menu_w, menu_h, 0x24000000);
      gui_draw_glass_panel(menu_x, menu_y, menu_w, menu_h, 0xD11A2230,
                           0x28FFFFFF, 0x80465C78, 1);

      gui_draw_rect(menu_x + 6, menu_y + 6, menu_w - 12, 20,
                    mouse_x >= menu_x + 6 && mouse_x < menu_x + menu_w - 6 &&
                            mouse_y >= menu_y + 6 && mouse_y < menu_y + 26
                        ? 0x1D4ED8
                        : 0x00000000);
      gui_draw_string(menu_x + 14, menu_y + 10, row1, 0xFFFFFF, 0x00000000);

      gui_draw_rect(menu_x + 6, menu_y + 30, menu_w - 12, 20,
                    mouse_x >= menu_x + 6 && mouse_x < menu_x + menu_w - 6 &&
                            mouse_y >= menu_y + 30 && mouse_y < menu_y + 50
                        ? 0x1D4ED8
                        : 0x00000000);
      gui_draw_string(menu_x + 14, menu_y + 34, row2, 0xFFFFFF, 0x00000000);

      if (row3) {
        gui_draw_rect(menu_x + 6, menu_y + 54, menu_w - 12, 20,
                      mouse_x >= menu_x + 6 &&
                              mouse_x < menu_x + menu_w - 6 &&
                              mouse_y >= menu_y + 54 && mouse_y < menu_y + 74
                          ? 0x7F1D1D
                          : 0x00000000);
        gui_draw_string(menu_x + 14, menu_y + 58, row3, 0xFECACA, 0x00000000);
      }
    }
  }
  /* Paint */
  else if (win->title[0] == 'P' && win->title[1] == 'a' &&
           win->title[2] == 'i') {
    /* Toolbar */
    gui_draw_rect(content_x, content_y, content_w, 32, 0x404040);
    gui_draw_string(content_x + 8, content_y + 8,
                    "Brush [O]  Line [/]  Color:", 0xFFFFFF, 0x404040);
    /* Color palette */
    gui_draw_rect(content_x + 200, content_y + 4, 20, 20, 0xFF0000);
    gui_draw_rect(content_x + 224, content_y + 4, 20, 20, 0x00FF00);
    gui_draw_rect(content_x + 248, content_y + 4, 20, 20, 0x0000FF);
    gui_draw_rect(content_x + 272, content_y + 4, 20, 20, 0x000000);
    /* Canvas */
    gui_draw_rect(content_x + 4, content_y + 36, content_w - 8, content_h - 44,
                  0xFFFFFF);
  }
  /* Browser */
  else if (win->title[0] == 'B' && win->title[1] == 'r' &&
           win->title[2] == 'o') {
    /* Toolbar Background */
    gui_draw_rect(content_x, content_y, content_w, 40, 0xDDDDDD);

    /* Address Bar */
    gui_draw_rect(content_x + 80, content_y + 8, content_w - 96, 24, 0xFFFFFF);
    gui_draw_rect_outline(content_x + 80, content_y + 8, content_w - 96, 24,
                          0xA0A0A0, 1);
    gui_draw_string(content_x + 88, content_y + 12, "http://os.de",
                    0x333333, 0xFFFFFF);

    /* Navigation Buttons */
    gui_draw_string(content_x + 12, content_y + 12, "<", 0x555555, 0xDDDDDD);
    gui_draw_string(content_x + 35, content_y + 12, ">", 0x555555, 0xDDDDDD);
    gui_draw_string(content_x + 58, content_y + 12, "@", 0x555555,
                    0xDDDDDD); /* Refresh */

    /* Web Content Area */
    gui_draw_rect(content_x, content_y + 40, content_w, content_h - 40,
                  0xFFFFFF);

    /* Mock Page Content */
    gui_draw_string(content_x + 20, content_y + 60, "Welcome to Browser",
                    0x000000, 0xFFFFFF);
    gui_draw_rect(content_x + 20, content_y + 78, 200, 2,
                  0x007AFF); /* Underline */

    gui_draw_string(content_x + 20, content_y + 90, "Status:", 0x555555,
                    0xFFFFFF);
    gui_draw_string(content_x + 80, content_y + 90, "Networking Enabled",
                    0x00AA00, 0xFFFFFF);

    gui_draw_string(content_x + 20, content_y + 110, "IP Addr:", 0x555555,
                    0xFFFFFF);
    gui_draw_string(content_x + 80, content_y + 110, "10.0.2.15 (DHCP)",
                    0x333333, 0xFFFFFF);

    /* Fake links */
    gui_draw_string(content_x + 20, content_y + 150, "- Latest News", 0x007AFF,
                    0xFFFFFF);
    gui_draw_string(content_x + 20, content_y + 170, "- Documentation",
                    0x007AFF, 0xFFFFFF);
    gui_draw_string(content_x + 20, content_y + 190, "- Source Code", 0x007AFF,
                    0xFFFFFF);
  }
  /* App Store */
  else if (win->title[0] == 'A' && win->title[1] == 'p' &&
           win->title[2] == 'p' && win->title[3] == ' ') {
    draw_app_store(content_x, content_y, content_w, content_h);
  }
  /* Installer */
  else if (win->title[0] == 'I' && win->title[1] == 'n' &&
           win->title[2] == 's' && win->title[3] == 't') {
    draw_installer_window(content_x, content_y, content_w, content_h);
  }
  /* Partition Manager */
  else if (win->title[0] == 'P' && win->title[1] == 'a' &&
           win->title[2] == 'r' && win->title[3] == 't') {
    draw_partition_manager_window(content_x, content_y, content_w, content_h);
  }
  else if (win == startup_window || startup_flow_active() ||
           (win->title[0] == 'A' && win->title[1] == 'c' &&
            win->title[2] == 'c') ||
           (win->title[0] == 'L' && win->title[1] == 'o' &&
            win->title[2] == 'g') ||
           (win->title[0] == 'W' && win->title[1] == 'e' &&
            win->title[2] == 'l') ||
           (win->title[0] == 'P' && win->title[1] == 'r' &&
            win->title[2] == 'e')) {
    draw_startup_auth_window(win, content_x, content_y, content_w, content_h);
  }
  /* Image Viewer */
  else if (win->title[0] == 'I' && win->title[1] == 'm' &&
           win->title[2] == 'a') {
    draw_image_viewer(win, content_x, content_y, content_w, content_h);
  }
  /* Help */
  else if (win->title[0] == 'H' && win->title[1] == 'e') {
    int yy = content_y + 10;
    gui_draw_string(content_x + 10, yy, "OS8 Help", 0x89B4FA, THEME_BG);
    yy += 24;
    gui_draw_string(content_x + 10, yy, "Mouse:", 0xF9E2AF, THEME_BG);
    yy += 18;
    gui_draw_string(content_x + 20, yy, "- Click dock to launch apps", 0xCDD6F4,
                    THEME_BG);
    yy += 16;
    gui_draw_string(content_x + 20, yy, "- Drag titlebars to move", 0xCDD6F4,
                    THEME_BG);
    yy += 16;
    gui_draw_string(content_x + 20, yy, "- Click red button to close", 0xCDD6F4,
                    THEME_BG);
    yy += 24;
    gui_draw_string(content_x + 10, yy, "Terminal:", 0xF9E2AF, THEME_BG);
    yy += 18;
    gui_draw_string(content_x + 20, yy, "- Type 'help' for commands", 0xCDD6F4,
                    THEME_BG);
    yy += 16;
    gui_draw_string(content_x + 20, yy, "- Type 'neofetch' for info", 0xCDD6F4,
                    THEME_BG);
  }
  /* About window */
  else if (win->title[0] == 'A' && win->title[1] == 'b' &&
           win->title[2] == 'o') {
    int yy = content_y + 20;
    int center_x = content_x + content_w / 2;
    char resolution[32];
    char windows_info[32];
#ifdef ARCH_X86_64
    const char *arch_info = "Architecture:  x86_64";
#elif defined(ARCH_X86)
    const char *arch_info = "Architecture:  x86";
#else
    const char *arch_info = "Architecture:  ARM64";
#endif

    build_resolution_string(resolution, primary_display.width,
                            primary_display.height);
    build_windows_string(windows_info);

    /* OS Logo */
    gui_draw_os_logo(center_x - 21, yy, 3, 0xCDD6F4, 0x89B4FA, THEME_BG);
    yy += 52;

    /* OS Name - large and centered */
    gui_draw_string(center_x - 58, yy, "OS8", 0xFFFFFF, THEME_BG);
    yy += 24;

    /* Version */
    gui_draw_string(center_x - 68, yy, "Version 8.0.0", 0xA6ADC8, THEME_BG);
    yy += 28;

    /* System info box */
    gui_draw_rect(content_x + 20, yy, content_w - 40, 80, 0x252535);
    yy += 10;
    gui_draw_string(content_x + 30, yy, arch_info, 0xCDD6F4, 0x252535);
    yy += 18;
    gui_draw_string(content_x + 30, yy, "Kernel:        OS8 v8.0.0",
                    0xCDD6F4, 0x252535);
    yy += 18;
    gui_draw_string(content_x + 30, yy, "Desktop:       Window compositor active",
                    0xCDD6F4, 0x252535);
    yy += 18;
    gui_draw_string(content_x + 30, yy, "Display:       ", 0xCDD6F4, 0x252535);
    gui_draw_string(content_x + 142, yy, resolution, 0xCDD6F4, 0x252535);
    gui_draw_string(content_x + 250, yy, windows_info, 0x89B4FA, 0x252535);
    yy += 28;
    gui_draw_string(content_x + 30, yy, BUILD_UUID, 0x6C7086, THEME_BG);
    yy += 20;

    /* Copyright */
    gui_draw_string(content_x + 30, yy, "(c) 2027 Benno111", 0x6C7086,
                    THEME_BG);
  }
  /* Settings window */
  else if (win->title[0] == 'S' && win->title[1] == 'e' &&
           win->title[2] == 't') {
    char resolution[32];
    char windows_info[32];
    const char *blur_status;
    const char *gpu_status;
    extern int intel_hda_is_playing(void);
    int sidebar_w = 118;
    int panel_x = content_x + sidebar_w + 14;
    int panel_y = content_y + 14;
    int panel_w = content_w - sidebar_w - 24;
    int card_w = (panel_w - 12) / 2;
    int dock_count_buf_idx = 0;
    char dock_count_buf[24];
    char installed_buf[24];
    int installed_apps = 0;

    build_resolution_string(resolution, primary_display.width,
                            primary_display.height);
    build_windows_string(windows_info);
    load_dock_config();
    for (int i = 0; i < app_catalog_count; i++) {
      if (app_is_installed(&app_catalog[i]))
        installed_apps++;
    }
    if (gui_are_blur_effects_enabled()) {
      blur_status = "Blur: On";
    } else if (gui_blur_effects_requested()) {
      blur_status = "Blur: Auto-disabled on this GPU";
    } else {
      blur_status = "Blur: Off";
    }
    if (gui_is_gpu_rendering_enabled()) {
      gpu_status = "GPU rendering active";
    } else if (str_cmp(g_gpu_backend_name, "bochs-vbe") == 0) {
      gpu_status = "Bochs/QEMU display backend active";
    } else if (str_cmp(g_gpu_backend_name, "framebuffer") == 0) {
      gpu_status = "Framebuffer display backend active";
    } else {
      gpu_status = "Software rendering active";
    }

    dock_count_buf[0] = '\0';
    append_decimal(dock_count_buf, &dock_count_buf_idx, dock_item_count);
    notepad_append_to_buf(dock_count_buf, sizeof(dock_count_buf), " dock apps");
    installed_buf[0] = '\0';
    dock_count_buf_idx = 0;
    append_decimal(installed_buf, &dock_count_buf_idx, installed_apps);
    notepad_append_to_buf(installed_buf, sizeof(installed_buf), " installed");

    gui_draw_rect(content_x, content_y, content_w, content_h, 0x141824);
    gui_draw_rect(content_x + 10, content_y + 10, sidebar_w - 12, content_h - 20,
                  0x101827);
    gui_draw_string(content_x + 24, content_y + 24, "Settings", 0xFFFFFF, 0x101827);
    gui_draw_string(content_x + 24, content_y + 42, "Control center", 0x94A3B8,
                    0x101827);

    for (int i = 0; i < 3; i++) {
      int tab_y = content_y + 76 + i * 38;
      uint32_t tab_bg = i == settings_active_tab ? 0x2563EB : 0x1E293B;
      const char *label = i == 0 ? "Overview" : (i == 1 ? "Appearance" : "System");
      gui_draw_rect(content_x + 18, tab_y, sidebar_w - 28, 28, tab_bg);
      gui_draw_string(content_x + 30, tab_y + 8, label, 0xFFFFFF, tab_bg);
    }

    gui_draw_rect(panel_x, panel_y, panel_w, 60, 0x1F2937);
    gui_draw_string(panel_x + 18, panel_y + 16,
                    settings_active_tab == 0
                        ? "Desktop overview"
                        : (settings_active_tab == 1 ? "Appearance & effects"
                                                    : "System tools"),
                    0xFFFFFF, 0x1F2937);
    gui_draw_string(panel_x + 18, panel_y + 34, settings_status, 0xCBD5E1,
                    0x1F2937);

    if (settings_active_tab == 0) {
      int card_y = panel_y + 72;
      gui_draw_rect(panel_x, card_y, panel_w, 72, 0x252535);
      gui_draw_string(panel_x + 16, card_y + 12, "Display", 0x93C5FD, 0x252535);
      gui_draw_string(panel_x + 16, card_y + 32, resolution, 0xFFFFFF, 0x252535);
      gui_draw_string(panel_x + 180, card_y + 32, windows_info, 0xCBD5E1, 0x252535);
      gui_draw_string(panel_x + 16, card_y + 50, dock_count_buf, 0xA5B4FC, 0x252535);
      gui_draw_string(panel_x + 180, card_y + 50, installed_buf, 0xA5F3FC, 0x252535);

      card_y += 84;
      gui_draw_rect(panel_x, card_y, card_w, 74, 0x252535);
      gui_draw_string(panel_x + 16, card_y + 12, "Graphics", 0x89B4FA, 0x252535);
      gui_draw_string(panel_x + 16, card_y + 32, gpu_status, 0xFFFFFF, 0x252535);
      gui_draw_string(panel_x + 16, card_y + 50, blur_status, 0xCBD5E1, 0x252535);

      gui_draw_rect(panel_x + card_w + 12, card_y, card_w, 74, 0x252535);
      gui_draw_string(panel_x + card_w + 28, card_y + 12, "Media & Network",
                      0x89B4FA, 0x252535);
      gui_draw_string(panel_x + card_w + 28, card_y + 32,
                      intel_hda_is_playing() ? "Audio is currently playing"
                                             : "Audio backend standing by",
                      intel_hda_is_playing() ? 0xA6E3A1 : 0xFFFFFF, 0x252535);
      gui_draw_string(panel_x + card_w + 28, card_y + 50,
                      "virtio-net online with user-mode NAT", 0xCBD5E1, 0x252535);

      card_y += 88;
      gui_draw_rect(panel_x, card_y, 108, 30, 0x1D4ED8);
      gui_draw_string(panel_x + 18, card_y + 9, "Backgrounds", 0xFFFFFF, 0x1D4ED8);
      gui_draw_rect(panel_x + 118, card_y, 98, 30, 0x2563EB);
      gui_draw_string(panel_x + 138, card_y + 9, "App Store", 0xFFFFFF, 0x2563EB);
      gui_draw_rect(panel_x + 226, card_y, 92, 30, 0x3B82F6);
      gui_draw_string(panel_x + 248, card_y + 9, "Devices", 0xFFFFFF, 0x3B82F6);
      gui_draw_rect(panel_x + 328, card_y, 84, 30, 0x4B5563);
      gui_draw_string(panel_x + 354, card_y + 9, "About", 0xFFFFFF, 0x4B5563);
    } else if (settings_active_tab == 1) {
      int preview_x = panel_x;
      int preview_y = panel_y + 72;
      int preview_w = 180;
      int preview_h = 90;
      int resolution_card_y;

      settings_sync_resolution_picker();

      gui_draw_rect(preview_x, preview_y, preview_w, preview_h, 0x252535);
      load_thumbnails();
      if (wallpapers[current_wallpaper].type == 1 &&
          thumbnail_cache[current_wallpaper].pixels) {
        media_image_t *thumb_img = &thumbnail_cache[current_wallpaper];
        for (int py = 0; py < preview_h - 16; py++) {
          for (int px = 0; px < preview_w - 16; px++) {
            int src_x = (px * thumb_img->width) / (preview_w - 16);
            int src_y = (py * thumb_img->height) / (preview_h - 16);
            if (src_x < (int)thumb_img->width && src_y < (int)thumb_img->height) {
              draw_image_pixel(preview_x + 8 + px, preview_y + 8 + py,
                               thumb_img->pixels[src_y * thumb_img->width + src_x]);
            }
          }
        }
      } else {
        for (int py = 0; py < preview_h - 16; py++) {
          uint8_t pr = wallpapers[current_wallpaper].tr +
                       ((wallpapers[current_wallpaper].br -
                         wallpapers[current_wallpaper].tr) *
                        py) /
                           (preview_h - 16);
          uint8_t pg = wallpapers[current_wallpaper].tg +
                       ((wallpapers[current_wallpaper].bg -
                         wallpapers[current_wallpaper].tg) *
                        py) /
                           (preview_h - 16);
          uint8_t pb = wallpapers[current_wallpaper].tb +
                       ((wallpapers[current_wallpaper].bb -
                         wallpapers[current_wallpaper].tb) *
                        py) /
                           (preview_h - 16);
          gui_draw_rect(preview_x + 8, preview_y + 8 + py, preview_w - 16, 1,
                        (pr << 16) | (pg << 8) | pb);
        }
      }

      gui_draw_rect(panel_x + 194, preview_y, panel_w - 194, preview_h, 0x252535);
      gui_draw_string(panel_x + 210, preview_y + 14, "Current wallpaper", 0x93C5FD,
                      0x252535);
      gui_draw_string(panel_x + 210, preview_y + 36, wallpapers[current_wallpaper].name,
                      0xFFFFFF, 0x252535);
      gui_draw_string(panel_x + 210, preview_y + 56,
                      wallpapers[current_wallpaper].type == 1 ? "Photo-based scene"
                                                              : "Gradient theme",
                      0xCBD5E1, 0x252535);
      gui_draw_string(panel_x + 210, preview_y + 72, blur_status, 0xA5B4FC, 0x252535);

      preview_y += 104;
      gui_draw_rect(panel_x, preview_y, panel_w, 72, 0x252535);
      gui_draw_string(panel_x + 16, preview_y + 12, "Visual effects", 0x89B4FA,
                      0x252535);
      gui_draw_string(panel_x + 16, preview_y + 30, gpu_status, 0xFFFFFF, 0x252535);
      gui_draw_string(panel_x + 16, preview_y + 46, g_gpu_backend_name, 0xCBD5E1,
                      0x252535);
      gui_draw_string(panel_x + 200, preview_y + 30, blur_status, 0xA5B4FC,
                      0x252535);
      gui_draw_string(panel_x + 200, preview_y + 46,
                      dock_is_visible() ? "Dock is visible on this boot mode"
                                        : "Dock hidden in current mode",
                      0xCBD5E1, 0x252535);

      resolution_card_y = preview_y + 84;
      gui_draw_rect(panel_x, resolution_card_y, panel_w, 96, 0x252535);
      gui_draw_string(panel_x + 16, resolution_card_y + 12, "Display resolution",
                      0x89B4FA, 0x252535);
      gui_draw_string(panel_x + 16, resolution_card_y + 28, "Current:", 0xCBD5E1,
                      0x252535);
      gui_draw_string(panel_x + 76, resolution_card_y + 28, resolution, 0xFFFFFF,
                      0x252535);
      gui_draw_string(panel_x + 210, resolution_card_y + 28, "Selected:", 0xCBD5E1,
                      0x252535);
      gui_draw_string(panel_x + 282, resolution_card_y + 28,
                      settings_resolution_options[settings_resolution_pending_idx].label,
                      0xA5B4FC, 0x252535);

      for (int i = 0; i < SETTINGS_RESOLUTION_OPTION_COUNT; i++) {
        int bx, by, bw, bh;
        uint32_t bg;
        uint32_t fg;
        settings_resolution_button_bounds(panel_x, panel_y, i, &bx, &by, &bw, &bh);
        bg = i == settings_resolution_pending_idx
                 ? 0x2563EB
                 : (i == settings_resolution_current_idx ? 0x374151 : 0x1E293B);
        fg = i == settings_resolution_pending_idx ? 0xFFFFFF : 0xCBD5E1;
        gui_draw_rect(bx, by, bw, bh, bg);
        gui_draw_string(bx + 8, by + 7, settings_resolution_options[i].label, fg, bg);
      }

      gui_draw_rect(panel_x + 8, resolution_card_y + 66, 90, 24, 0x1D4ED8);
      gui_draw_string(panel_x + 18, resolution_card_y + 74, "Wallpapers", 0xFFFFFF,
                      0x1D4ED8);
      gui_draw_rect(panel_x + 106, resolution_card_y + 66, 90, 24,
                    gui_blur_effects_requested() ? 0x7C3AED : 0x4B5563);
      gui_draw_string(panel_x + 126, resolution_card_y + 74,
                      gui_blur_effects_requested() ? "Blur On" : "Blur Off",
                      0xFFFFFF,
                      gui_blur_effects_requested() ? 0x7C3AED : 0x4B5563);
      gui_draw_rect(panel_x + 204, resolution_card_y + 66, 90, 24,
                    gui_is_gpu_rendering_enabled() ? 0x0F766E : 0x475569);
      gui_draw_string(panel_x + 228, resolution_card_y + 74,
                      gui_is_gpu_rendering_enabled() ? "GPU On" : "GPU Off",
                      0xFFFFFF,
                      gui_is_gpu_rendering_enabled() ? 0x0F766E : 0x475569);
      gui_draw_rect(panel_x + 302, resolution_card_y + 66, 90, 24, 0x4B5563);
      gui_draw_string(panel_x + 330, resolution_card_y + 74, "Apply",
                      0xFFFFFF, 0x4B5563);
    } else {
      int block_y = panel_y + 72;
      gui_draw_rect(panel_x, block_y, panel_w, 76, 0x252535);
      gui_draw_string(panel_x + 16, block_y + 12, "System status", 0x89B4FA,
                      0x252535);
      gui_draw_string(panel_x + 16, block_y + 32, g_gpu_backend_name, 0xFFFFFF,
                      0x252535);
      gui_draw_string(panel_x + 150, block_y + 32,
                      intel_hda_is_playing() ? "Intel HDA streaming"
                                             : "Intel HDA ready",
                      0xCBD5E1, 0x252535);
      gui_draw_string(panel_x + 16, block_y + 52, "virtio-net / file manager / app store",
                      0xCBD5E1, 0x252535);

      block_y += 90;
      gui_draw_rect(panel_x, block_y, 110, 30, 0x3B82F6);
      gui_draw_string(panel_x + 22, block_y + 9, "Devices", 0xFFFFFF, 0x3B82F6);
      gui_draw_rect(panel_x + 120, block_y, 110, 30, 0x1D4ED8);
      gui_draw_string(panel_x + 146, block_y + 9, "Files", 0xFFFFFF, 0x1D4ED8);
      gui_draw_rect(panel_x + 240, block_y, 110, 30, 0x2563EB);
      gui_draw_string(panel_x + 264, block_y + 9, "App Store", 0xFFFFFF, 0x2563EB);
      gui_draw_rect(panel_x + 360, block_y, 110, 30, 0x6D28D9);
      gui_draw_string(panel_x + 374, block_y + 9, "Reset Dock", 0xFFFFFF, 0x6D28D9);

      block_y += 42;
      gui_draw_rect(panel_x, block_y, 110, 30, 0x4B5563);
      gui_draw_string(panel_x + 32, block_y + 9, "About", 0xFFFFFF, 0x4B5563);
      gui_draw_rect(panel_x + 120, block_y, panel_w - 120, 30, 0x1E293B);
      gui_draw_string(panel_x + 136, block_y + 9, "Use these tools to inspect and restore",
                      0xCBD5E1, 0x1E293B);
    }
  }
  /* Device Manager window */
  else if (win->title[0] == 'D' && win->title[1] == 'e' &&
           win->title[2] == 'v') {
    int yy = content_y + 12;
    char resolution[32];
    char windows_info[32];
    char usb_ports[32];
    char usb_name0[48];
    char usb_name1[48];
    char storage_overview[96];
    char storage_line0[80];
    char storage_line1[80];
    char disk_overview[80];
    int usb_count = 0;
    extern int intel_hda_is_ready(void);
    extern int intel_hda_is_playing(void);
    extern int virtio_net_is_ready(void);
    extern int xhci_is_ready(void);
    extern int xhci_get_port_count(void);
    extern int xhci_get_connected_count(void);
    extern int usb_device_count(void);
    extern int usb_device_info(int idx, uint16_t *vid, uint16_t *pid, char *name,
                               int name_len);
    extern void storage_build_overview(char *buf, int max);
    extern void storage_build_disk_overview(char *buf, int max);
    extern int storage_describe_controller(int index, char *buf, int max);

    build_resolution_string(resolution, primary_display.width,
                            primary_display.height);
    build_windows_string(windows_info);
    build_device_ports_string(usb_ports, xhci_get_connected_count(),
                              xhci_get_port_count());
    usb_name0[0] = '\0';
    usb_name1[0] = '\0';
    usb_count = usb_device_count();
    if (usb_count > 0)
      usb_device_info(0, NULL, NULL, usb_name0, sizeof(usb_name0));
    if (usb_count > 1)
      usb_device_info(1, NULL, NULL, usb_name1, sizeof(usb_name1));
    storage_build_overview(storage_overview, sizeof(storage_overview));
    storage_build_disk_overview(disk_overview, sizeof(disk_overview));
    if (storage_describe_controller(0, storage_line0, sizeof(storage_line0)) !=
        0) {
      str_copy_safe(storage_line0, "No disk controllers registered",
                    sizeof(storage_line0));
    }
    if (storage_describe_controller(1, storage_line1, sizeof(storage_line1)) !=
        0) {
      storage_line1[0] = '\0';
    }

    gui_draw_string(content_x + 12, yy, "Device Manager", 0xFFFFFF, THEME_BG);
    yy += 28;

    gui_draw_rect(content_x + 10, yy, content_w - 20, 52, 0x252535);
    gui_draw_string(content_x + 20, yy + 8, "Display Adapter", 0x89B4FA,
                    0x252535);
    gui_draw_string(content_x + 20, yy + 28, "Framebuffer compositor active",
                    0xCDD6F4, 0x252535);
    gui_draw_string(content_x + content_w - 150, yy + 28, resolution, 0xCDD6F4,
                    0x252535);
    yy += 62;

    gui_draw_rect(content_x + 10, yy, content_w - 20, 68, 0x252535);
    gui_draw_string(content_x + 20, yy + 8, "Storage", 0x89B4FA, 0x252535);
    gui_draw_string(content_x + 20, yy + 26, storage_overview, 0xCDD6F4,
                    0x252535);
    gui_draw_string(content_x + content_w - 170, yy + 26, disk_overview,
                    0xA6E3A1, 0x252535);
    gui_draw_string(content_x + 20, yy + 42, storage_line0, 0xCDD6F4,
                    0x252535);
    if (storage_line1[0]) {
      gui_draw_string(content_x + 20, yy + 56, storage_line1, 0xCDD6F4,
                      0x252535);
    }
    yy += 78;

    gui_draw_rect(content_x + 10, yy, content_w - 20, 52, 0x252535);
    gui_draw_string(content_x + 20, yy + 8, "Input Devices", 0x89B4FA,
                    0x252535);
    gui_draw_string(content_x + 20, yy + 28,
                    "Keyboard + pointer input subsystem online", 0xCDD6F4,
                    0x252535);
    gui_draw_string(content_x + content_w - 150, yy + 28, windows_info,
                    0xA6E3A1, 0x252535);
    yy += 62;

    gui_draw_rect(content_x + 10, yy, content_w - 20, 52, 0x252535);
    gui_draw_string(content_x + 20, yy + 8, "Audio Controller", 0x89B4FA,
                    0x252535);
    gui_draw_string(content_x + 20, yy + 28,
                    intel_hda_is_ready() ? "Intel HDA controller present"
                                         : "Intel HDA controller not detected",
                    intel_hda_is_ready() ? 0xCDD6F4 : 0xF38BA8, 0x252535);
    gui_draw_string(content_x + content_w - 150, yy + 28,
                    intel_hda_is_playing() ? "Playing" : "Idle",
                    intel_hda_is_playing() ? 0xA6E3A1 : 0x6C7086, 0x252535);
    yy += 62;

    gui_draw_rect(content_x + 10, yy, content_w - 20, 52, 0x252535);
    gui_draw_string(content_x + 20, yy + 8, "Network Adapter", 0x89B4FA,
                    0x252535);
    gui_draw_string(content_x + 20, yy + 28,
                    virtio_net_is_ready() ? "virtio-net interface ready"
                                          : "virtio-net interface offline",
                    virtio_net_is_ready() ? 0xCDD6F4 : 0xF38BA8, 0x252535);
    gui_draw_string(content_x + content_w - 150, yy + 28,
                    virtio_net_is_ready() ? "eth0 / NAT" : "Unavailable",
                    virtio_net_is_ready() ? 0xA6E3A1 : 0x6C7086, 0x252535);
    yy += 62;

    gui_draw_rect(content_x + 10, yy, content_w - 20, 84, 0x252535);
    gui_draw_string(content_x + 20, yy + 8, "USB Host Controller", 0x89B4FA,
                    0x252535);
    gui_draw_string(content_x + 20, yy + 28,
                    xhci_is_ready() ? "xHCI controller initialized"
                                    : "xHCI controller unavailable",
                    xhci_is_ready() ? 0xCDD6F4 : 0xF38BA8, 0x252535);
    gui_draw_string(content_x + content_w - 150, yy + 28, usb_ports,
                    xhci_is_ready() ? 0xA6E3A1 : 0x6C7086, 0x252535);
    if (!xhci_is_ready()) {
      gui_draw_string(content_x + 20, yy + 48, "No USB enumeration available",
                      0x6C7086, 0x252535);
    } else if (usb_count <= 0) {
      gui_draw_string(content_x + 20, yy + 48, "No USB devices enumerated",
                      0x6C7086, 0x252535);
    } else {
      gui_draw_string(content_x + 20, yy + 48, usb_name0, 0xCDD6F4, 0x252535);
      if (usb_count > 1) {
        gui_draw_string(content_x + 20, yy + 64, usb_name1, 0x94A3B8, 0x252535);
      } else {
        gui_draw_string(content_x + 20, yy + 64, "1 device detected",
                        0x6C7086, 0x252535);
      }
    }
  }
  /* Clock window */
  else if (win->title[0] == 'C' && win->title[1] == 'l' &&
           win->title[2] == 'o') {
    draw_clock_widget(content_x, content_y, content_w, content_h, THEME_BG);
  }
  /* Game window */
  else if (win->title[0] == 'G' && win->title[1] == 'a' &&
           win->title[2] == 'm') {
    int yy = content_y + 15;

    /* Header */
    if (snake_game_over) {
      gui_draw_string(content_x + 12, yy, "GAME OVER! Press any key", 0xEF4444,
                      THEME_BG);
    } else {
      gui_draw_string(content_x + 12, yy, "Snake Game - WASD to move", 0x89B4FA,
                      THEME_BG);
    }
    yy += 28;

    /* Game area with border */
    int game_w = content_w - 24;
    int game_h = 180;
    int game_x = content_x + 12;
    int game_y = yy;
    gui_draw_rect_outline(game_x, game_y, game_w, game_h, 0x3B82F6, 2);
    gui_draw_rect(game_x + 2, game_y + 2, game_w - 4, game_h - 4, 0x101018);

    /* Calculate cell size */
    int cell_w = (game_w - 4) / SNAKE_GRID_W;
    int cell_h = (game_h - 4) / SNAKE_GRID_H;

    /* Draw food */
    int fx = game_x + 2 + snake_food_x * cell_w + 2;
    int fy = game_y + 2 + snake_food_y * cell_h + 2;
    gui_draw_rect(fx, fy, cell_w - 4, cell_h - 4, 0xEF4444);

    /* Draw snake */
    for (int i = 0; i < snake_len; i++) {
      int sx = game_x + 2 + snake_x[i] * cell_w + 1;
      int sy = game_y + 2 + snake_y[i] * cell_h + 1;
      uint32_t color = (i == 0) ? 0x22C55E : 0x16A34A; /* Head is brighter */
      if (i == snake_len - 1)
        color = 0x15803D; /* Tail is darker */
      gui_draw_rect(sx, sy, cell_w - 2, cell_h - 2, color);
    }

    /* Score display */
    char score_str[32];
    score_str[0] = 'S';
    score_str[1] = 'c';
    score_str[2] = 'o';
    score_str[3] = 'r';
    score_str[4] = 'e';
    score_str[5] = ':';
    score_str[6] = ' ';
    /* Convert score to string */
    int s = snake_score;
    int pos = 7;
    if (s == 0) {
      score_str[pos++] = '0';
    } else {
      int temp[10], ti = 0;
      while (s > 0) {
        temp[ti++] = s % 10;
        s /= 10;
      }
      while (ti > 0) {
        score_str[pos++] = '0' + temp[--ti];
      }
    }
    score_str[pos] = '\0';

    yy += game_h + 8;
    gui_draw_string(game_x + 8, yy - 16, score_str, 0xFFFFFF, 0x101018);

    /* Controls hint */
    gui_draw_string(content_x + 12, yy, "WASD or Arrow keys", 0x6C7086,
                    THEME_BG);
  }
  /* Terminal - use term_render from terminal.c for proper output display */
  else if (win->title[0] == 'T' && win->title[1] == 'e' &&
           win->title[2] == 'r') {
    /* Use window's own terminal if available, otherwise fall back to active */
    struct terminal *term = (struct terminal *)win->userdata;
    if (!term) {
      term = term_get_active();
    }
    if (term) {
      /* Update terminal's content area to match window position */
      term_set_content_pos(term, content_x, content_y);
      term_render(term);
    } else {
      /* Fallback if no terminal */
      gui_draw_string(content_x + 10, content_y + 10,
                      "Terminal not initialized", 0xFF0000, THEME_BG);
    }
  }
  /* Notepad - Modern Design with Full Toolbar */

  else if ((win->title[0] == 'N' && win->title[1] == 'o' &&
            win->title[2] == 't') ||
           (win->title[0] == 'R' && win->title[1] == 'e' &&
            win->title[2] == 'n')) {

    /* Modern dark toolbar */
    int toolbar_h = 62;
    gui_draw_rect(content_x, content_y, content_w, toolbar_h, 0x2D2D30);

    /* Toolbar buttons with modern styling */
    int btn_y = content_y + 6;
    int btn_h = 24;
    int btn_spacing = 4;
    int bx = content_x + 8;

    /* File operations group */
    /* New button */
    gui_draw_rect(bx, btn_y, 50, btn_h, 0x3E3E42);
    gui_draw_rect(bx, btn_y, 50, 1, 0x505054);
    gui_draw_string(bx + 13, btn_y + 5, "New", 0xCCCCCC, 0x3E3E42);
    bx += 50 + btn_spacing;

    /* Open button */
    gui_draw_rect(bx, btn_y, 50, btn_h, 0x3E3E42);
    gui_draw_rect(bx, btn_y, 50, 1, 0x505054);
    gui_draw_string(bx + 10, btn_y + 5, "Open", 0xCCCCCC, 0x3E3E42);
    bx += 50 + btn_spacing;

    /* Save button - highlighted */
    gui_draw_rect(bx, btn_y, 50, btn_h, 0x0E639C);
    gui_draw_rect(bx, btn_y, 50, 1, 0x1E7BB8);
    gui_draw_string(bx + 10, btn_y + 5, "Save", 0xFFFFFF, 0x0E639C);
    bx += 50 + btn_spacing;

    /* Save As button */
    gui_draw_rect(bx, btn_y, 64, btn_h, 0x3E3E42);
    gui_draw_rect(bx, btn_y, 64, 1, 0x505054);
    gui_draw_string(bx + 8, btn_y + 5, "Save As", 0xCCCCCC, 0x3E3E42);
    bx += 64 + btn_spacing;

    /* Separator */
    bx += 8;
    gui_draw_rect(bx, btn_y + 2, 1, btn_h - 4, 0x505054);
    bx += 12;

    /* Edit operations group */
    /* Cut button */
    gui_draw_rect(bx, btn_y, 42, btn_h, 0x3E3E42);
    gui_draw_rect(bx, btn_y, 42, 1, 0x505054);
    gui_draw_string(bx + 10, btn_y + 5, "Cut", 0xCCCCCC, 0x3E3E42);
    bx += 42 + btn_spacing;

    /* Copy button */
    gui_draw_rect(bx, btn_y, 50, btn_h, 0x3E3E42);
    gui_draw_rect(bx, btn_y, 50, 1, 0x505054);
    gui_draw_string(bx + 10, btn_y + 5, "Copy", 0xCCCCCC, 0x3E3E42);
    bx += 50 + btn_spacing;

    /* Paste button */
    gui_draw_rect(bx, btn_y, 55, btn_h, 0x3E3E42);
    gui_draw_rect(bx, btn_y, 55, 1, 0x505054);
    gui_draw_string(bx + 8, btn_y + 5, "Paste", 0xCCCCCC, 0x3E3E42);

    if (win->title[0] == 'N') {
      gui_draw_string(content_x + 10, content_y + 40,
                      notepad_filepath[0] ? notepad_filepath : "Untitled document",
                      0xA9B7C6, 0x2D2D30);
      gui_draw_string(content_x + content_w - 170, content_y + 40,
                      notepad_dirty ? "Modified" : "Saved", 0xD7BA7D, 0x2D2D30);
    }

    /* Text editing area with modern styling */
    int text_area_y = content_y + toolbar_h + 2;
    int status_h = 22;
    int text_area_h = content_h - toolbar_h - status_h - 4;

    /* Text area background with subtle border */
    gui_draw_rect(content_x + 4, text_area_y, content_w - 8, text_area_h,
                  0x1E1E1E);
    gui_draw_rect(content_x + 4, text_area_y, content_w - 8, 1, 0x3C3C3C);
    gui_draw_rect(content_x + 4, text_area_y, 1, text_area_h, 0x3C3C3C);

    /* Line number gutter */
    int gutter_w = 40;
    gui_draw_rect(content_x + 5, text_area_y + 1, gutter_w - 2, text_area_h - 2,
                  0x252526);

    /* Draw line numbers */
    int line_num = 1;
    int max_lines = (text_area_h - 8) / 16;
    for (int i = 0; i < max_lines && i < 20; i++) {
      char num_str[4] = {0};
      int n = line_num + i;
      if (n < 10) {
        num_str[0] = '0' + n;
      } else {
        num_str[0] = '0' + (n / 10);
        num_str[1] = '0' + (n % 10);
      }
      gui_draw_string(content_x + 20, text_area_y + 4 + i * 16, num_str,
                      0x858585, 0x252526);
    }

    /* Draw text with syntax-like highlighting */
    int tx = content_x + 8 + gutter_w;
    int ty = text_area_y + 4;
    int max_x = content_x + content_w - 12;
    int max_y = text_area_y + text_area_h - 8;
    char *target_text = (win->title[0] == 'N') ? notepad_text : rename_text;
    int target_cursor = (win->title[0] == 'N') ? notepad_cursor : rename_cursor;

    int line_count = 1;
    int col_count = 1;
    for (int i = 0; i < target_cursor && ty < max_y; i++) {
      char c = target_text[i];
      if (c == '\n') {
        tx = content_x + 8 + gutter_w;
        ty += 16;
        line_count++;
        col_count = 1;
      } else {
        gui_draw_char(tx, ty, c, 0xD4D4D4, 0x1E1E1E);
        tx += 8;
        col_count++;
        if (tx >= max_x) {
          tx = content_x + 8 + gutter_w;
          ty += 16;
          line_count++;
          col_count = 1;
        }
      }
    }

    /* Cursor with blink effect */
    if (win->focused) {
      gui_draw_rect(tx, ty, 2, 14, 0x569CD6);
    }

    /* Status bar */
    int status_y = content_y + content_h - status_h;
    gui_draw_rect(content_x, status_y, content_w, status_h, 0x007ACC);

    /* Status text */
    char status_text[64] = "Ln ";
    int si = 3;
    if (line_count < 10) {
      status_text[si++] = '0' + line_count;
    } else {
      status_text[si++] = '0' + (line_count / 10);
      status_text[si++] = '0' + (line_count % 10);
    }
    status_text[si++] = ',';
    status_text[si++] = ' ';
    status_text[si++] = 'C';
    status_text[si++] = 'o';
    status_text[si++] = 'l';
    status_text[si++] = ' ';
    int col = col_count;
    if (col < 10) {
      status_text[si++] = '0' + col;
    } else {
      status_text[si++] = '0' + (col / 10);
      status_text[si++] = '0' + (col % 10);
    }
    status_text[si] = '\0';
    gui_draw_string(content_x + 12, status_y + 4, status_text, 0xFFFFFF,
                    0x007ACC);

    /* File type indicator */
    if (win->title[0] == 'N') {
      char right_status[80] = "UTF-8  ";
      if (notepad_dirty) {
        notepad_append_to_buf(right_status, sizeof(right_status), "Dirty");
      } else {
        notepad_append_to_buf(right_status, sizeof(right_status), "Saved");
      }
      gui_draw_string(content_x + content_w - 110, status_y + 4, right_status,
                      0xFFFFFF, 0x007ACC);
      gui_draw_string(content_x + 110, status_y + 4, notepad_status, 0xFFFFFF,
                      0x007ACC);
    } else {
      gui_draw_string(content_x + content_w - 60, status_y + 4, "UTF-8", 0xFFFFFF,
                      0x007ACC);
    }

    if (win->title[0] == 'N' && notepad_dialog_mode != NOTEPAD_DIALOG_NONE) {
      struct fm_item items[FM_MAX_ITEMS];
      int item_count = fm_collect_items(notepad_dialog_dir, items, FM_MAX_ITEMS);
      int panel_w = content_w - 80;
      int panel_h = content_h - 70;
      int panel_x = content_x + 40;
      int panel_y = content_y + 26;
      int list_x = panel_x + 16;
      int list_y = panel_y + 68;
      int list_w = panel_w - 32;
      int row_h = 22;
      int visible_rows = (panel_h - 156) / row_h;
      if (visible_rows < 4)
        visible_rows = 4;

      gui_draw_rect(content_x + 10, content_y + 10, content_w - 20, content_h - 20,
                    0x101012);
      gui_draw_rect(panel_x, panel_y, panel_w, panel_h, 0x252526);
      gui_draw_rect(panel_x, panel_y, panel_w, 1, 0x3C3C3C);
      gui_draw_rect(panel_x, panel_y, 1, panel_h, 0x3C3C3C);

      gui_draw_string(panel_x + 16, panel_y + 14,
                      notepad_dialog_mode == NOTEPAD_DIALOG_OPEN ? "Open File"
                                                                  : "Save File",
                      0xFFFFFF, 0x252526);
      gui_draw_rect(panel_x + 16, panel_y + 34, 44, 22, 0x3E3E42);
      gui_draw_string(panel_x + 30, panel_y + 40, "Up", 0xCCCCCC, 0x3E3E42);
      gui_draw_string(panel_x + 70, panel_y + 40, notepad_dialog_dir, 0x9CDCFE,
                      0x252526);

      gui_draw_rect(list_x, list_y, list_w, visible_rows * row_h + 4, 0x1E1E1E);
      for (int i = 0; i < item_count && i < visible_rows; i++) {
        int row_y = list_y + 2 + i * row_h;
        uint32_t row_bg =
            str_cmp(notepad_dialog_selected, items[i].name) == 0 ? 0x094771 : 0x1E1E1E;
        gui_draw_rect(list_x + 2, row_y, list_w - 4, row_h - 2, row_bg);
        gui_draw_string(list_x + 8, row_y + 6, items[i].type == 4 ? "[DIR]" : "[FILE]",
                        items[i].type == 4 ? 0x4FC1FF : 0xB5CEA8, row_bg);
        gui_draw_string(list_x + 56, row_y + 6, items[i].name, 0xD4D4D4, row_bg);
      }

      gui_draw_string(panel_x + 16, panel_y + panel_h - 70, "Path", 0xCCCCCC,
                      0x252526);
      gui_draw_rect(panel_x + 16, panel_y + panel_h - 50, panel_w - 140, 24,
                    0x1E1E1E);
      gui_draw_string(panel_x + 24, panel_y + panel_h - 44, notepad_dialog_input,
                      0xDCDCAA, 0x1E1E1E);

      gui_draw_rect(panel_x + panel_w - 110, panel_y + panel_h - 50, 42, 24,
                    0x3E3E42);
      gui_draw_string(panel_x + panel_w - 100, panel_y + panel_h - 44, "Esc",
                      0xCCCCCC, 0x3E3E42);
      gui_draw_rect(panel_x + panel_w - 60, panel_y + panel_h - 50, 44, 24,
                    0x0E639C);
      gui_draw_string(panel_x + panel_w - 50, panel_y + panel_h - 44,
                      notepad_dialog_mode == NOTEPAD_DIALOG_OPEN ? "Open" : "Save",
                      0xFFFFFF, 0x0E639C);
    }
  }
  /* Snake Game */
  else if (win->title[0] == 'S' && win->title[1] == 'n' &&
           win->title[2] == 'a') {
    /* Calculate cell size based on content area */
    int cell_w = (content_w - 20) / SNAKE_GRID_W;
    int cell_h = (content_h - 40) / SNAKE_GRID_H;
    if (cell_w > cell_h)
      cell_w = cell_h;
    else
      cell_h = cell_w;

    int grid_x = content_x + (content_w - cell_w * SNAKE_GRID_W) / 2;
    int grid_y = content_y + 30;

    /* Draw score */
    char score_str[32];
    int si = 0;
    score_str[si++] = 'S';
    score_str[si++] = 'c';
    score_str[si++] = 'o';
    score_str[si++] = 'r';
    score_str[si++] = 'e';
    score_str[si++] = ':';
    score_str[si++] = ' ';
    int s = snake_score;
    if (s == 0)
      score_str[si++] = '0';
    else {
      char tmp[8];
      int ti = 0;
      while (s > 0) {
        tmp[ti++] = '0' + (s % 10);
        s /= 10;
      }
      while (ti > 0)
        score_str[si++] = tmp[--ti];
    }
    score_str[si] = '\0';
    gui_draw_string(content_x + 10, content_y + 8, score_str, 0xF9E2AF,
                    THEME_BG);

    /* Draw grid background */
    gui_draw_rect(grid_x - 2, grid_y - 2, cell_w * SNAKE_GRID_W + 4,
                  cell_h * SNAKE_GRID_H + 4, 0x1E1E2E);

    /* Draw snake body */
    for (int i = 0; i < snake_len; i++) {
      int sx = grid_x + snake_x[i] * cell_w + 1;
      int sy = grid_y + snake_y[i] * cell_h + 1;
      uint32_t color = (i == 0) ? 0x94E2D5 : 0xA6E3A1; /* Head vs body */
      gui_draw_rect(sx, sy, cell_w - 2, cell_h - 2, color);
    }

    /* Draw food */
    int fx = grid_x + snake_food_x * cell_w + 1;
    int fy = grid_y + snake_food_y * cell_h + 1;
    gui_draw_rect(fx, fy, cell_w - 2, cell_h - 2, 0xF38BA8);

    /* Game over message */
    if (snake_game_over) {
      gui_draw_string(content_x + content_w / 2 - 40,
                      content_y + content_h - 30, "GAME OVER!", 0xF38BA8,
                      THEME_BG);
      gui_draw_string(content_x + content_w / 2 - 60,
                      content_y + content_h - 14, "Press R to restart",
                      0x6C7086, THEME_BG);
    } else {
      gui_draw_string(content_x + 10, content_y + content_h - 14,
                      "Arrow keys to move", 0x6C7086, THEME_BG);
    }
  }
  /* Clock */
  else if (win->title[0] == 'C' && win->title[1] == 'l' &&
           win->title[2] == 'o') {
    draw_clock_widget(content_x, content_y, content_w, content_h, THEME_BG);
  }

  /* Background Settings Window */
  else if (win->title[0] == 'B' && win->title[1] == 'a' &&
           win->title[2] == 'c') {
    /* Header */
    gui_draw_rect(content_x, content_y, content_w, 40, 0x27272A);
    gui_draw_string(content_x + 15, content_y + 12, "Choose Wallpaper",
                    0xFAFAFA, 0x27272A);

    /* Wallpaper grid - 5 columns, 2 rows */
    int grid_x = content_x + 15;
    int grid_y = content_y + 55;
    int thumb_w = 65;
    int thumb_h = 45;
    int gap = 10;

    for (int i = 0; i < NUM_WALLPAPERS; i++) {
      int col = i % 5;
      int row = i / 5;
      int tx = grid_x + col * (thumb_w + gap);
      int ty = grid_y + row * (thumb_h + gap + 20);

      /* Draw preview based on type */
      if (wallpapers[i].type == 1) {
        /* Use cached thumbnail */
        load_thumbnails(); /* Load once */
        media_image_t *thumb_img = &thumbnail_cache[i];

        if (thumb_img->pixels && thumb_img->width > 0) {
          /* Draw scaled image from cache */
          for (int py = 0; py < thumb_h; py++) {
            for (int px = 0; px < thumb_w; px++) {
              int src_x = (px * thumb_img->width) / thumb_w;
              int src_y = (py * thumb_img->height) / thumb_h;
              if (src_x < (int)thumb_img->width &&
                  src_y < (int)thumb_img->height) {
                uint32_t pixel =
                    thumb_img->pixels[src_y * thumb_img->width + src_x];
                draw_image_pixel(tx + px, ty + py, pixel);
              }
            }
          }
        } else {
          /* Fallback - gray with "?" */
          gui_draw_rect(tx, ty, thumb_w, thumb_h, 0x3A3A4A);
          gui_draw_char(tx + thumb_w / 2 - 4, ty + thumb_h / 2 - 8, '?',
                        0x888888, 0x3A3A4A);
        }
      } else {
        /* Gradient wallpaper - draw gradient preview */
        for (int py = 0; py < thumb_h; py++) {
          int progress = (py * 256) / thumb_h;
          uint8_t pr = wallpapers[i].tr +
                       ((wallpapers[i].br - wallpapers[i].tr) * progress) / 256;
          uint8_t pg = wallpapers[i].tg +
                       ((wallpapers[i].bg - wallpapers[i].tg) * progress) / 256;
          uint8_t pb = wallpapers[i].tb +
                       ((wallpapers[i].bb - wallpapers[i].tb) * progress) / 256;
          uint32_t pcolor = (pr << 16) | (pg << 8) | pb;
          gui_draw_rect(tx, ty + py, thumb_w, 1, pcolor);
        }
      }

      /* Border - highlight if selected */
      uint32_t border_color = (i == current_wallpaper) ? 0x6366F1 : 0x52525B;
      gui_draw_rect(tx - 2, ty - 2, thumb_w + 4, 2, border_color);
      gui_draw_rect(tx - 2, ty + thumb_h, thumb_w + 4, 2, border_color);
      gui_draw_rect(tx - 2, ty - 2, 2, thumb_h + 4, border_color);
      gui_draw_rect(tx + thumb_w, ty - 2, 2, thumb_h + 4, border_color);

      /* Label */
      gui_draw_string(tx, ty + thumb_h + 4, wallpapers[i].name, 0xA1A1AA,
                      THEME_BG);
    }

    /* Current selection text */
    gui_draw_string(content_x + 15, content_y + content_h - 30,
                    "Click to select wallpaper", 0x71717A, THEME_BG);
  }

  /* Call window's draw callback if set */
  if (win->on_draw) {
    win->on_draw(win);
  }

  /* Draw resize grip in bottom-right corner */
  {
    int gx = x + w - 14;
    int gy = y + h - 14;
    uint32_t grip_color = win->focused ? 0x888888 : 0x666666;
    /* Draw diagonal grip lines (macOS style) */
    for (int i = 0; i < 3; i++) {
      int offset = i * 4;
      /* Diagonal line from bottom-left to top-right */
      gui_draw_line(gx + offset, gy + 10, gx + 10, gy + offset, grip_color);
    }
  }

  gui_restore_clip_rect(prev_clip);
}

/* ===================================================================== */
/* Desktop with Menu Bar and Dock */
/* ===================================================================== */

/* Menu dropdown state */
static int menu_open = 0; /* 0=closed, 1=main menu open */
static int main_menu_power_open = 0;

#define MAIN_MENU_W 364
#define MAIN_MENU_H 350
#define MAIN_MENU_HEADER_H 60
#define MAIN_MENU_LEFT_W 214
#define MAIN_MENU_ROW_H 34
#define MAIN_MENU_RIGHT_ROW_H 28

enum {
  MAIN_MENU_ITEM_NONE = -1,
  MAIN_MENU_ITEM_ABOUT = 0,
  MAIN_MENU_ITEM_TERMINAL,
  MAIN_MENU_ITEM_FILES,
  MAIN_MENU_ITEM_NOTES,
  MAIN_MENU_ITEM_SETTINGS,
  MAIN_MENU_ITEM_BROWSER,
  MAIN_MENU_ITEM_APPSTORE,
  MAIN_MENU_ITEM_POWER,
  MAIN_MENU_ITEM_POWER_SHUTDOWN,
  MAIN_MENU_ITEM_POWER_RESTART,
  MAIN_MENU_ITEM_COUNT
};

static void main_menu_launcher_button_rect(int *x, int *y, int *w, int *h) {
  int dock_y = (int)primary_display.height - DOCK_HEIGHT;
  int size = 50;

  if (x)
    *x = 14;
  if (y)
    *y = dock_y + (DOCK_HEIGHT - size) / 2;
  if (w)
    *w = size;
  if (h)
    *h = size;
}

static int main_menu_panel_x(void) {
  int launcher_x, launcher_y, launcher_w, launcher_h;
  int x;

  main_menu_launcher_button_rect(&launcher_x, &launcher_y, &launcher_w,
                                 &launcher_h);
  x = launcher_x - 6;
  if (x < 8)
    x = 8;
  if (x + MAIN_MENU_W > (int)primary_display.width - 8)
    x = (int)primary_display.width - MAIN_MENU_W - 8;
  return x;
}

static int main_menu_panel_y(void) {
  int dock_y = (int)primary_display.height - DOCK_HEIGHT;
  int y = dock_y - MAIN_MENU_H + 10;
  if (y < MENU_BAR_HEIGHT + 8)
    y = MENU_BAR_HEIGHT + 8;
  return y;
}

static void main_menu_panel_rect(int *x, int *y, int *w, int *h) {
  if (x)
    *x = main_menu_panel_x();
  if (y)
    *y = main_menu_panel_y();
  if (w)
    *w = MAIN_MENU_W;
  if (h)
    *h = MAIN_MENU_H;
}

static int main_menu_contains_point(int x, int y) {
  int panel_x, panel_y, panel_w, panel_h;
  main_menu_panel_rect(&panel_x, &panel_y, &panel_w, &panel_h);
  return x >= panel_x && x < panel_x + panel_w && y >= panel_y &&
         y < panel_y + panel_h;
}

static int main_menu_item_bounds(int item_index, int *x, int *y, int *w,
                                 int *h) {
  int panel_x = main_menu_panel_x();
  int panel_y = main_menu_panel_y();
  int left_x = panel_x + 12;
  int left_y = panel_y + MAIN_MENU_HEADER_H + 14;
  int left_w = MAIN_MENU_LEFT_W - 24;
  int right_x = panel_x + MAIN_MENU_LEFT_W + 10;
  int right_y = panel_y + MAIN_MENU_HEADER_H + 18;
  int right_w = MAIN_MENU_W - MAIN_MENU_LEFT_W - 22;

  if (item_index < 0 || item_index >= MAIN_MENU_ITEM_COUNT)
    return 0;

  switch (item_index) {
  case MAIN_MENU_ITEM_TERMINAL:
  case MAIN_MENU_ITEM_FILES:
  case MAIN_MENU_ITEM_NOTES:
  case MAIN_MENU_ITEM_SETTINGS:
  case MAIN_MENU_ITEM_BROWSER:
  case MAIN_MENU_ITEM_APPSTORE:
    if (x)
      *x = left_x;
    if (y)
      *y = left_y + (item_index - MAIN_MENU_ITEM_TERMINAL) * MAIN_MENU_ROW_H;
    if (w)
      *w = left_w;
    if (h)
      *h = MAIN_MENU_ROW_H - 2;
    return 1;
  case MAIN_MENU_ITEM_ABOUT:
    if (x)
      *x = right_x;
    if (y)
      *y = right_y;
    if (w)
      *w = right_w;
    if (h)
      *h = MAIN_MENU_RIGHT_ROW_H;
    return 1;
  case MAIN_MENU_ITEM_POWER:
    if (x)
      *x = right_x;
    if (y)
      *y = panel_y + MAIN_MENU_H - 108;
    if (w)
      *w = right_w;
    if (h)
      *h = MAIN_MENU_RIGHT_ROW_H;
    return 1;
  default:
    return 0;
  }
}

static int main_menu_power_item_bounds(int item_index, int *x, int *y, int *w,
                                       int *h) {
  int power_x, power_y, power_w, power_h;

  if (!main_menu_power_open)
    return 0;
  if (!main_menu_item_bounds(MAIN_MENU_ITEM_POWER, &power_x, &power_y, &power_w,
                             &power_h))
    return 0;

  switch (item_index) {
  case MAIN_MENU_ITEM_POWER_SHUTDOWN:
    if (x)
      *x = power_x + 8;
    if (y)
      *y = power_y + power_h + 8;
    if (w)
      *w = power_w - 8;
    if (h)
      *h = MAIN_MENU_RIGHT_ROW_H;
    return 1;
  case MAIN_MENU_ITEM_POWER_RESTART:
    if (x)
      *x = power_x + 8;
    if (y)
      *y = power_y + power_h + 8 + MAIN_MENU_RIGHT_ROW_H + 4;
    if (w)
      *w = power_w - 8;
    if (h)
      *h = MAIN_MENU_RIGHT_ROW_H;
    return 1;
  default:
    return 0;
  }
}

static int main_menu_item_at(int x, int y) {
  if (!menu_open)
    return MAIN_MENU_ITEM_NONE;
  if (!main_menu_contains_point(x, y))
    return MAIN_MENU_ITEM_NONE;

  for (int i = 0; i < MAIN_MENU_ITEM_COUNT; i++) {
    int item_x, item_y, item_w, item_h;
    if (!main_menu_item_bounds(i, &item_x, &item_y, &item_w, &item_h))
      continue;
    if (x >= item_x && x < item_x + item_w && y >= item_y &&
        y < item_y + item_h)
      return i;
  }
  for (int i = MAIN_MENU_ITEM_POWER_SHUTDOWN;
       i <= MAIN_MENU_ITEM_POWER_RESTART; i++) {
    int item_x, item_y, item_w, item_h;
    if (!main_menu_power_item_bounds(i, &item_x, &item_y, &item_w, &item_h))
      continue;
    if (x >= item_x && x < item_x + item_w && y >= item_y &&
        y < item_y + item_h)
      return i;
  }
  return MAIN_MENU_ITEM_NONE;
}

static void draw_main_menu_row(int item_index, const char *label,
                               const char *subtitle, uint32_t accent,
                               int compact) {
  int row_x, row_y, row_w, row_h;
  int hovered = (main_menu_item_at(mouse_x, mouse_y) == item_index);

  if (!main_menu_item_bounds(item_index, &row_x, &row_y, &row_w, &row_h))
    return;

  if (!compact) {
    gui_fill_rect_alpha(row_x, row_y, row_w, row_h,
                        hovered ? 0x4A355177 : 0x24202A38);
    gui_draw_rect_outline(row_x, row_y, row_w, row_h,
                          hovered ? 0x8AB7DAFF : 0x304A586B, 1);
    gui_fill_rect_alpha(row_x + 6, row_y + 5, 22, 22, accent | 0x66000000);
    gui_draw_rect_outline(row_x + 6, row_y + 5, 22, 22, 0x90FFFFFF, 1);
    gui_draw_string(row_x + 36, row_y + 6, label, 0xF4F7FB, 0x00000000);
    if (subtitle)
      gui_draw_string(row_x + 36, row_y + 18, subtitle, 0xA7B4C4, 0x00000000);
  } else {
    gui_fill_rect_alpha(row_x, row_y, row_w, row_h,
                        hovered ? 0x42486178 : 0x24313D50);
    gui_draw_rect_outline(row_x, row_y, row_w, row_h,
                          hovered ? 0x8AB7DAFF : 0x304A586B, 1);
    gui_draw_string(row_x + 10, row_y + 7, label,
                    hovered ? 0xFFFFFF : 0xEAF2FF, 0x00000000);
    if (subtitle)
      gui_draw_string(row_x + row_w - 56, row_y + 7, subtitle,
                      hovered ? 0xE6F4FF : 0xBAC8D7, 0x00000000);
  }
}

static void draw_main_menu_power_dropdown(void) {
  int power_x, power_y, power_w, power_h;
  int item_x, item_y, item_w, item_h;
  int hovered_item = main_menu_item_at(mouse_x, mouse_y);

  if (!main_menu_power_open)
    return;
  if (!main_menu_item_bounds(MAIN_MENU_ITEM_POWER, &power_x, &power_y, &power_w,
                             &power_h))
    return;

  gui_fill_rect_alpha(power_x + 6, power_y + power_h + 6, power_w - 2, 64,
                      0x28070C14);
  gui_draw_glass_panel(power_x, power_y + power_h + 4, power_w, 62, 0x6A2C3446,
                       0x24FFFFFF, 0x8C75839A, 1);

  if (main_menu_power_item_bounds(MAIN_MENU_ITEM_POWER_SHUTDOWN, &item_x, &item_y,
                                  &item_w, &item_h)) {
    if (hovered_item == MAIN_MENU_ITEM_POWER_SHUTDOWN) {
      gui_fill_rect_alpha(item_x, item_y, item_w, item_h, 0x50C84C4C);
      gui_draw_rect_outline(item_x, item_y, item_w, item_h, 0xA5F6B0B0, 1);
    }
    gui_draw_string(item_x + 8, item_y + 7, "Shutdown", 0xFFFFFF, 0x00000000);
  }

  if (main_menu_power_item_bounds(MAIN_MENU_ITEM_POWER_RESTART, &item_x, &item_y,
                                  &item_w, &item_h)) {
    if (hovered_item == MAIN_MENU_ITEM_POWER_RESTART) {
      gui_fill_rect_alpha(item_x, item_y, item_w, item_h, 0x50D98F38);
      gui_draw_rect_outline(item_x, item_y, item_w, item_h, 0xA8FFE2B6, 1);
    }
    gui_draw_string(item_x + 8, item_y + 7, "Restart", 0xFFFFFF, 0x00000000);
  }
}

static void draw_main_menu_panel(void) {
  int panel_x, panel_y, panel_w, panel_h;
  int launcher_x, launcher_y, launcher_w, launcher_h;
  int connector_x;

  if (!menu_open)
    return;

  main_menu_panel_rect(&panel_x, &panel_y, &panel_w, &panel_h);
  main_menu_launcher_button_rect(&launcher_x, &launcher_y, &launcher_w,
                                 &launcher_h);
  connector_x = launcher_x + launcher_w / 2 - 10;
  if (connector_x < panel_x + 20)
    connector_x = panel_x + 20;
  if (connector_x > panel_x + panel_w - 20)
    connector_x = panel_x + panel_w - 20;

  gui_fill_rect_alpha(panel_x + 8, panel_y + 8, panel_w, panel_h + 2,
                      0x28050910);
  gui_fill_rect_alpha(connector_x + 4, panel_y + panel_h - 2, 20, 16,
                      0x24050910);

  gui_draw_glass_panel(panel_x, panel_y, panel_w, panel_h, 0x6A2C3446,
                       0x42FFFFFF, 0x8C75839A, 2);
  gui_fill_rect_alpha(panel_x + BORDER_WIDTH, panel_y + BORDER_WIDTH,
                      panel_w - BORDER_WIDTH * 2, TITLEBAR_HEIGHT, 0x344D6488);
  gui_fill_rect_alpha(panel_x + BORDER_WIDTH,
                      panel_y + BORDER_WIDTH + TITLEBAR_HEIGHT - 1,
                      panel_w - BORDER_WIDTH * 2, 1, 0x48DCE8F5);

  {
    int btn_cy = panel_y + BORDER_WIDTH + TITLEBAR_HEIGHT / 2;
    draw_filled_circle(panel_x + BORDER_WIDTH + 14, btn_cy, 5, COLOR_BTN_CLOSE);
    draw_filled_circle(panel_x + BORDER_WIDTH + 30, btn_cy, 5, COLOR_BTN_MINIMIZE);
    draw_filled_circle(panel_x + BORDER_WIDTH + 46, btn_cy, 5, COLOR_BTN_ZOOM);
  }

  gui_draw_string(panel_x + 76, panel_y + 8, "OS 8", 0xFFF7FBFF,
                  0x00000000);
  gui_fill_rect_alpha(panel_x + BORDER_WIDTH, panel_y + BORDER_WIDTH + TITLEBAR_HEIGHT,
                      MAIN_MENU_LEFT_W - BORDER_WIDTH, panel_h - TITLEBAR_HEIGHT - BORDER_WIDTH * 2,
                      0x141824);
  gui_fill_rect_alpha(panel_x + MAIN_MENU_LEFT_W, panel_y + BORDER_WIDTH + TITLEBAR_HEIGHT,
                      panel_w - MAIN_MENU_LEFT_W - BORDER_WIDTH, panel_h - TITLEBAR_HEIGHT - BORDER_WIDTH * 2,
                      0x1F2937);
  gui_fill_rect_alpha(panel_x + MAIN_MENU_LEFT_W - 1,
                      panel_y + BORDER_WIDTH + TITLEBAR_HEIGHT, 1,
                      panel_h - TITLEBAR_HEIGHT - BORDER_WIDTH * 2, 0x30566C86);

  gui_fill_rect_alpha(panel_x + 14, panel_y + 40, 42, 42, 0x40556F92);
  draw_filled_circle(panel_x + 35, panel_y + 61, 16, 0xFFFFFFFF);
  gui_draw_os_logo(panel_x + 24, panel_y + 50, 2, 0x3B82F6, 0x1D4ED8,
                   0x00000000);
  gui_draw_string(panel_x + 68, panel_y + 45, "Username", 0xFFFFFF,
                  0x00000000);
  gui_draw_string(panel_x + MAIN_MENU_LEFT_W + 16, panel_y + 46, "System",
                  0xFFFFFF, 0x00000000);

  draw_main_menu_row(MAIN_MENU_ITEM_TERMINAL, "Terminal", "Console and shell",
                     0x1F2937, 0);
  draw_main_menu_row(MAIN_MENU_ITEM_FILES, "Files", "Browse folders",
                     0x3B82F6, 0);
  draw_main_menu_row(MAIN_MENU_ITEM_NOTES, "Notepad", "Quick editing",
                     0xFACC15, 0);
  draw_main_menu_row(MAIN_MENU_ITEM_SETTINGS, "Settings", "System controls",
                     0x9CA3AF, 0);
  draw_main_menu_row(MAIN_MENU_ITEM_BROWSER, "Browser", "Open the web",
                     0x0EA5E9, 0);
  draw_main_menu_row(MAIN_MENU_ITEM_APPSTORE, "App Store", "Install apps",
                     0x7C3AED, 0);

  draw_main_menu_row(MAIN_MENU_ITEM_ABOUT, "About OS", NULL, 0x89B4FA, 1);
  draw_main_menu_row(MAIN_MENU_ITEM_POWER, "Power", main_menu_power_open ? "v" : ">",
                     0xDC2626, 1);
  draw_main_menu_power_dropdown();

  gui_fill_rect_alpha(connector_x, panel_y + panel_h - 2, 20, 12, 0xDCE3EEF9);
  gui_fill_rect_alpha(connector_x + 2, panel_y + panel_h, 16, 10, 0x8A495C75);
  gui_draw_rect_outline(connector_x, panel_y + panel_h - 2, 20, 12,
                        0x805A7CA7, 1);
}

static int main_menu_activate(int item_index) {
  switch (item_index) {
  case MAIN_MENU_ITEM_ABOUT:
    gui_create_window("About", 280, 180, 420, 260);
    break;
  case MAIN_MENU_ITEM_TERMINAL:
    gui_launch_app_by_id("terminal");
    break;
  case MAIN_MENU_ITEM_FILES:
    gui_launch_app_by_id("files");
    break;
  case MAIN_MENU_ITEM_NOTES:
    gui_launch_app_by_id("notes");
    break;
  case MAIN_MENU_ITEM_SETTINGS:
    gui_launch_app_by_id("settings");
    break;
  case MAIN_MENU_ITEM_BROWSER:
    gui_launch_app_by_id("browser");
    break;
  case MAIN_MENU_ITEM_APPSTORE:
    gui_launch_app_by_id("appstore");
    break;
  case MAIN_MENU_ITEM_POWER:
    main_menu_power_open = main_menu_power_open ? 0 : 1;
    return 1;
  case MAIN_MENU_ITEM_POWER_SHUTDOWN: {
    extern void arch_poweroff(void);
    main_menu_power_open = 0;
    arch_poweroff();
    break;
  }
  case MAIN_MENU_ITEM_POWER_RESTART: {
    extern void arch_reboot(void);
    main_menu_power_open = 0;
    arch_reboot();
    break;
  }
  default:
    return 0;
  }

  menu_open = 0;
  main_menu_power_open = 0;
  return 1;
}

static void draw_menu_bar(void) {
  return;
}

/* Dock icons */
#include "icons.h"

#define DOCK_ICON_SIZE 44  /* Slightly smaller for more icons */
#define DOCK_ICON_MARGIN 4 /* Padding inside dock pill */
#define DOCK_PADDING 8     /* Space between icons */

/* Draw a 32x32 bitmap icon scaled to display size */
static void draw_icon(int x, int y, int size, const unsigned char *bitmap,
                      uint32_t fg, uint32_t bg) {
  for (int py = 0; py < 32; py++) {
    int draw_y = y + (py * size) / 32;
    for (int px = 0; px < 32; px++) {
      int draw_x = x + (px * size) / 32;
      uint32_t color = bitmap[py * 32 + px] ? fg : bg;
      /* Draw a small block for scaling */
      int next_x = x + ((px + 1) * size) / 32;
      int next_y = y + ((py + 1) * size) / 32;
      for (int dy = draw_y; dy < next_y; dy++) {
        for (int dx = draw_x; dx < next_x; dx++) {
          draw_pixel(dx, dy, color);
        }
      }
      return;
    }
  }
}

/* Draw rounded rectangle helper */
static void draw_rounded_rect(int x, int y, int w, int h, int r,
                              uint32_t color) {
  /* Main body */
  gui_draw_rect(x + r, y, w - 2 * r, h, color);
  gui_draw_rect(x, y + r, r, h - 2 * r, color);
  gui_draw_rect(x + w - r, y + r, r, h - 2 * r, color);

  /* Corners */
  for (int cy = -r; cy <= r; cy++) {
    for (int cx = -r; cx <= r; cx++) {
      if (cx * cx + cy * cy <= r * r) {
        draw_pixel(x + r + cx, y + r + cy, color);
        draw_pixel(x + w - r - 1 + cx, y + r + cy, color);
        draw_pixel(x + r + cx, y + h - r - 1 + cy, color);
        draw_pixel(x + w - r - 1 + cx, y + h - r - 1 + cy, color);
      }
    }
  }
}

/* Draw a filled circle */
static void draw_filled_circle(int cx, int cy, int r, uint32_t color) {
  for (int y = -r; y <= r; y++) {
    for (int x = -r; x <= r; x++) {
      if (x * x + y * y <= r * r) {
        draw_pixel(cx + x, cy + y, color);
      }
    }
  }
}

/* Draw Terminal icon */
static void draw_icon_terminal(int x, int y, int size) {
  int pad = size / 8;
  int inner_x = x + pad;
  int inner_y = y + pad;
  int inner_w = size - pad * 2;
  int inner_h = size - pad * 2;
  gui_draw_rect(inner_x, inner_y, inner_w, inner_h, 0x10161F);
  gui_draw_rect_outline(inner_x, inner_y, inner_w, inner_h, 0xC9D6E8, 1);
  gui_draw_rect(inner_x, inner_y, inner_w, 3, 0x1F2937);
  gui_draw_rect(inner_x + 4, inner_y + 5, 2, 2, 0xEF4444);
  gui_draw_rect(inner_x + 8, inner_y + 5, 2, 2, 0xF59E0B);
  gui_draw_rect(inner_x + 12, inner_y + 5, 2, 2, 0x22C55E);
  gui_draw_line(x + size / 3, y + size / 2 - size / 8, x + size / 2 - 2,
                y + size / 2, 0x86EFAC);
  gui_draw_line(x + size / 3, y + size / 2 + size / 8, x + size / 2 - 2,
                y + size / 2, 0x86EFAC);
  gui_draw_rect(x + size / 2 + 1, y + size * 2 / 3, size / 5, 2, 0xE5E7EB);
}

/* Draw Files icon */
static void draw_icon_files(int x, int y, int size) {
  int m = size / 6;
  gui_draw_rect(x + m, y + m * 2, size - m * 2, size - m * 3, 0xF8D56A);
  gui_draw_rect(x + m + 1, y + m * 2 + 2, size - m * 2 - 2, size / 4,
                0xFFE59A);
  gui_draw_rect(x + m, y + m, size / 3, m + 3, 0xF6C84F);
  gui_draw_rect_outline(x + m, y + m * 2, size - m * 2, size - m * 3,
                        0xB9851C, 1);
}

/* Draw Calculator icon */
static void draw_icon_calc(int x, int y, int size) {
  int pad = size / 7;
  int cell = size / 6;
  gui_draw_rect(x + pad, y + pad, size - pad * 2, size - pad * 2, 0xF3F4F6);
  gui_draw_rect_outline(x + pad, y + pad, size - pad * 2, size - pad * 2,
                        0x9CA3AF, 1);
  gui_draw_rect(x + pad + 3, y + pad + 3, size - pad * 2 - 6, cell + 4,
                0x1F2937);
  gui_draw_rect(x + pad + 6, y + pad + cell + 11, cell, cell, 0xCBD5E1);
  gui_draw_rect(x + pad + 6 + cell + 3, y + pad + cell + 11, cell, cell,
                0xCBD5E1);
  gui_draw_rect(x + pad + 6, y + pad + cell * 2 + 14, cell, cell, 0xCBD5E1);
  gui_draw_rect(x + pad + 6 + cell + 3, y + pad + cell * 2 + 14, cell, cell,
                0xF59E0B);
  gui_draw_rect(x + pad + size / 2 - 4, y + size / 2 - 3, size / 3, 3,
                0x111827);
  gui_draw_rect(x + pad + size / 2 - 4, y + size / 2 + 4, size / 3, 3,
                0x111827);
}

/* Draw Notes icon */
static void draw_icon_notes(int x, int y, int size) {
  int m = size / 6;
  gui_draw_rect(x + m, y + m / 2, size - m * 2, size - m, 0xFFF8D8);
  gui_draw_rect_outline(x + m, y + m / 2, size - m * 2, size - m, 0xD1B74C, 1);
  gui_draw_rect(x + size - m * 2 - 2, y + m / 2, m + 2, m + 2, 0xFDE68A);
  gui_draw_line(x + size - m * 2 - 2, y + m / 2 + m + 2, x + size - m, y + m / 2,
                0xD1B74C);
  for (int i = 0; i < 4; i++) {
    gui_draw_rect(x + m * 2, y + m * 2 + i * (m + 1), size - m * 4, 2,
                  0x9CA3AF);
  }
}

/* Draw Settings icon */
static void draw_icon_settings(int x, int y, int size) {
  int cx = x + size / 2;
  int cy = y + size / 2;
  int r = size / 4;
  int tooth = size / 10;
  draw_filled_circle(cx, cy, r + 3, 0xE5E7EB);
  for (int i = 0; i < 8; i++) {
    int tx = cx;
    int ty = cy;
    if (i == 0) ty -= r + tooth;
    if (i == 1) { tx += r / 2 + tooth / 2; ty -= r / 2 + tooth / 2; }
    if (i == 2) tx += r + tooth;
    if (i == 3) { tx += r / 2 + tooth / 2; ty += r / 2 + tooth / 2; }
    if (i == 4) ty += r + tooth;
    if (i == 5) { tx -= r / 2 + tooth / 2; ty += r / 2 + tooth / 2; }
    if (i == 6) tx -= r + tooth;
    if (i == 7) { tx -= r / 2 + tooth / 2; ty -= r / 2 + tooth / 2; }
    draw_filled_circle(tx, ty, tooth / 2 + 1, 0xD1D5DB);
  }
  draw_filled_circle(cx, cy, r - 1, 0x9CA3AF);
  draw_filled_circle(cx, cy, r / 2, 0xF8FAFC);
}

/* Draw Clock icon */
static void draw_icon_clock(int x, int y, int size) {
  int cx = x + size / 2;
  int cy = y + size / 2;
  int r = size / 3;
  draw_filled_circle(cx, cy, r + 2, 0xE2E8F0);
  draw_filled_circle(cx, cy, r, 0xFFFFFF);
  gui_draw_line(cx, cy, cx, cy - r + 4, 0x111827);
  gui_draw_line(cx, cy, cx + r / 2, cy + r / 4, 0x475569);
  draw_filled_circle(cx, cy, 2, 0xEF4444);
}

/* Draw Snake icon */
static void draw_icon_snake(int x, int y, int size) {
  int body = size / 7;
  draw_filled_circle(x + size / 4, y + size * 2 / 3, body, 0xA7F3D0);
  draw_filled_circle(x + size / 2, y + size / 2, body + 1, 0x6EE7B7);
  draw_filled_circle(x + size * 3 / 4 - 2, y + size / 3, body + 2, 0x22C55E);
  gui_draw_line(x + size / 4, y + size * 2 / 3, x + size / 2, y + size / 2,
                0x22C55E);
  gui_draw_line(x + size / 2, y + size / 2, x + size * 3 / 4 - 2, y + size / 3,
                0x22C55E);
  draw_filled_circle(x + size * 3 / 4 + 1, y + size / 3 - 2, 1, 0x111827);
}

/* Draw Help icon */
static void draw_icon_help(int x, int y, int size) {
  int cx = x + size / 2;
  int cy = y + size / 2;
  int r = size / 3;
  draw_filled_circle(cx, cy, r + 2, 0xDBEAFE);
  draw_filled_circle(cx, cy, r, 0xFFFFFF);
  gui_draw_rect(cx - 2, cy - r / 2, 4, r / 2, 0x2563EB);
  gui_draw_rect(cx - 1, cy - r / 2 - 3, r / 2 + 1, 3, 0x2563EB);
  draw_filled_circle(cx, cy + r / 2 - 1, 2, 0x2563EB);
}

/* Draw Browser/App Store icon */
static void draw_icon_web(int x, int y, int size) {
  int cx = x + size / 2;
  int cy = y + size / 2;
  int r = size / 3;
  draw_filled_circle(cx, cy, r + 2, 0xDBEAFE);
  draw_filled_circle(cx, cy, r, 0xF8FAFC);
  gui_draw_line(cx - r + 4, cy, cx + r - 4, cy, 0x0EA5E9);
  gui_draw_line(cx, cy - r + 4, cx, cy + r - 4, 0x0EA5E9);
  gui_draw_line(cx - r / 2, cy - r + 5, cx + r / 2, cy + r - 5, 0x38BDF8);
}

static void draw_icon_appstore(int x, int y, int size) {
  int bag_x = x + size / 5;
  int bag_y = y + size / 4;
  int bag_w = size - size * 2 / 5;
  int bag_h = size - size / 3;
  draw_filled_circle(x + size / 2, y + size / 2, size / 3 + 2, 0xEDE9FE);
  gui_draw_rect(bag_x, bag_y, bag_w, bag_h, 0x8B5CF6);
  gui_draw_rect_outline(bag_x, bag_y, bag_w, bag_h, 0xC4B5FD, 1);
  gui_draw_line(x + size / 2 - size / 8, bag_y, x + size / 2 + size / 8, bag_y,
                0xE9D5FF);
  gui_draw_line(x + size / 2 - size / 8, bag_y, x + size / 2 - size / 10,
                bag_y - size / 10, 0xE9D5FF);
  gui_draw_line(x + size / 2 + size / 8, bag_y, x + size / 2 + size / 10,
                bag_y - size / 10, 0xE9D5FF);
  gui_draw_line(x + size / 2 - size / 10, y + size / 2, x + size / 2 + size / 10,
                y + size / 2, 0xFFFFFF);
  gui_draw_line(x + size / 2, y + size / 2 - size / 10, x + size / 2,
                y + size / 2 + size / 10, 0xFFFFFF);
}

static void draw_system_app_icon_kind(gui_app_kind_t kind, int x, int y,
                                      int size) {
  switch (kind) {
  case GUI_APP_TERMINAL:
    draw_icon_terminal(x, y, size);
    break;
  case GUI_APP_FILES:
    draw_icon_files(x, y, size);
    break;
  case GUI_APP_CALCULATOR:
    draw_icon_calc(x, y, size);
    break;
  case GUI_APP_NOTES:
    draw_icon_notes(x, y, size);
    break;
  case GUI_APP_SETTINGS:
    draw_icon_settings(x, y, size);
    break;
  case GUI_APP_CLOCK:
    draw_icon_clock(x, y, size);
    break;
  case GUI_APP_SNAKE:
    draw_icon_snake(x, y, size);
    break;
  case GUI_APP_HELP:
    draw_icon_help(x, y, size);
    break;
  case GUI_APP_BROWSER:
    draw_icon_web(x, y, size);
    break;
  case GUI_APP_APPSTORE:
    draw_icon_appstore(x, y, size);
    break;
  }
}

int gui_draw_system_app_icon(const char *app_id, int x, int y, int size) {
  const dock_app_def_t *app = find_catalog_app(app_id);
  if (!app)
    return -1;
  draw_system_app_icon_kind(app->kind, x, y, size);
  return 0;
}

/* Draw dock with fixed icon sizes and a top-rounded background */
static void draw_dock_status_indicators(int dock_y, int dock_h) {
  char time_str[9];
  int hours24, minutes, seconds;
  int panel_w = 122;
  int panel_h = 34;
  int panel_x = (int)primary_display.width - panel_w - 16;
  int panel_y = dock_y + (dock_h - panel_h) / 2;
  int wx = panel_x + 16;
  int wy = panel_y + 16;

  clock_get_time(&hours24, &minutes, &seconds);
  clock_format_time(time_str, hours24, minutes, seconds);
  time_str[5] = '\0';

  gui_fill_rect_alpha(panel_x, panel_y, panel_w, panel_h, 0x28405268);
  gui_draw_rect_outline(panel_x, panel_y, panel_w, panel_h, 0x50738BA3, 1);
  gui_fill_rect_alpha(panel_x, panel_y, panel_w, 1, 0x46FFFFFF);

  /* WiFi status */
  gui_draw_rect(wx, wy + 6, 2, 2, 0xFFFFFF);
  gui_draw_line(wx - 3, wy + 3, wx, wy, 0xFFFFFF);
  gui_draw_line(wx, wy, wx + 3, wy + 3, 0xFFFFFF);
  gui_draw_line(wx - 6, wy, wx, wy - 3, 0xFFFFFF);
  gui_draw_line(wx, wy - 3, wx + 6, wy, 0xFFFFFF);

  /* Small green status dot */
  draw_filled_circle(panel_x + 38, panel_y + panel_h / 2, 3, 0x22C55E);

  gui_draw_string(panel_x + 50, panel_y + 9, time_str, 0xFFFFFF, 0x00000000);
}

static void draw_dock(void) {
  if (!dock_is_visible())
    return;
  if (!gui_is_installer_mode()) {
    load_dock_config();
  }
  if (dock_item_count <= 0)
    return;

  int icon_sizes[MAX_DOCK_ITEMS];
  int dock_h = DOCK_HEIGHT;
  int dock_x = 0;
  int dock_y = (int)primary_display.height - dock_h;
  int launcher_btn_x;
  int launcher_btn_y;
  int launcher_btn_w;
  int launcher_btn_h;
  int icon_start_x;
  int hovered_idx = -1;
  int hovered_launcher = 0;

  main_menu_launcher_button_rect(&launcher_btn_x, &launcher_btn_y,
                                 &launcher_btn_w, &launcher_btn_h);
  icon_start_x = launcher_btn_x + launcher_btn_w + 18;

  for (int i = 0; i < dock_item_count; i++) {
    int base_center_x = icon_start_x + i * (DOCK_ICON_SIZE + DOCK_PADDING) +
                        DOCK_ICON_SIZE / 2;
    int base_icon_x = base_center_x - DOCK_ICON_SIZE / 2;
    int base_icon_y = dock_y + (dock_h - DOCK_ICON_SIZE) / 2;

    icon_sizes[i] = DOCK_ICON_SIZE;
    if (mouse_x >= base_icon_x && mouse_x < base_icon_x + DOCK_ICON_SIZE &&
        mouse_y >= base_icon_y && mouse_y < base_icon_y + DOCK_ICON_SIZE) {
      hovered_idx = i;
    }
  }

  gui_fill_rect_alpha(dock_x, dock_y, primary_display.width, dock_h, 0xA118202C);
  gui_fill_rect_alpha(dock_x, dock_y, primary_display.width, 1, 0x72FFFFFF);
  gui_fill_rect_alpha(dock_x, dock_y + 1, primary_display.width, 1, 0x28495D78);
  gui_fill_rect_alpha(dock_x, dock_y + dock_h - 1, primary_display.width, 1,
                      0x64060A10);

  draw_main_menu_panel();

  if (mouse_x >= launcher_btn_x && mouse_x < launcher_btn_x + launcher_btn_w &&
      mouse_y >= launcher_btn_y && mouse_y < launcher_btn_y + launcher_btn_h) {
    hovered_launcher = 1;
  }

  gui_fill_rect_alpha(launcher_btn_x, launcher_btn_y, launcher_btn_w,
                      launcher_btn_h,
                      menu_open ? 0x5A78A9DA
                                : (hovered_launcher ? 0x36566F92
                                                    : 0x2437455B));
  gui_draw_rect_outline(launcher_btn_x, launcher_btn_y, launcher_btn_w,
                        launcher_btn_h,
                        menu_open ? 0xB8E8FFFF
                                  : (hovered_launcher ? 0x7EA7D8 : 0x506A87A8),
                        1);
  gui_draw_os_logo(launcher_btn_x + 10, launcher_btn_y + 9, 2, 0xFFFFFF,
                   0x89B4FA, 0x00000000);

  draw_dock_status_indicators(dock_y, dock_h);

  int center_y = dock_y + dock_h / 2;
  int curr_x = icon_start_x;
  int render_centers[MAX_DOCK_ITEMS];
  int running_counts[MAX_DOCK_ITEMS];
  for (int i = 0; i < dock_item_count; i++) {
    render_centers[i] = curr_x + icon_sizes[i] / 2;
    running_counts[i] = count_windows_for_app_kind(dock_items[i]->kind);
    curr_x += icon_sizes[i] + DOCK_PADDING;
  }

  for (int i = 0; i < dock_item_count; i++) {
    int size = icon_sizes[i];
    int cx = render_centers[i];
    int cy = center_y;
    int draw_x = cx - size / 2;
    int draw_y = cy - size / 2;
    int icon_r = size / 5;
    uint32_t bg_color = dock_items[i]->icon_color;

    gui_draw_rect(draw_x + icon_r, draw_y, size - 2 * icon_r, size, bg_color);
    gui_draw_rect(draw_x, draw_y + icon_r, size, size - 2 * icon_r, bg_color);
    for (int dy = -icon_r; dy <= icon_r; dy++) {
      for (int dx = -icon_r; dx <= icon_r; dx++) {
        if (dx * dx + dy * dy <= icon_r * icon_r) {
          draw_pixel(draw_x + icon_r + dx, draw_y + icon_r + dy, bg_color);
          draw_pixel(draw_x + size - icon_r - 1 + dx, draw_y + icon_r + dy,
                     bg_color);
          draw_pixel(draw_x + icon_r + dx, draw_y + size - icon_r - 1 + dy,
                     bg_color);
          draw_pixel(draw_x + size - icon_r - 1 + dx,
                     draw_y + size - icon_r - 1 + dy, bg_color);
        }
      }
    }

    for (int x = draw_x + icon_r; x < draw_x + size - icon_r; x++) {
      draw_pixel(x, draw_y + 2, bg_color + 0x202020);
      draw_pixel(x, draw_y + 3, bg_color + 0x202020);
    }

    draw_system_app_icon_kind(dock_items[i]->kind, draw_x + size / 8,
                              draw_y + size / 8, size * 3 / 4);

    if (running_counts[i] > 0) {
      int dots = running_counts[i] > 3 ? 3 : running_counts[i];
      int start_x = draw_x + size / 2 - ((dots * 6) - 2) / 2;
      int dot_y = draw_y + size + 6;

      for (int dot = 0; dot < dots; dot++) {
        draw_filled_circle(start_x + dot * 6, dot_y, 2,
                           i == hovered_idx ? 0xFFFFFF : 0xC7D2FE);
      }
    }
  }

  if (hovered_idx >= 0) {
    char label_buf[80];
    const char *label = dock_items[hovered_idx]->label;
    int idx_x = render_centers[hovered_idx];
    int label_len = 0;
    int running = running_counts[hovered_idx];
    int out = 0;

    while (label[out] && out < (int)sizeof(label_buf) - 1) {
      label_buf[out] = label[out];
      out++;
    }
    if (running > 0 && out < (int)sizeof(label_buf) - 10) {
      label_buf[out++] = ' ';
      label_buf[out++] = '(';
      if (running >= 10) {
        label_buf[out++] = (char)('0' + ((running / 10) % 10));
      }
      label_buf[out++] = (char)('0' + (running % 10));
      label_buf[out++] = ')';
    }
    label_buf[out] = '\0';
    label = label_buf;

    while (label[label_len])
      label_len++;
    int label_w = label_len * 8 + 16;
    int label_h = 24;
    int label_x = idx_x - label_w / 2;
    int label_y = dock_y - 32;

    draw_rounded_rect(label_x, label_y, label_w, label_h, 6, 0x303040);
    gui_draw_rect_outline(label_x, label_y, label_w, label_h, 0x505060, 1);
    gui_draw_string(label_x + 8, label_y + 4, label, 0xFFFFFF, 0x303040);

    {
      int tri_x = label_x + label_w / 2;
      int tri_y = label_y + label_h;
      for (int i = 0; i < 4; i++) {
        for (int j = -i; j <= i; j++) {
          draw_pixel(tri_x + j, tri_y + i, 0x303040);
        }
      }
    }
  }

  if (hovered_launcher) {
    int label_w = 88;
    int label_h = 24;
    int label_x = launcher_btn_x;
    int label_y = dock_y - 32;

    draw_rounded_rect(label_x, label_y, label_w, label_h, 6, 0x303040);
    gui_draw_rect_outline(label_x, label_y, label_w, label_h, 0x505060, 1);
    gui_draw_string(label_x + 8, label_y + 4, "Main Menu", 0xFFFFFF, 0x303040);
  }
}

/* Cached wallpaper for performance - gradient is expensive to recalculate */
static uint32_t *cached_wallpaper = NULL;
static int wallpaper_cached = 0;
static int wallpaper_cached_idx = -1; /* Which wallpaper is cached */

/* Draw wallpaper - supports both gradients and JPEG images */
static void draw_wallpaper(void) {
  int start_y = MENU_BAR_HEIGHT;
  /* Extend wallpaper all the way to bottom of screen (dock drawn on top) */
  int end_y = primary_display.height;
  int height = end_y - start_y;
  int width = primary_display.width;
  uint32_t *target =
      primary_display.backbuffer ? primary_display.backbuffer
                                 : primary_display.framebuffer;

  if (!target)
    return;

  /* Check if we need to reload (wallpaper changed) */
  if (wallpaper_cached_idx != current_wallpaper) {
    wallpaper_cached = 0;
    wallpaper_cached_idx = current_wallpaper;
    /* Load image if needed */
    wallpaper_ensure_loaded();
  }

  /* Check if this is an image wallpaper */
  if (wallpapers[current_wallpaper].type == 1 && wallpaper_image.pixels) {
    /* Draw scaled JPEG image - simple nearest neighbor for reliability */
    uint32_t img_w = wallpaper_image.width;
    uint32_t img_h = wallpaper_image.height;
    uint32_t *pixels = wallpaper_image.pixels;

    /* Calculate scale factors (fixed point 16.16) */
    uint32_t scale_x = (img_w << 16) / width;
    uint32_t scale_y = (img_h << 16) / height;

    for (int y = start_y; y < end_y; y++) {
      uint32_t *line = target + y * (primary_display.pitch / 4);
      uint32_t src_y = ((y - start_y) * scale_y) >> 16;
      if (src_y >= img_h)
        src_y = img_h - 1;
      uint32_t *src_row = pixels + src_y * img_w;

      for (int x = 0; x < width; x++) {
        uint32_t src_x = (x * scale_x) >> 16;
        if (src_x >= img_w)
          src_x = img_w - 1;
        line[x] = src_row[src_x];
      }
    }
    return;
  }

  /* Gradient wallpaper - use wallpaper_get_pixel */
  for (int y = start_y; y < end_y; y++) {
    uint32_t *line = target + y * (primary_display.pitch / 4);
    uint32_t color = wallpaper_get_pixel(0, y - start_y, height);

    for (int x = 0; x < width; x++) {
      line[x] = color;
    }
  }
}

static void draw_desktop(void) {
  /* Draw beautiful gradient wallpaper */
  draw_wallpaper();

  /* Draw desktop icons */
  if (desktop_session_active())
    desktop_draw_icons();

  /* Draw build info in the bottom-right corner above the dock. */
  {
#ifdef ARCH_X86_64
    const char *build_info = "OS 8 x86_64";
#elif defined(ARCH_X86)
    const char *build_info = "OS 8 x86";
#else
    const char *build_info = "OS 8 ARM64";
#endif
    int build_len = 0;
    int uuid_len = 0;
    while (build_info[build_len]) {
      build_len++;
    }
    while (BUILD_UUID[uuid_len]) {
      uuid_len++;
    }

    int text_w = build_len > uuid_len ? build_len * 8 : uuid_len * 8;
    int text_x = (int)primary_display.width - text_w - 24;
    int text_y =
        (int)primary_display.height - dock_reserved_height() - 40;

    if (text_x < 12)
      text_x = 12;

    gui_draw_string(text_x, text_y, build_info, 0xD9E4F4, 0x00000000);
    gui_draw_string(text_x, text_y + 16, BUILD_UUID, 0xAEB9CB, 0x00000000);
  }
}

static void draw_top_rounded_rect_alpha(int x, int y, int w, int h, int r,
                                        uint32_t color) {
  if (w <= 0 || h <= 0)
    return;
  if (r < 0)
    r = 0;
  if (r * 2 > w)
    r = w / 2;
  if (r > h)
    r = h;

  if (r == 0) {
    gui_fill_rect_alpha(x, y, w, h, color);
    return;
  }

  gui_fill_rect_alpha(x, y + r, w, h - r, color);
  gui_fill_rect_alpha(x + r, y, w - 2 * r, r, color);
  gui_fill_rect_alpha(x, y + r, r, h - r, color);
  gui_fill_rect_alpha(x + w - r, y + r, r, h - r, color);

  for (int cy = -r; cy <= r; cy++) {
    for (int cx = -r; cx <= r; cx++) {
      if (cx * cx + cy * cy <= r * r) {
        int py = y + r + cy;
        if (py <= y + r) {
          draw_pixel_alpha(x + r + cx, py, color);
          draw_pixel_alpha(x + w - r - 1 + cx, py, color);
        }
      }
    }
  }
}

/* ===================================================================== */
/* Compositor - Draw everything with dirty region optimization */
/* ===================================================================== */

/* Dirty region tracking for compositor */
#define MAX_DIRTY_REGIONS 32
typedef struct {
  int x, y, w, h;
  int valid;
} compositor_dirty_rect_t;

static compositor_dirty_rect_t g_dirty_regions[MAX_DIRTY_REGIONS];
static int g_dirty_count = 0;
static int g_full_redraw = 1; /* Start with full redraw */
static int g_frame_count = 0;
static int g_gpu_rendering_enabled = 0;
static int g_blur_effects_requested = 1;
static int g_blur_effects_enabled = 0;
static uint32_t *g_saved_backbuffer = NULL;
static char g_gpu_backend_name[32] = "software";

static int dock_handle_click(int x, int y) {
  int dock_y;
  int dock_h;
  int launcher_btn_x;
  int launcher_btn_y;
  int launcher_btn_w;
  int launcher_btn_h;

  if (!dock_is_visible())
    return 0;
  if (!gui_is_installer_mode()) {
    load_dock_config();
  }

  dock_y = primary_display.height - DOCK_HEIGHT;
  dock_h = DOCK_HEIGHT;
  main_menu_launcher_button_rect(&launcher_btn_x, &launcher_btn_y,
                                 &launcher_btn_w, &launcher_btn_h);

  if (y < dock_y || y >= dock_y + dock_h)
    return 0;

  if (x >= launcher_btn_x && x < launcher_btn_x + launcher_btn_w &&
      y >= launcher_btn_y && y < launcher_btn_y + launcher_btn_h) {
    menu_open = menu_open ? 0 : 1;
    if (!menu_open)
      main_menu_power_open = 0;
    return 1;
  }

  {
    int icon_x = launcher_btn_x + launcher_btn_w + 18;
    int icon_y_start = dock_y + (dock_h - DOCK_ICON_SIZE) / 2;

    for (int i = 0; i < dock_item_count; i++) {
      if (x >= icon_x && x < icon_x + DOCK_ICON_SIZE && y >= icon_y_start &&
          y < icon_y_start + DOCK_ICON_SIZE) {
        menu_open = 0;
        main_menu_power_open = 0;
        gui_focus_or_launch_app_by_id(dock_items[i]->id);
        return 1;
      }
      icon_x += DOCK_ICON_SIZE + DOCK_PADDING;
    }
  }

  return 1;
}

/* Mark a region as needing update */
void compositor_mark_dirty(int x, int y, int w, int h) {
  if (g_dirty_count < MAX_DIRTY_REGIONS) {
    g_dirty_regions[g_dirty_count].x = x;
    g_dirty_regions[g_dirty_count].y = y;
    g_dirty_regions[g_dirty_count].w = w;
    g_dirty_regions[g_dirty_count].h = h;
    g_dirty_regions[g_dirty_count].valid = 1;
    g_dirty_count++;
  } else {
    g_full_redraw = 1;
  }
}

void compositor_mark_full_redraw(void) {
  g_full_redraw = 1;
  g_dirty_count = 0;
}

static int gui_backend_supports_blur_effects(void) {
  if (str_cmp(g_gpu_backend_name, "virtio-gpu") == 0)
    return 1;
  if (str_cmp(g_gpu_backend_name, "intel-gfx") == 0)
    return 1;
  return 0;
}

static void gui_refresh_blur_effects_policy(void) {
  int next = g_blur_effects_requested && g_gpu_rendering_enabled &&
             gui_backend_supports_blur_effects();

  if (next == g_blur_effects_enabled)
    return;

  g_blur_effects_enabled = next;
  printk(KERN_INFO "GUI: blur effects %s (%s, requested=%d)\n",
         g_blur_effects_enabled ? "enabled" : "disabled", g_gpu_backend_name,
         g_blur_effects_requested);
  compositor_mark_full_redraw();
}

void gui_set_blur_effects_enabled(int enabled) {
  g_blur_effects_requested = enabled ? 1 : 0;
  gui_refresh_blur_effects_policy();
}

int gui_blur_effects_requested(void) { return g_blur_effects_requested; }

int gui_are_blur_effects_enabled(void) { return g_blur_effects_enabled; }

void gui_configure_gpu_rendering(int enabled) {
  if (enabled == g_gpu_rendering_enabled)
    return;
  if (enabled) {
    if (!primary_display.framebuffer)
      return;
    if (primary_display.backbuffer)
      g_saved_backbuffer = primary_display.backbuffer;
    g_gpu_rendering_enabled = 1;
    printk(KERN_INFO
           "GUI: %s acceleration enabled (safe compositor handoff)\n",
           g_gpu_backend_name);
  } else {
    if (!primary_display.backbuffer && g_saved_backbuffer)
      primary_display.backbuffer = g_saved_backbuffer;
    g_gpu_rendering_enabled = 0;
    printk(KERN_INFO "GUI: Software backbuffer rendering enabled\n");
  }
  gui_refresh_blur_effects_policy();
  compositor_mark_full_redraw();
}

int gui_is_gpu_rendering_enabled(void) { return g_gpu_rendering_enabled; }

void gui_refresh_hardware_acceleration_policy(void) {
  int enable = 0;
  const char *backend = "framebuffer";
  extern int intel_gfx_is_ready(void);
  extern int intel_gfx_has_framebuffer(void);
  extern bool virtio_gpu_is_available(void);
  extern bool virtio_gpu_has_3d(void);

  if (virtio_gpu_is_available() && virtio_gpu_has_3d()) {
    enable = 1;
    backend = "virtio-gpu";
  } else if (intel_gfx_is_ready() && intel_gfx_has_framebuffer()) {
    enable = 1;
    backend = "intel-gfx";
  } else if (pci_find_device(0x1234, 0x1111)) {
    backend = "bochs-vbe";
  }

  str_copy_safe(g_gpu_backend_name, backend, sizeof(g_gpu_backend_name));
  gui_configure_gpu_rendering(enable);
  gui_refresh_blur_effects_policy();
}

/* Optimized memcpy for scanlines */
static inline void fast_memcpy_line(uint32_t *dst, uint32_t *src, int width) {
  /* Use 64-bit copies for better performance */
  uint64_t *d64 = (uint64_t *)dst;
  uint64_t *s64 = (uint64_t *)src;
  int count = width / 2;

  for (int i = 0; i < count; i++) {
    d64[i] = s64[i];
  }

  /* Handle odd pixel */
  if (width & 1) {
    dst[width - 1] = src[width - 1];
  }
}

/* Copy a specific region from backbuffer to framebuffer */
static void blit_region(int x, int y, int w, int h) {
  if (!primary_display.backbuffer || !primary_display.framebuffer)
    return;

  /* Clip to screen bounds */
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > (int)primary_display.width)
    w = primary_display.width - x;
  if (y + h > (int)primary_display.height)
    h = primary_display.height - y;
  if (w <= 0 || h <= 0)
    return;

  int pitch_pixels = primary_display.pitch / 4;

  for (int row = y; row < y + h; row++) {
    uint32_t *src = primary_display.backbuffer + row * pitch_pixels + x;
    uint32_t *dst = primary_display.framebuffer + row * pitch_pixels + x;
    fast_memcpy_line(dst, src, w);
  }
}

/* Forward declaration for cursor */
void gui_draw_cursor(void);

void gui_compose(void) {
  g_frame_count++;
  if (!startup_setup_account_active())
    installer_process_background_install();

  /* Draw desktop and taskbar */
  draw_desktop();

  /* Update Snake game state (throttled) */
  static int snake_tick = 0;
  if (++snake_tick >= 10) { /* Update every 10 frames */
    snake_tick = 0;
    snake_move();
  }

  /* Draw windows from bottom to top (reverse order) */
  struct window *draw_order[MAX_WINDOWS];
  int count = 0;
  for (struct window *win = window_stack; win && count < MAX_WINDOWS;
       win = win->next) {
    draw_order[count++] = win;
  }

  /* Draw in reverse (bottom to top) */
  for (int i = count - 1; i >= 0; i--) {
    draw_window(draw_order[i]);
  }

  draw_menu_bar();
  if (dock_is_visible())
    draw_dock();

  draw_window_switcher_overlay();
  draw_secure_attention_overlay();

  if (window_switcher_frames > 0)
    window_switcher_frames--;

  /* Draw cursor to backbuffer BEFORE blit */
  gui_draw_cursor();

  /* Smart frame buffer update */
  if (primary_display.backbuffer && primary_display.framebuffer) {
    if (g_full_redraw || g_dirty_count == 0) {
      /* Full frame update - use ultra-fast unrolled copy */
      uint64_t *src = (uint64_t *)primary_display.backbuffer;
      uint64_t *dst = (uint64_t *)primary_display.framebuffer;
      size_t count64 = (primary_display.pitch * primary_display.height) / 8;
      size_t i = 0;

      /* Unrolled copy - 8 qwords (64 bytes / 16 pixels) per iteration */
      size_t fast_count = count64 & ~7UL;
      for (; i < fast_count; i += 8) {
        dst[i] = src[i];
        dst[i + 1] = src[i + 1];
        dst[i + 2] = src[i + 2];
        dst[i + 3] = src[i + 3];
        dst[i + 4] = src[i + 4];
        dst[i + 5] = src[i + 5];
        dst[i + 6] = src[i + 6];
        dst[i + 7] = src[i + 7];
      }
      for (; i < count64; i++) {
        dst[i] = src[i];
      }

      g_full_redraw = 0;
    } else {
      /* Partial update - only copy dirty regions */
      for (int d = 0; d < g_dirty_count; d++) {
        if (g_dirty_regions[d].valid) {
          blit_region(g_dirty_regions[d].x, g_dirty_regions[d].y,
                      g_dirty_regions[d].w, g_dirty_regions[d].h);
        }
      }
    }

    /* Memory barrier */
#ifdef ARCH_ARM64
    asm volatile("dsb sy" ::: "memory");
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
    asm volatile("mfence" ::: "memory");
#endif
  }

  /* Clear dirty regions for next frame */
  g_dirty_count = 0;

  /* Force full redraw periodically to catch any missed updates */
  if ((g_frame_count & 0x3F) == 0) { /* Every 64 frames */
    g_full_redraw = 1;
  }
}

/* ===================================================================== */
/* Mouse Cursor (Windows-style arrow - drawn to backbuffer, no flicker) */
/* ===================================================================== */

#define CURSOR_WIDTH 16
#define CURSOR_HEIGHT 24

/* 1=black outline, 2=white fill, 0=transparent */
static const uint8_t cursor_data[CURSOR_HEIGHT][CURSOR_WIDTH] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 1, 1, 2, 2, 2, 2, 1, 0, 0},
    {1, 2, 2, 2, 2, 1, 1, 0, 0, 1, 2, 2, 2, 1, 0, 0},
    {1, 2, 2, 2, 1, 0, 0, 0, 0, 1, 2, 2, 2, 1, 0, 0},
    {1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 1, 0},
    {1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 1, 0},
    {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

/* Draw cursor directly to the active render target. */
void gui_draw_cursor(void) {
  extern void mouse_get_position(int *x, int *y);
  int cx, cy;
  mouse_get_position(&cx, &cy);

  /* Update global mouse position for event handling */
  mouse_x = cx;
  mouse_y = cy;

  uint32_t *target =
      primary_display.backbuffer ? primary_display.backbuffer
                                 : primary_display.framebuffer;
  if (!target)
    return;

  int pitch = primary_display.pitch / 4;

  for (int row = 0; row < CURSOR_HEIGHT; row++) {
    for (int col = 0; col < CURSOR_WIDTH; col++) {
      uint8_t pixel = cursor_data[row][col];
      if (pixel == 0)
        continue; /* Transparent */

      int px = cx + col;
      int py = cy + row;
      if (px >= 0 && px < (int)primary_display.width && py >= 0 &&
          py < (int)primary_display.height) {
        uint32_t color = (pixel == 1) ? 0x00000000 : 0x00FFFFFF;
        target[py * pitch + px] = color;
      }
    }
  }
}

void gui_move_mouse(int dx, int dy) {
  mouse_x += dx;
  mouse_y += dy;

  if (mouse_x < 0)
    mouse_x = 0;
  if (mouse_y < 0)
    mouse_y = 0;
  if (mouse_x >= (int)primary_display.width)
    mouse_x = primary_display.width - 1;
  if (mouse_y >= (int)primary_display.height)
    mouse_y = primary_display.height - 1;
}

void gui_set_mouse_buttons(int buttons) { mouse_buttons = buttons; }

void gui_handle_key_event(int key) {
  if (startup_flow_active()) {
    startup_handle_key(key);
    return;
  }

  if (key == KEY_CTRL_ALT_DEL) {
    open_secure_attention();
    return;
  }

  if (secure_attention_open) {
    if (key == 27) {
      execute_secure_attention_action(SECURE_ACTION_CANCEL);
      return;
    }
    if (key == '\t' || key == KEY_RIGHT) {
      secure_attention_selection = (secure_attention_selection + 1) % 3;
      compositor_mark_full_redraw();
      return;
    }
    if (key == KEY_LEFT) {
      secure_attention_selection = (secure_attention_selection + 2) % 3;
      compositor_mark_full_redraw();
      return;
    }
    if (key == '\n' || key == '\r' || key == ' ') {
      execute_secure_attention_action(secure_attention_selection);
      return;
    }
    return;
  }

  if (key == KEY_WINDOW_SWITCHER) {
    activate_window_switcher();
    return;
  }

  /* Check if desktop is doing inline rename - takes priority */
  extern int desktop_is_renaming(void);
  extern int desktop_handle_key(int key);
  if (desktop_is_renaming()) {
    if (desktop_handle_key(key))
      return; /* Desktop consumed the key */
  }

  /* Route key to focused window */
  if (focused_window && focused_window->visible) {
    /* Check if it's a Terminal window */
    if (focused_window->title[0] == 'T' && focused_window->title[1] == 'e' &&
        focused_window->title[2] == 'r') {
      struct terminal *term = (struct terminal *)focused_window->userdata;
      if (!term) {
        term = term_get_active();
      }
      if (term) {
        term_set_active(term);
        term_handle_key(term, key);
      }
    }
    /* Check if it's a Notepad window */
    else if (focused_window->title[0] == 'N' &&
             focused_window->title[1] == 'o' &&
             focused_window->title[2] == 't') {
      notepad_key(key);
    }
    /* Check if it's a Rename window */
    else if (focused_window->title[0] == 'R' &&
             focused_window->title[1] == 'e' &&
             focused_window->title[2] == 'n') {
      rename_key(key);
    }
    /* Check if it's a Game window */
    else if (focused_window->title[0] == 'S' &&
             focused_window->title[1] == 'n' &&
             focused_window->title[2] == 'a') {
      snake_key(key);
    }
    /* Check if it's an Image Viewer window */
    else if (focused_window->title[0] == 'I' &&
             focused_window->title[1] == 'm' &&
             focused_window->title[2] == 'a') {
      /* ESC key (27) - exit fullscreen */
      if (key == 27 && g_imgview.fullscreen) {
        g_imgview.fullscreen = 0;
        g_imgview.zoom_pct = 0;
        g_imgview.offset_x = 0;
        g_imgview.offset_y = 0;
      }
      /* F key - toggle fullscreen */
      else if (key == 'f' || key == 'F') {
        g_imgview.fullscreen = !g_imgview.fullscreen;
        g_imgview.zoom_pct = 0;
        g_imgview.offset_x = 0;
        g_imgview.offset_y = 0;
      }
      /* R key - rotate right */
      else if (key == 'r' || key == 'R') {
        g_imgview.rotation = (g_imgview.rotation + 90) % 360;
      }
      /* L key - rotate left */
      else if (key == 'l' || key == 'L') {
        g_imgview.rotation = (g_imgview.rotation + 270) % 360;
      }
    }
    /* Call window's key handler if set */
    if (focused_window->on_key) {
      focused_window->on_key(focused_window, key);
    }
  }
}

/* ===================================================================== */
/* Event Handling with Window Dragging and Resizing */
/* ===================================================================== */

/* Dragging state */
static struct window *dragging_window = 0;
static int drag_offset_x = 0, drag_offset_y = 0;
static int prev_buttons = 0;

#define SNAP_EDGE_THRESHOLD 28
#define WINDOW_BOTTOM_CLEARANCE 12

static void snap_window_to_zone(struct window *win, int mouse_x_pos,
                                int mouse_y_pos) {
  int screen_w;
  int screen_h;
  int work_y;
  int work_h;

  if (!win)
    return;

  screen_w = (int)primary_display.width;
  screen_h = (int)primary_display.height;
  work_y = MENU_BAR_HEIGHT;
  work_h =
      screen_h - MENU_BAR_HEIGHT - dock_reserved_height() - WINDOW_BOTTOM_CLEARANCE;

  if (mouse_y_pos <= work_y + SNAP_EDGE_THRESHOLD) {
    if (win->state != WINDOW_MAXIMIZED) {
      win->saved_x = win->x;
      win->saved_y = win->y;
      win->saved_width = win->width;
      win->saved_height = win->height;
    }
    win->x = 0;
    win->y = work_y;
    win->width = screen_w;
    win->height = work_h;
    win->state = WINDOW_MAXIMIZED;
    return;
  }

  if (mouse_x_pos <= SNAP_EDGE_THRESHOLD &&
      mouse_y_pos <= work_y + work_h / 2) {
    win->x = 0;
    win->y = work_y;
    win->width = screen_w / 2;
    win->height = work_h / 2;
    win->state = WINDOW_NORMAL;
    return;
  }

  if (mouse_x_pos >= screen_w - SNAP_EDGE_THRESHOLD &&
      mouse_y_pos <= work_y + work_h / 2) {
    win->x = screen_w / 2;
    win->y = work_y;
    win->width = screen_w - win->x;
    win->height = work_h / 2;
    win->state = WINDOW_NORMAL;
    return;
  }

  if (mouse_x_pos <= SNAP_EDGE_THRESHOLD &&
      mouse_y_pos >= work_y + work_h - SNAP_EDGE_THRESHOLD) {
    win->x = 0;
    win->y = work_y + work_h / 2;
    win->width = screen_w / 2;
    win->height = work_h - work_h / 2;
    win->state = WINDOW_NORMAL;
    return;
  }

  if (mouse_x_pos >= screen_w - SNAP_EDGE_THRESHOLD &&
      mouse_y_pos >= work_y + work_h - SNAP_EDGE_THRESHOLD) {
    win->x = screen_w / 2;
    win->y = work_y + work_h / 2;
    win->width = screen_w - win->x;
    win->height = work_h - work_h / 2;
    win->state = WINDOW_NORMAL;
    return;
  }

  if (mouse_x_pos <= SNAP_EDGE_THRESHOLD) {
    win->x = 0;
    win->y = work_y;
    win->width = screen_w / 2;
    win->height = work_h;
    win->state = WINDOW_NORMAL;
    return;
  }

  if (mouse_x_pos >= screen_w - SNAP_EDGE_THRESHOLD) {
    win->x = screen_w / 2;
    win->y = work_y;
    win->width = screen_w - win->x;
    win->height = work_h;
    win->state = WINDOW_NORMAL;
  }
}

/* Resizing state */
static struct window *resizing_window = 0;
#define RESIZE_NONE 0
#define RESIZE_RIGHT 1
#define RESIZE_BOTTOM 2
#define RESIZE_BOTTOM_RIGHT 3
#define RESIZE_LEFT 4
#define RESIZE_TOP 5
#define RESIZE_TOP_LEFT 6
#define RESIZE_TOP_RIGHT 7
#define RESIZE_BOTTOM_LEFT 8
static int resize_edge = RESIZE_NONE;
static int resize_start_x = 0, resize_start_y = 0;
static int resize_start_w = 0, resize_start_h = 0;
static int resize_start_win_x = 0, resize_start_win_y = 0;

#define RESIZE_BORDER                                                          \
  12 /* Pixel width of resize grab area - larger for easier grabbing */
#define MIN_WINDOW_WIDTH 150
#define MIN_WINDOW_HEIGHT 100

void gui_handle_mouse_event(int x, int y, int buttons) {
  int old_buttons = prev_buttons;
  mouse_x = x;
  mouse_y = y;
  prev_buttons = buttons;

  int left_click = (buttons & 1) && !(old_buttons & 1); /* Just pressed */
  int left_held = (buttons & 1);
  int left_release = !(buttons & 1) && (old_buttons & 1);
  int right_click = (buttons & 2) && !(old_buttons & 2); /* Right button */

  if (startup_flow_active()) {
    if (desktop_is_context_menu_visible())
      desktop_hide_context_menu();

    if (left_click && startup_window) {
      int content_x = startup_window->x + BORDER_WIDTH;
      int content_y = startup_window->y + BORDER_WIDTH;
      int content_w = startup_window->width - BORDER_WIDTH * 2;
      int content_h = startup_window->height - BORDER_WIDTH * 2;

      gui_focus_window(startup_window);
      if (startup_setup_account_active()) {
        int button_x = 0, button_y = 0, button_w = 0, button_h = 0;
        int field_x = 0, field_y = 0, field_w = 0, field_h = 0;

        startup_get_setup_button_rect(content_x, content_y, content_w, content_h,
                                      &button_x, &button_y, &button_w,
                                      &button_h);
        if (startup_setup_account_form_active()) {
          startup_get_setup_field_rect(content_x, content_y, content_w,
                                       content_h, 0, &field_x, &field_y,
                                       &field_w, &field_h);
          if (x >= field_x && x < field_x + field_w && y >= field_y &&
              y < field_y + field_h) {
            startup_active_field = 0;
            return;
          }
          startup_get_setup_field_rect(content_x, content_y, content_w,
                                       content_h, 1, &field_x, &field_y,
                                       &field_w, &field_h);
          if (x >= field_x && x < field_x + field_w && y >= field_y &&
              y < field_y + field_h) {
            startup_active_field = 1;
            return;
          }
        }
        if (x >= button_x && x < button_x + button_w && y >= button_y &&
            y < button_y + button_h) {
          submit_startup_flow();
          return;
        }
        return;
      }
      if (x >= content_x + 20 && x < content_x + content_w - 20 &&
          y >= content_y + 94 && y < content_y + 128) {
        startup_active_field = 0;
        return;
      }
      if (x >= content_x + 20 && x < content_x + content_w - 20 &&
          y >= content_y + 162 && y < content_y + 196) {
        startup_active_field = 1;
        return;
      }
      if (x >= content_x + 20 && x < content_x + 190 && y >= content_y + 214 &&
          y < content_y + 248) {
        submit_startup_flow();
        return;
      }
    }
    return;
  }

  /* Handle context menu hover - ALWAYS call when menu visible */
  int menu_vis = desktop_is_context_menu_visible();
  if (menu_vis) {
    printk(KERN_INFO "MOUSE: Menu visible, calling hover at %d,%d\n", x, y);
    desktop_context_menu_hover(x, y);
    /* Force compositor to update */
    extern void compositor_mark_full_redraw(void);
    compositor_mark_full_redraw();
  }

  /* Track for double-click detection */
  static int last_click_x = 0, last_click_y = 0;
  static int click_count = 0;

  if (secure_attention_open) {
    if (left_click) {
      int hit = secure_attention_button_hit(x, y);
      if (hit >= 0) {
        secure_attention_selection = hit;
        execute_secure_attention_action(hit);
      } else {
        execute_secure_attention_action(SECURE_ACTION_CANCEL);
      }
    }
    return;
  }

  /* Handle window dragging */
  if (dragging_window && left_held) {
    if (dragging_window->state == WINDOW_MAXIMIZED) {
      dragging_window->x = dragging_window->saved_x;
      dragging_window->y = dragging_window->saved_y;
      dragging_window->width = dragging_window->saved_width;
      dragging_window->height = dragging_window->saved_height;
      dragging_window->state = WINDOW_NORMAL;
      drag_offset_x = dragging_window->width / 2;
      if (drag_offset_x < 40)
        drag_offset_x = 40;
    }

    /* Move window with mouse */
    dragging_window->x = x - drag_offset_x;
    dragging_window->y = y - drag_offset_y;

    /* Clamp to screen */
    if (dragging_window->y < MENU_BAR_HEIGHT)
      dragging_window->y = MENU_BAR_HEIGHT;
    if (dragging_window->y >
        (int)primary_display.height - dock_reserved_height() -
            TITLEBAR_HEIGHT - WINDOW_BOTTOM_CLEARANCE)
      dragging_window->y =
          primary_display.height - dock_reserved_height() - TITLEBAR_HEIGHT -
          WINDOW_BOTTOM_CLEARANCE;
    if (dragging_window->x < 0)
      dragging_window->x = 0;
    if (dragging_window->x > (int)primary_display.width - 100)
      dragging_window->x = primary_display.width - 100;
  }

  /* Handle window resizing */
  if (resizing_window && left_held) {
    int dx = x - resize_start_x;
    int dy = y - resize_start_y;
    int new_w = resize_start_w;
    int new_h = resize_start_h;
    int new_x = resize_start_win_x;
    int new_y = resize_start_win_y;

    /* Calculate new dimensions based on which edge is being dragged */
    if (resize_edge == RESIZE_RIGHT || resize_edge == RESIZE_BOTTOM_RIGHT ||
        resize_edge == RESIZE_TOP_RIGHT) {
      new_w = resize_start_w + dx;
    }
    if (resize_edge == RESIZE_LEFT || resize_edge == RESIZE_BOTTOM_LEFT ||
        resize_edge == RESIZE_TOP_LEFT) {
      new_w = resize_start_w - dx;
      new_x = resize_start_win_x + dx;
    }
    if (resize_edge == RESIZE_BOTTOM || resize_edge == RESIZE_BOTTOM_RIGHT ||
        resize_edge == RESIZE_BOTTOM_LEFT) {
      new_h = resize_start_h + dy;
    }
    if (resize_edge == RESIZE_TOP || resize_edge == RESIZE_TOP_LEFT ||
        resize_edge == RESIZE_TOP_RIGHT) {
      new_h = resize_start_h - dy;
      new_y = resize_start_win_y + dy;
    }

    /* Enforce minimum size */
    if (new_w < MIN_WINDOW_WIDTH) {
      if (resize_edge == RESIZE_LEFT || resize_edge == RESIZE_BOTTOM_LEFT ||
          resize_edge == RESIZE_TOP_LEFT) {
        new_x = resize_start_win_x + resize_start_w - MIN_WINDOW_WIDTH;
      }
      new_w = MIN_WINDOW_WIDTH;
    }
    if (new_h < MIN_WINDOW_HEIGHT) {
      if (resize_edge == RESIZE_TOP || resize_edge == RESIZE_TOP_LEFT ||
          resize_edge == RESIZE_TOP_RIGHT) {
        new_y = resize_start_win_y + resize_start_h - MIN_WINDOW_HEIGHT;
      }
      new_h = MIN_WINDOW_HEIGHT;
    }

    /* Clamp to screen */
    if (new_y < MENU_BAR_HEIGHT)
      new_y = MENU_BAR_HEIGHT;
    if (new_x < 0)
      new_x = 0;

    resizing_window->x = new_x;
    resizing_window->y = new_y;
    resizing_window->width = new_w;
    resizing_window->height = new_h;
  }

  if (left_release) {
    if (dragging_window) {
      snap_window_to_zone(dragging_window, x, y);
    }
    dragging_window = 0;
    resizing_window = 0;
    resize_edge = RESIZE_NONE;
  }

  /* Handle desktop right-click (context menu) - check BEFORE left_click gate */
  if (desktop_session_active() && right_click) {
    printk(KERN_INFO "RIGHT-CLICK at %d,%d buttons=%d\n", x, y, buttons);
    /* Check if right-click is on desktop area (not on window, menu bar, or
     * dock) */
    int on_window = 0;
    for (struct window *win = window_stack; win; win = win->next) {
      if (!win->visible)
        continue;
      if (x >= win->x && x < win->x + win->width && y >= win->y &&
          y < win->y + win->height) {
        on_window = 1;
        break;
      }
    }

    if (!on_window && y > MENU_BAR_HEIGHT &&
        y < (int)primary_display.height - dock_reserved_height()) {
      printk(KERN_INFO
             "RIGHT-CLICK on desktop, calling desktop_handle_click\n");
      /* Right-click on desktop - handle in desktop manager */
      desktop_handle_click(x, y, 2, 0); /* button 2 = right */
      return;
    }
  }

  /* Handle desktop left-click for icon selection - check BEFORE window checks
   */
  if (desktop_session_active() && left_click) {
    /* Check context menu first */
    if (desktop_is_context_menu_visible()) {
      if (desktop_context_menu_click(x, y)) {
        return;
      }
    }

    /* Check menu bar dropdown BEFORE desktop icons (dropdown overlaps desktop
     * area) */
    if (menu_open == 1 && main_menu_contains_point(x, y)) {
      if (main_menu_activate(main_menu_item_at(x, y))) {
        return;
      }
      menu_open = 0;
      main_menu_power_open = 0;
      return;
    }

    if (y < MENU_BAR_HEIGHT) {
      menu_open = 0;
      main_menu_power_open = 0;
      return;
    }

    if (dock_handle_click(x, y)) {
      return;
    }

    /* Check if click is on desktop area (not on window) */
    int on_window = 0;
    for (struct window *win = window_stack; win; win = win->next) {
      if (!win->visible)
        continue;
      if (x >= win->x && x < win->x + win->width && y >= win->y &&
          y < win->y + win->height) {
        on_window = 1;
        break;
      }
    }

    if (!on_window && y > MENU_BAR_HEIGHT &&
        y < (int)primary_display.height - dock_reserved_height()) {
      /* Track double-click */
      int dx = x - last_click_x;
      int dy = y - last_click_y;
      if (dx < 0)
        dx = -dx;
      if (dy < 0)
        dy = -dy;

      if (dx < 10 && dy < 10) {
        click_count++;
        if (click_count >= 2) {
          /* Double click - open item */
          desktop_handle_double_click(x, y);
          click_count = 0;
          return;
        }
      } else {
        click_count = 1;
      }
      last_click_x = x;
      last_click_y = y;

      /* Single click - select icon */
      int shift_held = 0;
      if (desktop_handle_click(x, y, 1, shift_held)) {
        return; /* Click was on desktop icon */
      }
    }
  }

  /* Check if clicking on a window */
  if (!left_click)
    return;

  /* Check menu bar and dropdown clicks */
  if (y < MENU_BAR_HEIGHT || (menu_open && main_menu_contains_point(x, y))) {

    printk("MENU DEBUG: x=%d y=%d menu_open=%d MBH=%d\\n", x, y, menu_open,
           MENU_BAR_HEIGHT);

    /* If dropdown is open, check dropdown item clicks */
    if (menu_open == 1 && main_menu_contains_point(x, y)) {
      if (main_menu_activate(main_menu_item_at(x, y))) {
        return;
      }
      menu_open = 0;
      main_menu_power_open = 0;
      return;
    }

    /* Menu bar clicks */
    if (y < MENU_BAR_HEIGHT) {
      menu_open = 0;
      main_menu_power_open = 0;
      return;
    }
  }

  /* Close menu if clicking elsewhere */
  if (menu_open) {
    menu_open = 0;
    main_menu_power_open = 0;
  }

  for (struct window *win = window_stack; win; win = win->next) {
    if (!win->visible)
      continue;

    if (x >= win->x && x < win->x + win->width && y >= win->y &&
        y < win->y + win->height) {

      gui_focus_window(win);

      /* Check for resize edges FIRST (on any visible window) */
      {
        int at_left = (x >= win->x && x < win->x + RESIZE_BORDER);
        int at_right = (x >= win->x + win->width - RESIZE_BORDER &&
                        x < win->x + win->width);
        int at_top = (y >= win->y && y < win->y + RESIZE_BORDER);
        int at_bottom = (y >= win->y + win->height - RESIZE_BORDER &&
                         y < win->y + win->height);

        /* Determine which edge/corner */
        int edge = RESIZE_NONE;
        if (at_bottom && at_right)
          edge = RESIZE_BOTTOM_RIGHT;
        else if (at_bottom && at_left)
          edge = RESIZE_BOTTOM_LEFT;
        else if (at_top && at_right)
          edge = RESIZE_TOP_RIGHT;
        else if (at_top && at_left)
          edge = RESIZE_TOP_LEFT;
        else if (at_right)
          edge = RESIZE_RIGHT;
        else if (at_bottom)
          edge = RESIZE_BOTTOM;
        else if (at_left)
          edge = RESIZE_LEFT;
        else if (at_top && !win->has_titlebar)
          edge = RESIZE_TOP;

        if (edge != RESIZE_NONE) {
          resizing_window = win;
          resize_edge = edge;
          resize_start_x = x;
          resize_start_y = y;
          resize_start_w = win->width;
          resize_start_h = win->height;
          resize_start_win_x = win->x;
          resize_start_win_y = win->y;
          return;
        }
      }

      /* Check for traffic light buttons (on LEFT side now) */
      if (win->has_titlebar) {
        int btn_cy = win->y + BORDER_WIDTH + TITLEBAR_HEIGHT / 2;
        int btn_r = 8; /* Click radius slightly larger than visual */

        /* Close button (first) */
        int close_cx = win->x + BORDER_WIDTH + 18;
        if ((x - close_cx) * (x - close_cx) + (y - btn_cy) * (y - btn_cy) <=
            btn_r * btn_r) {
          if (!window_close_disabled(win))
            gui_destroy_window(win);
          return;
        }

        /* Minimize button (second) */
        int min_cx = close_cx + 20;
        if ((x - min_cx) * (x - min_cx) + (y - btn_cy) * (y - btn_cy) <=
            btn_r * btn_r) {
          if (!window_minimize_disabled(win)) {
            win->visible = false;
            win->state = WINDOW_MINIMIZED;
          }
          return;
        }

        /* Zoom/Maximize button (third) */
        int zoom_cx = min_cx + 20;
        if ((x - zoom_cx) * (x - zoom_cx) + (y - btn_cy) * (y - btn_cy) <=
            btn_r * btn_r) {
          if (win->state == WINDOW_MAXIMIZED) {
            /* Restore */
            win->x = win->saved_x;
            win->y = win->saved_y;
            win->width = win->saved_width;
            win->height = win->saved_height;
            win->state = WINDOW_NORMAL;
          } else {
            /* Maximize */
            win->saved_x = win->x;
            win->saved_y = win->y;
            win->saved_width = win->width;
            win->saved_height = win->height;
            win->x = 0;
            win->y = MENU_BAR_HEIGHT;
            win->width = primary_display.width;
            win->height =
                primary_display.height - MENU_BAR_HEIGHT -
                dock_reserved_height() - WINDOW_BOTTOM_CLEARANCE;
            win->state = WINDOW_MAXIMIZED;
          }
          return;
        }

        /* Start dragging if clicking on title bar */
        if (y >= win->y + BORDER_WIDTH &&
            y < win->y + BORDER_WIDTH + TITLEBAR_HEIGHT &&
            x >= win->x + BORDER_WIDTH + 70) { /* After traffic lights */
          dragging_window = win;
          drag_offset_x = x - win->x;
          drag_offset_y = y - win->y;
          return;
        }
      }

      /* Handle clicks inside Calculator window */
      if (win->title[0] == 'C' && win->title[1] == 'a' &&
          win->title[2] == 'l') {
        /* Calculate content area */
        int content_x = win->x + BORDER_WIDTH;
        int content_y = win->y + BORDER_WIDTH + TITLEBAR_HEIGHT;
        int content_w = win->width - BORDER_WIDTH * 2;
        int content_h = win->height - BORDER_WIDTH * 2 - TITLEBAR_HEIGHT;

        /* Button layout - 5x4 grid matching render */
        static const char btns[5][4] = {
            {'C', 'N', '%', '/'}, /* N = +/- (negate) */
            {'7', '8', '9', '*'},
            {'4', '5', '6', '-'},
            {'1', '2', '3', '+'},
            {'0', '0', '.', '='}};

        int disp_h = 70;
        int grid_x = content_x + 12;
        int grid_y = content_y + disp_h + 20;
        int grid_w = content_w - 24;
        int grid_h = content_h - disp_h - 32;
        int bw = (grid_w - 12) / 4;
        int bh = (grid_h - 16) / 5;
        int gap = 4;

        /* Check if click is in button area */
        if (x >= grid_x && y >= grid_y) {
          int col = (x - grid_x) / (bw + gap);
          int row = (y - grid_y) / (bh + gap);
          if (row >= 0 && row < 5 && col >= 0 && col < 4) {
            char btn = btns[row][col];
            /* Handle special buttons */
            if (btn == 'N') {
              /* +/- button - negate display */
              calc_display = -calc_display;
            } else if (btn == '%') {
              /* Percent - divide by 100 */
              calc_display = calc_display / 100;
            } else if (btn == '.') {
              /* Decimal point - ignore for integers */
            } else {
              calc_button_click(btn);
            }
          }
        }
        break;
      }

      /* Handle clicks inside Background Settings window */
      if (win->title[0] == 'B' && win->title[1] == 'a' &&
          win->title[2] == 'c') {
        int content_x = win->x + BORDER_WIDTH;
        int content_y = win->y + BORDER_WIDTH + TITLEBAR_HEIGHT;

        /* Wallpaper grid layout (matching render) */
        int grid_x = content_x + 15;
        int grid_y = content_y + 55;
        int thumb_w = 65;
        int thumb_h = 45;
        int gap = 10;

        for (int i = 0; i < NUM_WALLPAPERS; i++) {
          int col = i % 5;
          int row = i / 5;
          int tx = grid_x + col * (thumb_w + gap);
          int ty = grid_y + row * (thumb_h + gap + 20);

          if (x >= tx && x < tx + thumb_w && y >= ty && y < ty + thumb_h) {
            current_wallpaper = i;
            wallpaper_ensure_loaded(); /* Load JPEG if needed */
            break;
          }
        }
        break;
      }

      /* Handle clicks inside Settings window */
      if (win->title[0] == 'S' && win->title[1] == 'e' &&
          win->title[2] == 't') {
        int content_x = win->x + BORDER_WIDTH;
        int content_y = win->y + BORDER_WIDTH + TITLEBAR_HEIGHT;
        int sidebar_w = 118;
        int panel_x = content_x + sidebar_w + 14;
        int panel_y = content_y + 14;

        for (int i = 0; i < 3; i++) {
          int tab_y = content_y + 76 + i * 38;
          if (x >= content_x + 18 && x < content_x + sidebar_w - 10 &&
              y >= tab_y && y < tab_y + 28) {
            settings_active_tab = i;
            if (i == 0)
              str_copy_safe(settings_status, "Tune your desktop experience.",
                            sizeof(settings_status));
            else if (i == 1)
              str_copy_safe(settings_status, "Adjust wallpapers, blur, and graphics.",
                            sizeof(settings_status));
            else
              str_copy_safe(settings_status, "Launch tools and recover defaults.",
                            sizeof(settings_status));
            break;
          }
        }

        if (settings_active_tab == 0) {
          int row_y = panel_y + 72 + 84 + 88;
          if (x >= panel_x && x < panel_x + 108 && y >= row_y && y < row_y + 30) {
            gui_create_window("Background Settings", win->x + 18, win->y + 18, 400,
                              350);
            str_copy_safe(settings_status, "Opened background settings.",
                          sizeof(settings_status));
            break;
          }
          if (x >= panel_x + 118 && x < panel_x + 216 && y >= row_y &&
              y < row_y + 30) {
            gui_create_window("App Store", win->x + 28, win->y + 28, 540, 420);
            str_copy_safe(settings_status, "Opened the app store.",
                          sizeof(settings_status));
            break;
          }
          if (x >= panel_x + 226 && x < panel_x + 318 && y >= row_y &&
              y < row_y + 30) {
            gui_create_window("Device Manager", win->x + 40, win->y + 40, 460,
                              360);
            str_copy_safe(settings_status, "Opened device manager.",
                          sizeof(settings_status));
            break;
          }
          if (x >= panel_x + 328 && x < panel_x + 412 && y >= row_y &&
              y < row_y + 30) {
            gui_create_window("About", 280, 180, 420, 260);
            str_copy_safe(settings_status, "Opened about window.",
                          sizeof(settings_status));
            break;
          }
        } else if (settings_active_tab == 1) {
          int resolution_card_y = panel_y + 72 + 104 + 84;
          int button_y = resolution_card_y + 66;
          int picked_resolution = 0;

          settings_sync_resolution_picker();
          for (int i = 0; i < SETTINGS_RESOLUTION_OPTION_COUNT; i++) {
            int bx, by, bw, bh;
            settings_resolution_button_bounds(panel_x, panel_y, i, &bx, &by, &bw,
                                              &bh);
            if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
              settings_resolution_pending_idx = i;
              str_copy_safe(settings_status, "Resolution preset selected.",
                            sizeof(settings_status));
              picked_resolution = 1;
              break;
            }
          }
          if (picked_resolution)
            break;

          if (x >= panel_x + 8 && x < panel_x + 98 && y >= button_y &&
              y < button_y + 24) {
            gui_create_window("Background Settings", win->x + 18, win->y + 18, 400,
                              350);
            str_copy_safe(settings_status, "Pick a new wallpaper.",
                          sizeof(settings_status));
            break;
          }
          if (x >= panel_x + 106 && x < panel_x + 196 && y >= button_y &&
              y < button_y + 24) {
            gui_set_blur_effects_enabled(!gui_blur_effects_requested());
            str_copy_safe(settings_status,
                          gui_blur_effects_requested() ? "Blur requested."
                                                       : "Blur disabled.",
                          sizeof(settings_status));
            break;
          }
          if (x >= panel_x + 204 && x < panel_x + 294 && y >= button_y &&
              y < button_y + 24) {
            gui_configure_gpu_rendering(!gui_is_gpu_rendering_enabled());
            str_copy_safe(settings_status,
                          gui_is_gpu_rendering_enabled() ? "GPU rendering enabled."
                                                         : "Software renderer enabled.",
                          sizeof(settings_status));
            break;
          }
          if (x >= panel_x + 302 && x < panel_x + 392 && y >= button_y &&
              y < button_y + 24) {
            if (settings_resolution_pending_idx == settings_resolution_current_idx) {
              str_copy_safe(settings_status, "That resolution is already active.",
                            sizeof(settings_status));
            } else {
              const settings_resolution_option_t *opt =
                  &settings_resolution_options[settings_resolution_pending_idx];
              if (gui_apply_resolution(opt->width, opt->height) == 0) {
                settings_sync_resolution_picker();
                str_copy_safe(settings_status, "Resolution changed successfully.",
                              sizeof(settings_status));
              } else {
                str_copy_safe(settings_status,
                              "Live resolution switching is unavailable on this display backend.",
                              sizeof(settings_status));
              }
            }
            break;
          }
        } else {
          int row1_y = panel_y + 72 + 90;
          int row2_y = row1_y + 42;
          if (x >= panel_x && x < panel_x + 110 && y >= row1_y && y < row1_y + 30) {
            gui_create_window("Device Manager", win->x + 40, win->y + 40, 460,
                              360);
            str_copy_safe(settings_status, "Opened device manager.",
                          sizeof(settings_status));
            break;
          }
          if (x >= panel_x + 120 && x < panel_x + 230 && y >= row1_y &&
              y < row1_y + 30) {
            gui_create_file_manager_path(win->x + 26, win->y + 26, "/");
            str_copy_safe(settings_status, "Opened file manager.",
                          sizeof(settings_status));
            break;
          }
          if (x >= panel_x + 240 && x < panel_x + 350 && y >= row1_y &&
              y < row1_y + 30) {
            gui_create_window("App Store", win->x + 28, win->y + 28, 540, 420);
            str_copy_safe(settings_status, "Opened the app store.",
                          sizeof(settings_status));
            break;
          }
          if (x >= panel_x + 360 && x < panel_x + 470 && y >= row1_y &&
              y < row1_y + 30) {
            dock_item_count = 0;
            dock_add_all_system_apps();
            save_dock_config();
            str_copy_safe(settings_status, "Dock reset to system defaults.",
                          sizeof(settings_status));
            break;
          }
          if (x >= panel_x && x < panel_x + 110 && y >= row2_y &&
              y < row2_y + 30) {
            gui_create_window("About", 280, 180, 420, 260);
            str_copy_safe(settings_status, "Opened about window.",
                          sizeof(settings_status));
            break;
          }
        }
      }

      /* Handle clicks inside App Store window */
      if (win->title[0] == 'A' && win->title[1] == 'p' &&
          win->title[2] == 'p' && win->title[3] == ' ') {
        int content_x = win->x + BORDER_WIDTH;
        int content_y = win->y + BORDER_WIDTH + TITLEBAR_HEIGHT;
        int content_w = win->width - BORDER_WIDTH * 2;
        int y_row = content_y + 12 + 18 + 24;

        for (int i = 0; i < app_catalog_count; i++) {
          const dock_app_def_t *app = &app_catalog[i];
          if (!app->visible_in_store)
            continue;

          int installed = app_is_installed(app);
          int button_w = installed ? 72 : 88;
          int button_x = content_x + content_w - button_w - 18;
          int button_y = y_row + 13;

          if (x >= button_x && x < button_x + button_w && y >= button_y &&
              y < button_y + 28) {
            if (!installed) {
              install_app(app, 1);
            }
            gui_launch_app_by_id(app->id);
            return;
          }

          y_row += APP_STORE_CARD_HEIGHT + 8;
        }
      }

      if (win->title[0] == 'I' && win->title[1] == 'n' &&
          win->title[2] == 's' && win->title[3] == 't') {
        int content_x = win->x + BORDER_WIDTH;
        int content_y = win->y + BORDER_WIDTH + TITLEBAR_HEIGHT;
        int content_w = win->width - BORDER_WIDTH * 2;
        int content_h = win->height - BORDER_WIDTH * 2 - TITLEBAR_HEIGHT;
        int card_x = content_x + 24;
        int card_y = content_y + 22;
        int button_x = content_x + 24;
        int manage_x = button_x + 180 + 12;
        int button_y = content_y + content_h - 64;
        int button_w = 180;
        int button_h = 34;
        int disk_y = card_y + 102;

        installer_refresh_disk_inventory();

        if (installer_show_restart_screen) {
          if (x >= button_x && x < button_x + button_w && y >= button_y &&
              y < button_y + button_h) {
            installer_reboot_deadline_ms = 0;
            {
              extern void arch_reboot(void);
              arch_reboot();
            }
            return;
          }
          return;
        }

        for (int i = 0; i < installer_disk_count && i < 3; i++) {
          int row_y = disk_y + 18 + i * 26;
          if (x >= card_x + 18 && x < card_x + content_w - 60 && y >= row_y &&
              y < row_y + 22) {
            installer_selected_disk = i;
            installer_set_status("Installer target disk updated.");
            return;
          }
        }

        if (x >= button_x && x < button_x + button_w && y >= button_y &&
            y < button_y + button_h) {
          if (!installer_has_run && !installer_active) {
            installer_start_background_install();
          }
          return;
        }

        if (x >= manage_x && x < manage_x + 150 && y >= button_y &&
            y < button_y + button_h) {
          open_partition_manager_window(win->x + 36, win->y + 30);
          return;
        }
      }

      if (win->title[0] == 'P' && win->title[1] == 'a' &&
          win->title[2] == 'r' && win->title[3] == 't') {
        int content_x = win->x + BORDER_WIDTH;
        int content_y = win->y + BORDER_WIDTH + TITLEBAR_HEIGHT;
        extern int storage_create_partition(int disk_index,
                                            storage_partition_kind_t kind,
                                            uint32_t size_mib);
        extern int storage_update_partition(int disk_index, int partition_index,
                                            storage_partition_kind_t kind,
                                            uint32_t size_mib);
        extern int storage_delete_partition(int disk_index, int partition_index);
        extern int storage_ensure_install_partitions(int disk_index);

        installer_refresh_disk_inventory();
        partition_manager_refresh_partitions();
        for (int i = 0; i < installer_disk_count && i < 6; i++) {
          int row_y = content_y + 96 + i * 28;
          if (x >= content_x + 24 && x < content_x + win->width - 24 &&
              y >= row_y && y < row_y + 24) {
            installer_selected_disk = i;
            str_copy_safe(partition_manager_status, "Selected disk updated.",
                          sizeof(partition_manager_status));
            return;
          }
        }

        for (int i = 0; i < partition_manager_partition_count && i < 4; i++) {
          int row_y = content_y + 196 + i * 22;
          if (x >= content_x + 24 && x < content_x + win->width - 24 &&
              y >= row_y && y < row_y + 18) {
            partition_manager_selected_partition = i;
            str_copy_safe(partition_manager_status, "Selected partition updated.",
                          sizeof(partition_manager_status));
            return;
          }
        }

        if (x >= content_x + 24 && x < content_x + 144 && y >= content_y + 298 &&
            y < content_y + 328) {
          installer_write_target_config();
          str_copy_safe(partition_manager_status,
                        "Real disk set as installer target.",
                        sizeof(partition_manager_status));
          installer_set_status("Installer target disk updated.");
          return;
        }

        if (x >= content_x + 152 && x < content_x + 230 &&
            y >= content_y + 298 && y < content_y + 328) {
          if (storage_create_partition(installer_selected_disk,
                                       STORAGE_PARTITION_EFI, 256) == 0) {
            partition_manager_refresh_partitions();
            str_copy_safe(partition_manager_status, "EFI partition created.",
                          sizeof(partition_manager_status));
          } else {
            str_copy_safe(partition_manager_status, "EFI partition creation failed.",
                          sizeof(partition_manager_status));
          }
          return;
        }

        if (x >= content_x + 238 && x < content_x + 332 &&
            y >= content_y + 298 && y < content_y + 328) {
          if (storage_create_partition(installer_selected_disk,
                                       STORAGE_PARTITION_SYSTEM, 8192) == 0) {
            partition_manager_refresh_partitions();
            str_copy_safe(partition_manager_status, "System partition created.",
                          sizeof(partition_manager_status));
          } else {
            str_copy_safe(partition_manager_status,
                          "System partition creation failed.",
                          sizeof(partition_manager_status));
          }
          return;
        }

        if (x >= content_x + 340 && x < content_x + 424 &&
            y >= content_y + 298 && y < content_y + 328) {
          if (storage_create_partition(installer_selected_disk,
                                       STORAGE_PARTITION_DATA, 4096) == 0) {
            partition_manager_refresh_partitions();
            str_copy_safe(partition_manager_status, "Data partition created.",
                          sizeof(partition_manager_status));
          } else {
            str_copy_safe(partition_manager_status, "Data partition creation failed.",
                          sizeof(partition_manager_status));
          }
          return;
        }

        if (x >= content_x + 432 && x < content_x + 528 &&
            y >= content_y + 298 && y < content_y + 328) {
          if (partition_manager_partition_count > 0 &&
              storage_delete_partition(installer_selected_disk,
                                       partition_manager_selected_partition) ==
                  0) {
            partition_manager_refresh_partitions();
            str_copy_safe(partition_manager_status, "Partition deleted.",
                          sizeof(partition_manager_status));
          } else {
            str_copy_safe(partition_manager_status, "Partition delete failed.",
                          sizeof(partition_manager_status));
          }
          return;
        }

        if (x >= content_x + 24 && x < content_x + 124 &&
            y >= content_y + 332 && y < content_y + 362) {
          if (partition_manager_partition_count > 0 &&
              storage_update_partition(installer_selected_disk,
                                       partition_manager_selected_partition,
                                       STORAGE_PARTITION_DATA, 6144) == 0) {
            partition_manager_refresh_partitions();
            str_copy_safe(partition_manager_status,
                          "Selected partition edited to Data 6144 MiB.",
                          sizeof(partition_manager_status));
          } else {
            str_copy_safe(partition_manager_status, "Partition edit failed.",
                          sizeof(partition_manager_status));
          }
          return;
        }

        if (x >= content_x + 132 && x < content_x + 242 &&
            y >= content_y + 332 && y < content_y + 362) {
          if (storage_ensure_install_partitions(installer_selected_disk) >= 0) {
            partition_manager_refresh_partitions();
            str_copy_safe(partition_manager_status,
                          "Auto layout ensured EFI and System partitions.",
                          sizeof(partition_manager_status));
          } else {
            str_copy_safe(partition_manager_status, "Auto layout failed.",
                          sizeof(partition_manager_status));
          }
          return;
        }

        if (x >= content_x + 250 && x < content_x + 340 &&
            y >= content_y + 332 && y < content_y + 362) {
          installer_refresh_disk_inventory();
          partition_manager_refresh_partitions();
          str_copy_safe(partition_manager_status, "Disk list refreshed.",
                        sizeof(partition_manager_status));
          return;
        }

        if (x >= content_x + 348 && x < content_x + 458 &&
            y >= content_y + 332 && y < content_y + 362) {
          gui_create_file_manager_path(win->x + 24, win->y + 24, "/");
          str_copy_safe(partition_manager_status,
                        "Opened File Manager for disk-related files.",
                        sizeof(partition_manager_status));
          return;
        }
      }

      if (win->on_mouse) {
        win->on_mouse(win, x - win->x, y - win->y, buttons);
      }
      break;
    }
  }

}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

int gui_init(uint32_t *framebuffer, uint32_t width, uint32_t height,
             uint32_t pitch) {
  printk(KERN_INFO "GUI: Initializing windowing system\n");
  extern int boot_should_show_splash(void);

  if (gui_is_installer_mode()) {
    installer_has_run = 0;
    installer_show_restart_screen = 0;
    installer_set_status("Ready to install the system image.");
  }

  primary_display.framebuffer = framebuffer;
  primary_display.width = width;
  primary_display.height = height;
  primary_display.pitch = pitch;
  primary_display.bpp = 32;

  /* ============================================= */
  /* LOADING SCREEN - Show during initialization  */
  /* ============================================= */

  if (boot_should_show_splash()) {
    /* Fill with dark gradient background */
    for (int y = 0; y < (int)height; y++) {
      int progress = (y * 256) / height;
      uint8_t r = 15 + (progress * 10) / 256;
      uint8_t g = 15 + (progress * 5) / 256;
      uint8_t b = 30 + (progress * 25) / 256;
      uint32_t color = (r << 16) | (g << 8) | b;
      for (int x = 0; x < (int)width; x++) {
        framebuffer[y * (pitch / 4) + x] = color;
      }
    }

    /* Draw the actual OS logo and centered brand text */
    {
      const char *logo = "OS8";
      int logo_len = 0;
      int logo_scale = 5;
      int logo_size = 14 * logo_scale;
      int logo_x = ((int)width - logo_size) / 2;
      int logo_y = (int)height / 2 - 110;
      int text_y;
      int text_x;

      while (logo[logo_len])
        logo_len++;

      gui_draw_os_logo(logo_x, logo_y, logo_scale, 0xFFFFFF, 0x89B4FA,
                       0x00000000);

      text_y = logo_y + logo_size + 20;
      text_x = ((int)width - logo_len * 8) / 2;
      gui_draw_string(text_x, text_y, logo, 0xFFFFFF, 0x00000000);
    }

    /* Draw version text */
    {
      const char *version = "v1.0 - Modern Desktop Experience";
      int ver_x = (width - 33 * 8) / 2;
      int ver_y = height / 2 + 4;
      int bar_w = 300;
      int bar_h = 8;
      int bar_x = (width - bar_w) / 2;
      int bar_y = height / 2 + 40;
      const char *loading_msgs[] = {"Initializing hardware...",
                                    "Loading desktop environment...",
                                    "Starting services...",
                                    "Welcome to OS8!"};

      gui_draw_string(ver_x, ver_y, version, 0x9CA3AF, 0x00000000);
      gui_draw_rect(bar_x, bar_y, bar_w, bar_h, 0x27272A);
      gui_draw_rect(bar_x, bar_y, bar_w, 1, 0x3F3F46);

      for (int stage = 0; stage < 4; stage++) {
        int fill = (bar_w * (stage + 1)) / 4;
        int msg_x = (width - 30 * 8) / 2;
        int msg_y = bar_y + 20;

        gui_draw_rect(bar_x + 1, bar_y + 1, fill - 2, bar_h - 2, 0x6366F1);
        gui_draw_rect(msg_x - 10, msg_y - 2, 260, 20, 0x000000);
        gui_draw_string(msg_x, msg_y, loading_msgs[stage], 0xE4E4E7,
                        0x000000);
      }
    }
  }

  /* ============================================= */
  /* END LOADING SCREEN                           */
  /* ============================================= */

  /* Register input callbacks */
  extern void input_set_gui_key_callback(void (*callback)(int key));
  extern void gui_handle_key_event(int key);
  input_set_gui_key_callback(gui_handle_key_event);

  /* Allocate backbuffer for double-buffering */
  primary_display.backbuffer = kmalloc(pitch * height);
  if (!primary_display.backbuffer) {
    printk(KERN_WARNING
           "GUI: Backbuffer allocation failed, rendering directly to framebuffer\n");
  } else {
    g_saved_backbuffer = primary_display.backbuffer;
  }

  /* Clear windows */
  for (int i = 0; i < MAX_WINDOWS; i++) {
    windows[i].id = 0;
  }

  /* Initialize desktop manager only after the filesystem stack is ready. */
#ifndef ARCH_X86_64
  desktop_manager_init();
#endif

  if (!gui_is_installer_mode()) {
    ensure_startup_flow();
    if (!startup_flow_active())
      load_dock_config();
  }

  printk(KERN_INFO "GUI: Display %ux%u initialized\n", width, height);

  return 0;
}

struct display *gui_get_display(void) { return &primary_display; }

uint32_t gui_get_screen_width(void) { return primary_display.width; }

uint32_t gui_get_screen_height(void) { return primary_display.height; }

struct window *gui_create_file_manager(int x, int y) {
  struct window *win = gui_create_window("File Manager", x, y, 450, 350);
  if (win) {
    struct fm_state *st = kmalloc(sizeof(struct fm_state));
    if (st) {
      st->path[0] = '/';
      st->path[1] = '\0';
      st->selected[0] = '\0';
      st->scroll_y = 0;
      st->context_menu_visible = 0;
      st->context_menu_x = 0;
      st->context_menu_y = 0;
      st->context_menu_target_type = 0;
      st->context_menu_target_on_item = 0;
      st->context_menu_target[0] = '\0';
      win->userdata = st;
      win->on_mouse = fm_on_mouse;
      fm_set_window_title(win, st->path);
    }
  }
  return win;
}

/* Create file manager at specific path */
struct window *gui_create_file_manager_path(int x, int y, const char *path) {
  /* Build title with path */
  char title[128] = "File Manager - ";
  int ti = 15;
  if (path) {
    for (int i = 0; path[i] && ti < 126; i++) {
      title[ti++] = path[i];
    }
  }
  title[ti] = '\0';

  struct window *win = gui_create_window(title, x, y, 450, 350);
  if (win) {
    struct fm_state *st = kmalloc(sizeof(struct fm_state));
    if (st) {
      /* Copy the provided path */
      if (path) {
        int i = 0;
        while (path[i] && i < 255) {
          st->path[i] = path[i];
          i++;
        }
        st->path[i] = '\0';
      } else {
        st->path[0] = '/';
        st->path[1] = '\0';
      }
      st->selected[0] = '\0';
      st->scroll_y = 0;
      st->context_menu_visible = 0;
      st->context_menu_x = 0;
      st->context_menu_y = 0;
      st->context_menu_target_type = 0;
      st->context_menu_target_on_item = 0;
      st->context_menu_target[0] = '\0';
      win->userdata = st;
      win->on_mouse = fm_on_mouse;
      fm_set_window_title(win, st->path);
    }
  }
  return win;
}

static void notepad_on_mouse(struct window *win, int x, int y, int buttons) {
  int content_x = BORDER_WIDTH;
  int content_y = BORDER_WIDTH + TITLEBAR_HEIGHT;

  (void)buttons;

  if (notepad_dialog_mode != NOTEPAD_DIALOG_NONE) {
    struct fm_item items[FM_MAX_ITEMS];
    int item_count = fm_collect_items(notepad_dialog_dir, items, FM_MAX_ITEMS);
    int panel_w = win->width - BORDER_WIDTH * 2 - 80;
    int panel_h = win->height - BORDER_WIDTH * 2 - TITLEBAR_HEIGHT - 70;
    int panel_x = content_x + 40;
    int panel_y = content_y + 26;
    int list_x = panel_x + 16;
    int list_y = panel_y + 68;
    int list_w = panel_w - 32;
    int row_h = 22;
    int visible_rows = (panel_h - 156) / row_h;
    if (visible_rows < 4)
      visible_rows = 4;

    if (x >= panel_x + 16 && x < panel_x + 60 && y >= panel_y + 34 &&
        y < panel_y + 56) {
      char parent[256];
      notepad_extract_parent_dir(notepad_dialog_dir, parent, sizeof(parent));
      str_copy_safe(notepad_dialog_dir, parent, sizeof(notepad_dialog_dir));
      notepad_set_status("Moved to parent folder");
      return;
    }

    for (int i = 0; i < item_count && i < visible_rows; i++) {
      int row_y = list_y + 2 + i * row_h;
      if (x >= list_x + 2 && x < list_x + list_w - 2 && y >= row_y &&
          y < row_y + row_h - 2) {
        char full_path[512];
        str_copy_safe(notepad_dialog_selected, items[i].name,
                      sizeof(notepad_dialog_selected));
        fm_join_path(notepad_dialog_dir, items[i].name, full_path, sizeof(full_path));
        if (items[i].type == 4) {
          str_copy_safe(notepad_dialog_dir, full_path, sizeof(notepad_dialog_dir));
          str_copy_safe(notepad_dialog_input, full_path, sizeof(notepad_dialog_input));
          notepad_dialog_selected[0] = '\0';
          notepad_set_status("Opened folder");
        } else {
          str_copy_safe(notepad_dialog_input, full_path, sizeof(notepad_dialog_input));
          notepad_set_status("Selected file");
        }
        return;
      }
    }

    if (x >= panel_x + panel_w - 110 && x < panel_x + panel_w - 68 &&
        y >= panel_y + panel_h - 50 && y < panel_y + panel_h - 26) {
      notepad_close_dialog();
      notepad_set_status("Dialog closed");
      return;
    }
    if (x >= panel_x + panel_w - 60 && x < panel_x + panel_w - 16 &&
        y >= panel_y + panel_h - 50 && y < panel_y + panel_h - 26) {
      notepad_confirm_dialog();
      return;
    }
    return;
  }

  if (y >= content_y && y < content_y + 30) {
    if (x >= content_x + 8 && x < content_x + 58) {
      notepad_reset_document();
      return;
    }
    if (x >= content_x + 62 && x < content_x + 112) {
      notepad_begin_dialog(NOTEPAD_DIALOG_OPEN);
      return;
    }
    if (x >= content_x + 116 && x < content_x + 166) {
      if (notepad_filepath[0])
        notepad_save_to_path(notepad_filepath);
      else
        notepad_begin_dialog(NOTEPAD_DIALOG_SAVE);
      return;
    }
    if (x >= content_x + 170 && x < content_x + 234) {
      notepad_begin_dialog(NOTEPAD_DIALOG_SAVE);
      return;
    }
    if (x >= content_x + 259 && x < content_x + 301) {
      clipboard_len = 0;
      for (int i = 0; i < notepad_cursor && i < CLIPBOARD_MAX - 1; i++) {
        clipboard_buffer[i] = notepad_text[i];
        clipboard_len++;
      }
      clipboard_buffer[clipboard_len] = '\0';
      notepad_text[0] = '\0';
      notepad_cursor = 0;
      notepad_dirty = 1;
      notepad_set_status("Cut document to clipboard");
      notepad_update_window_title();
      return;
    }
    if (x >= content_x + 305 && x < content_x + 355) {
      clipboard_len = 0;
      for (int i = 0; i < notepad_cursor && i < CLIPBOARD_MAX - 1; i++) {
        clipboard_buffer[i] = notepad_text[i];
        clipboard_len++;
      }
      clipboard_buffer[clipboard_len] = '\0';
      notepad_set_status("Copied to clipboard");
      return;
    }
    if (x >= content_x + 359 && x < content_x + 414) {
      for (int i = 0; i < clipboard_len && notepad_cursor < NOTEPAD_MAX_TEXT - 1;
           i++) {
        notepad_text[notepad_cursor++] = clipboard_buffer[i];
      }
      notepad_text[notepad_cursor] = '\0';
      notepad_dirty = 1;
      notepad_set_status("Pasted from clipboard");
      notepad_update_window_title();
      return;
    }
  }
}

void gui_open_notepad(const char *path) {
  notepad_reset_document();
  if (path)
    notepad_load_file(path);

  struct window *win = gui_create_window("Notepad", 150, 80, 450, 350);
  if (win) {
    win->on_mouse = notepad_on_mouse;
    notepad_update_window_title();
  }
}

static void rename_on_mouse(struct window *win, int x, int y, int buttons) {
  (void)buttons;
  /* Check Save Button */
  int content_y = BORDER_WIDTH + TITLEBAR_HEIGHT;
  if (y >= content_y && y < content_y + 30) {
    if (x >= BORDER_WIDTH + 10 && x < BORDER_WIDTH + 70) {
      /* Save (Rename) clicked */
      if (rename_path[0] && rename_text[0]) {
        /* Construct new full path */
        char new_full_path[512];

        /* Extract parent dir from rename_path */
        int i = 0;
        int last_slash = -1;
        while (rename_path[i]) {
          new_full_path[i] = rename_path[i];
          if (rename_path[i] == '/')
            last_slash = i;
          i++;
        }

        /* Append new name after last slash */
        int idx = last_slash + 1;
        int t_idx = 0;
        while (rename_text[t_idx]) {
          new_full_path[idx++] = rename_text[t_idx++];
        }
        new_full_path[idx] = '\0';

        int ret = user_storage_rename(rename_path, new_full_path);

        if (ret == 0) {
          printk("Rename successful: %s -> %s\n", rename_path, new_full_path);
          win->visible = 0; /* Close window */
        } else {
          printk("Rename failed: %d\n", ret);
        }
      }
    }
  }
}

void gui_open_rename(const char *path) {
  /* Clear state */
  rename_text[0] = '\0';
  rename_cursor = 0;

  /* Copy path */
  int i = 0;
  while (path[i] && i < 511) {
    rename_path[i] = path[i];
    i++;
  }
  rename_path[i] = '\0';

  /* Pre-fill text with filename only */
  int last_slash = -1;
  i = 0;
  while (rename_path[i]) {
    if (rename_path[i] == '/')
      last_slash = i;
    i++;
  }

  const char *filename = rename_path + last_slash + 1;
  i = 0;
  while (filename[i] && i < 255) {
    rename_text[i] = filename[i];
    i++;
  }
  rename_text[i] = '\0';
  rename_cursor = i;

  struct window *win = gui_create_window("Rename", 200, 150, 300, 150);
  if (win) {
    win->on_mouse = rename_on_mouse;
  }
}

/* ===================================================================== */
/* Image Viewer                                                          */
/* ===================================================================== */

extern const unsigned char bootstrap_test_png[];
extern const unsigned int bootstrap_test_png_len;

/* g_imgview is already defined as extern earlier in the file */

#define NUM_BOOTSTRAP_IMAGES 5

static const char *get_bootstrap_image_path(int index) {
  static const char *paths[] = {
      "/assets/wallpapers/landscape.jpg",
      "/assets/wallpapers/portrait.jpg",
      "/assets/wallpapers/square.jpg",
      "/assets/wallpapers/wallpaper.jpg",
      NULL,
  };
  if (index >= 0 && index < NUM_BOOTSTRAP_IMAGES)
    return paths[index];
  return NULL;
}

static const char *get_bootstrap_image_name(int index) {
  static const char *names[] = {"Landscape", "Portrait", "Square", "Wallpaper",
                                "PNG Test"};
  if (index >= 0 && index < NUM_BOOTSTRAP_IMAGES)
    return names[index];
  return "Unknown";
}

static void image_viewer_load_bootstrap(int index) {
  if (index < 0 || index >= NUM_BOOTSTRAP_IMAGES)
    return;

  /* Free previous image */
  if (g_imgview.loaded) {
    media_free_image(&g_imgview.image);
  }

  /* Decode image - detect format by magic bytes */
  uint8_t *external_data = NULL;
  size_t external_size = 0;
  const unsigned char *data = NULL;
  unsigned int len = 0;
  const char *path = get_bootstrap_image_path(index);
  int ret = -1;
  int used_external_file = 0;

  if (path && media_load_file(path, &external_data, &external_size) == 0) {
    data = external_data;
    len = (unsigned int)external_size;
    used_external_file = 1;
  } else if (index == 4) {
    data = bootstrap_test_png;
    len = bootstrap_test_png_len;
  } else {
    printk(KERN_ERR "Image Viewer: Missing bootstrap asset %s\n",
           path ? path : "(null)");
    g_imgview.loaded = 0;
    return;
  }

  /* PNG magic: 0x89 'P' 'N' 'G' */
  if (len >= 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
      data[3] == 'G') {
    ret = media_decode_png(data, len, &g_imgview.image);
  } else {
    ret = media_decode_jpeg(data, len, &g_imgview.image);
  }
  if (used_external_file)
    media_free_file(external_data);

  if (ret == 0) {
    g_imgview.loaded = 1;
    g_imgview.zoom_pct = 100;
    g_imgview.offset_x = 0;
    g_imgview.offset_y = 0;
    g_imgview.current_image_index = index;

    int i = 0;
    const char *name = get_bootstrap_image_name(index);
    while (name[i] && i < 255) {
      g_imgview.current_file[i] = name[i];
      i++;
    }
    g_imgview.current_file[i] = '\0';

    printk(KERN_INFO "Image Viewer: Loaded %s (%dx%d)\n",
           get_bootstrap_image_name(index), g_imgview.image.width,
           g_imgview.image.height);
  } else {
    printk(KERN_ERR "Image Viewer: Failed to load image\n");
    g_imgview.loaded = 0;
  }
}

/* Load image from folder file list */
static void image_viewer_load_from_folder(int index) {
  if (index < 0 || index >= g_imgview.file_count)
    return;

  /* Build full path */
  char full_path[512];
  int pi = 0;
  for (int i = 0; g_imgview.folder_path[i] && pi < 500; i++) {
    full_path[pi++] = g_imgview.folder_path[i];
  }
  for (int i = 0; g_imgview.file_list[index][i] && pi < 511; i++) {
    full_path[pi++] = g_imgview.file_list[index][i];
  }
  full_path[pi] = '\0';

  /* Load image file */
  uint8_t *data = NULL;
  size_t size = 0;
  if (media_load_file(full_path, &data, &size) != 0) {
    printk(KERN_ERR "Image Viewer: Failed to read %s\n", full_path);
    return;
  }

  /* Free previous image */
  if (g_imgview.loaded) {
    media_free_image(&g_imgview.image);
  }

  /* Decode image */
  int ret = -1;
  if (size >= 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
      data[3] == 'G') {
    ret = media_decode_png(data, size, &g_imgview.image);
  } else {
    ret = media_decode_jpeg(data, size, &g_imgview.image);
  }
  media_free_file(data);

  if (ret == 0) {
    g_imgview.loaded = 1;
    g_imgview.zoom_pct = 100;
    g_imgview.offset_x = 0;
    g_imgview.offset_y = 0;
    g_imgview.file_index = index;
    g_imgview.current_image_index = -1; /* Mark as folder-loaded */

    /* Copy filename */
    int i = 0;
    while (g_imgview.file_list[index][i] && i < 255) {
      g_imgview.current_file[i] = g_imgview.file_list[index][i];
      i++;
    }
    g_imgview.current_file[i] = '\0';

    printk(KERN_INFO "Image Viewer: Loaded %s (%dx%d) [%d/%d]\n",
           g_imgview.current_file, g_imgview.image.width,
           g_imgview.image.height, index + 1, g_imgview.file_count);
  } else {
    printk(KERN_ERR "Image Viewer: Failed to decode %s\n", full_path);
    g_imgview.loaded = 0;
  }
}

static void image_viewer_on_draw(struct window *win) {
  int screen_w = primary_display.width;
  int screen_h = primary_display.height;

  /* Content area (below titlebar, inside borders) */
  int content_x = win->x + BORDER_WIDTH;
  int content_y = win->y + BORDER_WIDTH + TITLEBAR_HEIGHT;
  int content_w = win->width - 2 * BORDER_WIDTH;
  int content_h = win->height - 2 * BORDER_WIDTH - TITLEBAR_HEIGHT;

  /* In fullscreen mode, use entire screen */
  int draw_x = g_imgview.fullscreen ? 0 : content_x;
  int draw_y = g_imgview.fullscreen ? 0 : content_y;
  int draw_w = g_imgview.fullscreen ? screen_w : content_w;
  int draw_h = g_imgview.fullscreen ? screen_h : content_h;

  /* Dark cinematic background */
  uint32_t bg_color = 0x0D0D0D;
  for (int y = draw_y; y < draw_y + draw_h; y++) {
    for (int x = draw_x; x < draw_x + draw_w; x++) {
      draw_pixel(x, y, bg_color);
    }
  }

  if (!g_imgview.loaded) {
    /* Elegant "No image" message */
    const char *msg = "Drop an image or click Next";
    int msg_len = 27;
    int text_x = draw_x + (draw_w - msg_len * 8) / 2;
    int text_y = draw_y + draw_h / 2;
    gui_draw_string(text_x, text_y, msg, 0x6B7280, bg_color);
    return;
  }

  /* Calculate display dimensions with rotation */
  int img_w = (int)g_imgview.image.width;
  int img_h = (int)g_imgview.image.height;
  int rot = g_imgview.rotation;

  /* Swap dimensions for 90/270 rotation */
  if (rot == 90 || rot == 270) {
    int tmp = img_w;
    img_w = img_h;
    img_h = tmp;
  }

  /* Auto-fit image to screen */
  int toolbar_h = 56;
  int avail_h = draw_h - toolbar_h - 20;
  int avail_w = draw_w - 20;

  int zoom = g_imgview.zoom_pct;
  if (zoom == 0) {
    /* Auto-fit mode */
    int zoom_w = (avail_w * 100) / img_w;
    int zoom_h = (avail_h * 100) / img_h;
    zoom = (zoom_w < zoom_h) ? zoom_w : zoom_h;
    if (zoom > 100)
      zoom = 100; /* Don't upscale */
  }

  int scaled_w = img_w * zoom / 100;
  int scaled_h = img_h * zoom / 100;

  /* Center image */
  int img_x = draw_x + (draw_w - scaled_w) / 2 + g_imgview.offset_x;
  int img_y = draw_y + (avail_h - scaled_h) / 2 + 10 + g_imgview.offset_y;

  /* Draw image with rotation */
  int orig_w = (int)g_imgview.image.width;
  int orig_h = (int)g_imgview.image.height;

  for (int dy = 0; dy < scaled_h; dy++) {
    int screen_y = img_y + dy;
    if (screen_y < draw_y || screen_y >= draw_y + avail_h + 10)
      continue;

    for (int dx = 0; dx < scaled_w; dx++) {
      int screen_x = img_x + dx;
      if (screen_x < draw_x || screen_x >= draw_x + draw_w)
        continue;

      /* Unscale to get image coordinates */
      int ix = dx * 100 / zoom;
      int iy = dy * 100 / zoom;

      /* Apply rotation transform */
      int src_x, src_y;
      switch (rot) {
      case 90:
        src_x = iy;
        src_y = orig_h - 1 - ix;
        break;
      case 180:
        src_x = orig_w - 1 - ix;
        src_y = orig_h - 1 - iy;
        break;
      case 270:
        src_x = orig_w - 1 - iy;
        src_y = ix;
        break;
      default: /* 0 */
        src_x = ix;
        src_y = iy;
        break;
      }

      if (src_x >= 0 && src_x < orig_w && src_y >= 0 && src_y < orig_h) {
        uint32_t pixel = g_imgview.image.pixels[src_y * orig_w + src_x];
        draw_image_pixel(screen_x, screen_y, pixel);
      }
    }
  }

  /* ============================================= */
  /* MODERN FLOATING TOOLBAR                      */
  /* ============================================= */

  int tb_w = 520;
  int tb_h = 48;
  int tb_x = draw_x + (draw_w - tb_w) / 2;
  int tb_y = draw_y + draw_h - tb_h - 16;

  /* Glassmorphism toolbar background */
  for (int y = tb_y; y < tb_y + tb_h; y++) {
    for (int x = tb_x; x < tb_x + tb_w; x++) {
      /* Semi-transparent dark with blur effect simulation */
      int dist_y = (y - tb_y < tb_h / 2) ? (y - tb_y) : (tb_y + tb_h - y);
      int alpha = 200 + (dist_y * 30 / (tb_h / 2));
      if (alpha > 230)
        alpha = 230;
      uint32_t bg = ((alpha / 10) << 16) | ((alpha / 10) << 8) | (alpha / 8);
      draw_pixel(x, y, bg);
    }
  }

  /* Rounded corners (top) */
  int corner_r = 12;
  for (int cy = 0; cy < corner_r; cy++) {
    for (int cx = 0; cx < corner_r; cx++) {
      int dx = corner_r - cx;
      int dy = corner_r - cy;
      if (dx * dx + dy * dy > corner_r * corner_r) {
        draw_pixel(tb_x + cx, tb_y + cy, bg_color);
        draw_pixel(tb_x + tb_w - 1 - cx, tb_y + cy, bg_color);
      }
    }
  }

  /* Toolbar buttons */
  int btn_size = 36;
  int btn_spacing = 8;
  int btn_y = tb_y + (tb_h - btn_size) / 2;
  int btn_x = tb_x + 16;

  uint32_t btn_bg = 0x374151;
  uint32_t btn_hover = 0x4B5563;
  uint32_t icon_color = 0xE5E7EB;

  for (int i = 0; i < 8; i++) {
    /* Check hover */
    int is_hover = (mouse_x >= btn_x && mouse_x < btn_x + btn_size &&
                    mouse_y >= btn_y && mouse_y < btn_y + btn_size);
    uint32_t bg = is_hover ? btn_hover : btn_bg;

    /* Draw rounded button */
    int r = 8;
    for (int y = 0; y < btn_size; y++) {
      for (int x = 0; x < btn_size; x++) {
        int in_corner = 0;
        if (x < r && y < r && (r - x) * (r - x) + (r - y) * (r - y) > r * r)
          in_corner = 1;
        if (x >= btn_size - r && y < r &&
            (x - btn_size + r + 1) * (x - btn_size + r + 1) +
                    (r - y) * (r - y) >
                r * r)
          in_corner = 1;
        if (x < r && y >= btn_size - r &&
            (r - x) * (r - x) +
                    (y - btn_size + r + 1) * (y - btn_size + r + 1) >
                r * r)
          in_corner = 1;
        if (x >= btn_size - r && y >= btn_size - r &&
            (x - btn_size + r + 1) * (x - btn_size + r + 1) +
                    (y - btn_size + r + 1) * (y - btn_size + r + 1) >
                r * r)
          in_corner = 1;
        if (!in_corner) {
          draw_pixel(btn_x + x, btn_y + y, bg);
        }
      }
    }
    /* Draw pre-rendered RGBA icon from toolbar_icons.h */
    const uint32_t *icon_data = toolbar_icons[i];
    int icon_x = btn_x + (btn_size - TOOLBAR_ICON_SIZE) / 2;
    int icon_y = btn_y + (btn_size - TOOLBAR_ICON_SIZE) / 2;

    for (int iy = 0; iy < TOOLBAR_ICON_SIZE; iy++) {
      for (int ix = 0; ix < TOOLBAR_ICON_SIZE; ix++) {
        uint32_t pixel = icon_data[iy * TOOLBAR_ICON_SIZE + ix];
        uint8_t alpha = (pixel >> 24) & 0xFF;
        if (alpha > 0) {
          /* Simple alpha blending: if alpha > 128, draw white */
          if (alpha > 128) {
            draw_pixel(icon_x + ix, icon_y + iy, icon_color);
          }
        }
      }
    }
    btn_x += btn_size + btn_spacing;
  }

  /* Image info text */
  char info[64];
  int idx = 0;
  /* Dimensions */
  int w = (int)g_imgview.image.width;
  int h = (int)g_imgview.image.height;
  if (w >= 1000) {
    info[idx++] = '0' + (w / 1000) % 10;
  }
  if (w >= 100) {
    info[idx++] = '0' + (w / 100) % 10;
  }
  if (w >= 10) {
    info[idx++] = '0' + (w / 10) % 10;
  }
  info[idx++] = '0' + w % 10;
  info[idx++] = 'x';
  if (h >= 1000) {
    info[idx++] = '0' + (h / 1000) % 10;
  }
  if (h >= 100) {
    info[idx++] = '0' + (h / 100) % 10;
  }
  if (h >= 10) {
    info[idx++] = '0' + (h / 10) % 10;
  }
  info[idx++] = '0' + h % 10;
  info[idx++] = ' ';
  /* Rotation */
  if (rot > 0) {
    if (rot >= 100)
      info[idx++] = '0' + (rot / 100) % 10;
    if (rot >= 10)
      info[idx++] = '0' + (rot / 10) % 10;
    info[idx++] = '0' + rot % 10;
    info[idx++] = (char)176; /* degree symbol approximation */
  }
  info[idx] = '\0';

  gui_draw_string(tb_x + tb_w - 120, btn_y + 12, info, 0x9CA3AF, 0x1F2937);
}

static void image_viewer_on_mouse(struct window *win, int x, int y,
                                  int buttons) {
  /* x,y are already window-relative (0,0 = window top-left) */

  /* Content area within window (relative coords) */
  int content_x = BORDER_WIDTH;
  int content_y = BORDER_WIDTH + TITLEBAR_HEIGHT;
  int content_w = win->width - 2 * BORDER_WIDTH;
  int content_h = win->height - 2 * BORDER_WIDTH - TITLEBAR_HEIGHT;

  /* Toolbar position within content area */
  int tb_w = 520;
  int tb_h = 48;
  int tb_x = content_x + (content_w - tb_w) / 2;
  int tb_y = content_y + content_h - tb_h - 16;

  int btn_size = 36;
  int btn_spacing = 8;
  int btn_y_pos = tb_y + (tb_h - btn_size) / 2;
  int btn_x = tb_x + 16;

  /* Check each of the 8 toolbar buttons */
  for (int i = 0; i < 8; i++) {
    if (x >= btn_x && x < btn_x + btn_size && y >= btn_y_pos &&
        y < btn_y_pos + btn_size) {

      switch (i) {
      case 0: /* Previous */
        if (g_imgview.file_count > 0) {
          /* Folder-based navigation */
          int new_index = g_imgview.file_index - 1;
          if (new_index < 0)
            new_index = g_imgview.file_count - 1;
          image_viewer_load_from_folder(new_index);
        } else {
          /* Fallback to bootstrap images */
          if (g_imgview.current_image_index <= 0) {
            image_viewer_load_bootstrap(NUM_BOOTSTRAP_IMAGES - 1);
          } else {
            image_viewer_load_bootstrap(g_imgview.current_image_index - 1);
          }
        }
        break;

      case 1: /* Next */
        if (g_imgview.file_count > 0) {
          /* Folder-based navigation */
          int new_index = g_imgview.file_index + 1;
          if (new_index >= g_imgview.file_count)
            new_index = 0;
          image_viewer_load_from_folder(new_index);
        } else {
          /* Fallback to bootstrap images */
          if (g_imgview.current_image_index < 0 ||
              g_imgview.current_image_index >= NUM_BOOTSTRAP_IMAGES - 1) {
            image_viewer_load_bootstrap(0);
          } else {
            image_viewer_load_bootstrap(g_imgview.current_image_index + 1);
          }
        }
        break;

      case 2: /* Rotate Right (CW) */
        g_imgview.rotation = (g_imgview.rotation + 90) % 360;
        break;

      case 3: /* Rotate Left (CCW) */
        g_imgview.rotation = (g_imgview.rotation + 270) % 360;
        break;

      case 4: /* Zoom In */
        if (g_imgview.zoom_pct == 0) {
          g_imgview.zoom_pct = 100;
        }
        g_imgview.zoom_pct = g_imgview.zoom_pct * 125 / 100;
        if (g_imgview.zoom_pct > 400) {
          g_imgview.zoom_pct = 400;
        }
        break;

      case 5: /* Zoom Out */
        if (g_imgview.zoom_pct == 0) {
          g_imgview.zoom_pct = 100;
        }
        g_imgview.zoom_pct = g_imgview.zoom_pct * 80 / 100;
        if (g_imgview.zoom_pct < 10) {
          g_imgview.zoom_pct = 10;
        }
        break;

      case 6:                   /* Fit */
        g_imgview.zoom_pct = 0; /* Auto-fit mode */
        g_imgview.offset_x = 0;
        g_imgview.offset_y = 0;
        break;

      case 7: /* Fullscreen Toggle */
        g_imgview.fullscreen = !g_imgview.fullscreen;
        g_imgview.zoom_pct = 0; /* Reset to auto-fit */
        g_imgview.offset_x = 0;
        g_imgview.offset_y = 0;
        break;
      }
      return;
    }
    btn_x += btn_size + btn_spacing;
  }

  /* Pan disabled - image stays fixed */
  (void)buttons; /* Unused */
}

void gui_open_image_gallery(void) {
  struct window *win = gui_create_window("Image Gallery", 100, 80, 700, 550);
  if (win) {
    win->on_draw = image_viewer_on_draw;
    win->on_mouse = image_viewer_on_mouse;

    /* Load first image */
    if (!g_imgview.loaded) {
      image_viewer_load_bootstrap(0);
    }
  }
}
