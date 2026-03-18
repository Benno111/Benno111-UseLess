/*
 * UnixOS Kernel - Main Entry Point
 *
 * This is the C entry point called from boot.S after basic
 * hardware initialization is complete.
 */

#include "apps/embedded_apps.h"
#include "arch/arch.h"
#include "build_uuid.h"
#include "drivers/pci.h"
#include "drivers/uart.h"
#include "fs/vfs.h"
#include "media/media.h"
#include "media/seed_assets.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "printk.h"
#include "sched/sched.h"
#include "types.h"

/* Kernel version */
#define VIBOS_VERSION_MAJOR 0
#define VIBOS_VERSION_MINOR 5
#define VIBOS_VERSION_PATCH 0

/* External symbols from linker script */
extern char __kernel_start[];
extern char __kernel_end[];
extern char __bss_start[];
extern char __bss_end[];

/* Forward declarations for GUI bring-up helpers used across this file. */
struct window;
struct terminal;

/* Forward declarations */
static void print_banner(void);
static void init_subsystems(void *dtb);
static void start_init_process(void);
static void populate_seed_filesystem(void);
static void populate_installer_payload(void);
static void populate_seed_tree_at(const char *prefix);
static void ensure_boot_payload_dirs(const char *prefix);
static int copy_tree_to_prefix(const char *src_root, const char *dst_root,
                               int skip_payload_roots);
static int copy_tree_callback(void *ctx, const char *name, int len,
                              loff_t offset, ino_t ino, unsigned type);
static int build_seed_path(char *dst, size_t dst_size, const char *prefix,
                           const char *path);
static void seed_make_dir(const char *prefix, const char *path);
static void seed_write_text(const char *prefix, const char *path, mode_t mode,
                            const char *content);
static void seed_write_bytes(const char *prefix, const char *path, mode_t mode,
                             const uint8_t *data, size_t size);
static void keyboard_handler(int key);
#ifdef ARCH_X86_64
static void start_x86_64_bringup(void);
#endif

/*
 * kernel_main - Main kernel entry point
 * @dtb: Pointer to device tree blob passed by bootloader
 *
 * This function never returns. After initialization, it either:
 * 1. Starts the init process and enters the scheduler
 * 2. Panics if initialization fails
 */
void kernel_main(void *dtb) {
  /* Initialize early console for debugging */
  uart_early_init();

  /* Print boot banner */
  print_banner();

  (void)dtb; /* Suppress unused warning */
  (void)__kernel_start;
  (void)__kernel_end;

#ifdef ARCH_X86_64
  start_x86_64_bringup();
  panic("x86_64 bring-up returned unexpectedly!");
#endif

  /* Initialize all kernel subsystems */
  init_subsystems(dtb);

  printk(KERN_INFO "All subsystems initialized successfully\n");
  printk(KERN_INFO "Starting init process...\n\n");

  /* Start the first userspace process */
  start_init_process();

  /* This point should never be reached */
  panic("kernel_main returned unexpectedly!");
}

/*
 * print_banner - Display kernel boot banner
 */
static void print_banner(void) {
  printk("\n");
  printk("        _  _         ___  ____  \n");
  printk(" __   _(_)| |__     / _ \\/ ___| \n");
  printk(" \\ \\ / / || '_ \\   | | | \\___ \\ \n");
  printk("  \\ V /| || |_) |  | |_| |___) |\n");
  printk("   \\_/ |_||_.__/    \\___/|____/ \n");
  printk("\n");
#ifdef ARCH_X86_64
  printk("OS next stage v%d.%d.%d - x86_64 bring-up\n", VIBOS_VERSION_MAJOR,
         VIBOS_VERSION_MINOR, VIBOS_VERSION_PATCH);
  printk("A Unix-like operating system for x86_64\n");
#else
  printk("OS next stage v%d.%d.%d - ARM64 with GUI\n", VIBOS_VERSION_MAJOR,
         VIBOS_VERSION_MINOR, VIBOS_VERSION_PATCH);
  printk("A Unix-like operating system for ARM64\n");
#endif
  printk("Copyright (c) 2026 OS next stage Project\n");
  printk("Build UUID: %s\n", BUILD_UUID);
  printk("\n");
}

#ifdef ARCH_X86_64
static void start_x86_64_bringup(void) {
  printk(KERN_INFO "[INIT] x86_64 bring-up mode\n");
  printk(KERN_INFO "  Using Limine framebuffer and minimal GUI path\n");

  extern int fb_init(void);
  extern void fb_get_info(uint32_t **buffer, uint32_t *width, uint32_t *height);
  extern uint32_t fb_get_pitch(void);
  extern void kmalloc_init(void);
  extern int gui_init(uint32_t *framebuffer, uint32_t width, uint32_t height,
                      uint32_t pitch);
  extern struct window *gui_create_window(const char *title, int x, int y,
                                          int w, int h);
  extern void gui_focus_window(struct window *win);
  extern void gui_compose(void);
  extern struct terminal *term_create(int x, int y, int cols, int rows);
  extern void term_set_active(struct terminal * term);
  extern void gui_set_window_userdata(struct window *win, void *data);
  extern void desktop_manager_init(void);
  extern void pci_init(void);
  extern void storage_init(void);
  extern void arch_timer_init(void);
  extern int vfs_mount(const char *source, const char *target,
                       const char *filesystemtype, unsigned long mountflags,
                       const void *data);
  extern void vfs_init(void);
  extern int ramfs_init(void);
  extern void *limine_get_rsdp(void);
  extern const char *limine_get_kernel_cmdline(void);
  extern void acpi_init(void *rsdp_ptr);
  extern void boot_parse_cmdline(const char *cmdline);
  extern int boot_is_installer_mode(void);
  extern int input_init(void);
  extern void input_poll(void);
  extern void input_set_key_callback(void (*callback)(int key));
  extern void input_set_mouse_bounds(int width, int height);
  extern void input_set_mouse_scale(int scale);
  extern void mouse_get_position(int *x, int *y);
  extern int mouse_get_buttons(void);
  extern void gui_handle_mouse_event(int x, int y, int buttons);
  extern void gui_handle_key_event(int key);

  uint32_t *fb_buffer = 0;
  uint32_t fb_width = 0;
  uint32_t fb_height = 0;
  uint32_t fb_pitch = 0;
  struct window *term_window = 0;
  struct window *installer_window = 0;
  int last_mx = -1;
  int last_my = -1;
  int last_buttons = -1;
  int needs_redraw = 1;
  uint64_t last_refresh = 0;
  const uint64_t REFRESH_MS = 33;

  if (fb_init() != 0) {
    panic("Failed to initialize framebuffer on x86_64!");
  }

  fb_get_info(&fb_buffer, &fb_width, &fb_height);
  fb_pitch = fb_get_pitch();

  if (!fb_buffer || !fb_width || !fb_height) {
    panic("No usable framebuffer available on x86_64!");
  }

  boot_parse_cmdline(limine_get_kernel_cmdline());

  kmalloc_init();
  arch_timer_init();
  vfs_init();
  ramfs_init();
  if (vfs_mount("ramfs", "/", "ramfs", 0, NULL) != 0) {
    panic("Failed to mount x86_64 root filesystem!");
  }
  acpi_init(limine_get_rsdp());
  populate_seed_filesystem();
  storage_init();
  printk(KERN_INFO
         "x86_64: Deferring PCI probe until after GUI initialization\n");

  printk(KERN_INFO "  Framebuffer ready: %ux%u\n", fb_width, fb_height);
  if (gui_init(fb_buffer, fb_width, fb_height,
               fb_pitch ? fb_pitch : (fb_width * 4)) != 0) {
    panic("Failed to initialize x86_64 GUI bring-up!");
  }

  printk(KERN_INFO "x86_64: Running late PCI probe\n");
  pci_init();

  input_init();
  input_set_key_callback(keyboard_handler);
  input_set_mouse_bounds((int)fb_width, (int)fb_height);
  input_set_mouse_scale(2);
  if (boot_is_installer_mode()) {
    int installer_w = 640;
    int installer_h = 420;
    int installer_x = (int)(fb_width > (uint32_t)installer_w
                                ? (fb_width - (uint32_t)installer_w) / 2
                                : 20);
    int installer_y = (int)(fb_height > (uint32_t)installer_h
                                ? (fb_height - (uint32_t)installer_h) / 2
                                : 20);
    installer_window =
        gui_create_window("Installer", installer_x, installer_y, installer_w,
                          installer_h);
    if (installer_window) {
      gui_focus_window(installer_window);
    }
  } else {
    desktop_manager_init();
    term_window = gui_create_window("Terminal", 50, 50, 700, 420);
    if (term_window) {
      struct terminal *term = term_create(52, 80, 80, 20);
      if (term) {
        gui_set_window_userdata(term_window, term);
        term_set_active(term);
      }
      gui_focus_window(term_window);
    }
  }

  printk(KERN_INFO "x86_64 minimal GUI ready.\n");
  gui_compose();
  last_refresh = arch_timer_get_ms();

  while (1) {
    extern int uart_getc_nonblock(void);
    int mx, my, mbuttons;
    int c;
    uint64_t now;

    input_poll();

    c = uart_getc_nonblock();
    if (c >= 0) {
      gui_handle_key_event(c);
      needs_redraw = 1;
    }

    mouse_get_position(&mx, &my);
    mbuttons = mouse_get_buttons();
    if (mx != last_mx || my != last_my || mbuttons != last_buttons) {
      gui_handle_mouse_event(mx, my, mbuttons);
      last_mx = mx;
      last_my = my;
      last_buttons = mbuttons;
      needs_redraw = 1;
    }

    now = arch_timer_get_ms();
    if (now - last_refresh >= REFRESH_MS) {
      last_refresh = now;
      needs_redraw = 1;
    }

    if (needs_redraw) {
      gui_compose();
      needs_redraw = 0;
    }

    for (volatile int i = 0; i < 500; i++) {
    }
  }
}
#endif

static void populate_seed_filesystem(void) {
  populate_seed_tree_at("");
  populate_installer_payload();
}

static int build_seed_path(char *dst, size_t dst_size, const char *prefix,
                           const char *path) {
  size_t idx = 0;
  const char *use_prefix = prefix ? prefix : "";
  const char *use_path = path ? path : "";

  if (!dst || dst_size == 0)
    return -1;

  if (!use_prefix[0]) {
    if (use_path[0] != '/' && idx < dst_size - 1)
      dst[idx++] = '/';
  } else {
    for (size_t i = 0; use_prefix[i] && idx < dst_size - 1; i++)
      dst[idx++] = use_prefix[i];
    if (idx > 0 && dst[idx - 1] == '/' && use_path[0] == '/')
      use_path++;
    else if (idx > 0 && dst[idx - 1] != '/' && use_path[0] != '/' &&
             idx < dst_size - 1)
      dst[idx++] = '/';
  }

  for (size_t i = 0; use_path[i] && idx < dst_size - 1; i++)
    dst[idx++] = use_path[i];
  dst[idx] = '\0';
  return 0;
}

static void seed_make_dir(const char *prefix, const char *path) {
  char full_path[256];
  extern int ramfs_create_dir(const char *path, mode_t mode);

  if (build_seed_path(full_path, sizeof(full_path), prefix, path) != 0)
    return;
  ramfs_create_dir(full_path, 0755);
}

static void seed_write_text(const char *prefix, const char *path, mode_t mode,
                            const char *content) {
  char full_path[256];
  extern int ramfs_create_file(const char *path, mode_t mode,
                               const char *content);

  if (build_seed_path(full_path, sizeof(full_path), prefix, path) != 0)
    return;
  ramfs_create_file(full_path, mode, content);
}

static void seed_write_bytes(const char *prefix, const char *path, mode_t mode,
                             const uint8_t *data, size_t size) {
  char full_path[256];
  extern int ramfs_create_file_bytes(const char *path, mode_t mode,
                                     const uint8_t *data, size_t size);

  if (build_seed_path(full_path, sizeof(full_path), prefix, path) != 0)
    return;
  ramfs_create_file_bytes(full_path, mode, data, size);
}

static void populate_seed_tree_at(const char *prefix) {
  extern const unsigned char bootstrap_test_png[];
  extern const unsigned int bootstrap_test_png_len;

  seed_make_dir(prefix, "Documents");
  seed_make_dir(prefix, "Downloads");
  seed_make_dir(prefix, "Pictures");
  seed_make_dir(prefix, "System");
  seed_make_dir(prefix, "Desktop");
  seed_make_dir(prefix, "/Desktop/Projects");

  seed_write_text(prefix, "/Desktop/notes.txt", 0644,
                  "Welcome to OS next stage!\n\nThis is your desktop - right-click "
                  "for options!\n");
  seed_write_text(prefix, "/Desktop/readme.txt", 0644,
                  "OS next stage Desktop Manager\n\n- Double-click to open files\n- "
                  "Right-click for context menu\n");
  seed_write_text(prefix, "readme.txt", 0644,
                  "Welcome to OS next stage!\nThis is a real file in RamFS.");
  seed_write_text(prefix, "todo.txt", 0644,
                  "- Implement Browser\n- Fix Bugs\n- Sleep");
  seed_write_bytes(prefix, "sample.mp3", 0644, vib_seed_mp3, vib_seed_mp3_len);
  seed_write_bytes(prefix, "Pictures/test.png", 0644, bootstrap_test_png,
                   bootstrap_test_png_len);

  seed_make_dir(prefix, "bin");
  seed_make_dir(prefix, "sbin");
  seed_make_dir(prefix, "usr");
  seed_make_dir(prefix, "usr/bin");

  seed_write_bytes(prefix, "/sbin/init", 0755, init_bin, init_bin_len);
  seed_write_bytes(prefix, "/bin/login", 0755, login_bin, login_bin_len);
  seed_write_bytes(prefix, "/bin/sh", 0755, shell_bin, shell_bin_len);

  seed_make_dir(prefix, "examples");
  seed_write_text(prefix, "examples/hello.py", 0644,
                  "# Hello World in Python for OS next stage\n"
                  "# Run with: run hello.py\n\n"
                  "def greet(name):\n"
                  "    return 'Hello, ' + name + '!'\n\n"
                  "def main():\n"
                  "    print('Welcome to OS next stage Python Demo')\n"
                  "    message = greet('OS next stage User')\n"
                  "    print(message)\n\n"
                  "if __name__ == '__main__':\n"
                  "    main()\n");
  seed_write_text(prefix, "examples/fibonacci.py", 0644,
                  "# Fibonacci Sequence in Python\n"
                  "# Run with: run fibonacci.py\n\n"
                  "def fibonacci(n):\n"
                  "    if n <= 0: return []\n"
                  "    fib = [0, 1]\n"
                  "    for i in range(2, n):\n"
                  "        fib.append(fib[i-1] + fib[i-2])\n"
                  "    return fib\n\n"
                  "print(fibonacci(10))\n");
  seed_write_text(prefix, "examples/hello.nano", 0644,
                  "// Hello World in NanoLang\n"
                  "// Run with: run hello.nano\n\n"
                  "fn greet(name: str) -> str {\n"
                  "    return 'Hello, ' + name + '!';\n"
                  "}\n\n"
                  "fn main() {\n"
                  "    print('Welcome to NanoLang');\n"
                  "    let msg = greet('OS next stage');\n"
                  "    print(msg);\n"
                  "}\n");
  seed_write_text(prefix, "examples/calculator.nano", 0644,
                  "// Calculator in NanoLang\n"
                  "fn add(a: int, b: int) -> int { return a + b; }\n"
                  "fn main() {\n"
                  "    print('42 + 7 = ');\n"
                  "    print(add(42, 7));\n"
                  "}\n");
}

static void ensure_boot_payload_dirs(const char *prefix) {
  seed_make_dir(prefix, "boot");
  seed_make_dir(prefix, "EFI");
  seed_make_dir(prefix, "EFI/BOOT");
  seed_make_dir(prefix, "limine");
}

typedef struct {
  const char *src_root;
  const char *dst_root;
  int skip_payload_roots;
} seed_copy_ctx_t;

static int copy_tree_callback(void *ctx, const char *name, int len,
                              loff_t offset, ino_t ino, unsigned type) {
  seed_copy_ctx_t *copy = (seed_copy_ctx_t *)ctx;
  char src_path[256];
  char dst_path[256];
  int src_len = 0;
  int dst_len = 0;
  struct file *dir;
  seed_copy_ctx_t next;
  uint8_t *data = NULL;
  size_t size = 0;

  (void)offset;
  (void)ino;

  if (!copy || !name || len <= 0)
    return 0;
  if ((len == 1 && name[0] == '.') ||
      (len == 2 && name[0] == '.' && name[1] == '.'))
    return 0;
  if (copy->skip_payload_roots &&
      ((len == 7 && name[0] == 'i' && name[1] == 'n' && name[2] == 's' &&
        name[3] == 't' && name[4] == 'a' && name[5] == 'l' && name[6] == 'l') ||
       (len == 5 && name[0] == 's' && name[1] == 'e' && name[2] == 't' &&
        name[3] == 'u' && name[4] == 'p')))
    return 0;

  src_path[0] = '\0';
  if (copy->src_root) {
    for (src_len = 0;
         copy->src_root[src_len] && src_len < (int)sizeof(src_path) - 1;
         src_len++) {
      src_path[src_len] = copy->src_root[src_len];
    }
    src_path[src_len] = '\0';
  }
  if (!(src_len == 1 && src_path[0] == '/') && src_len < (int)sizeof(src_path) - 1)
    src_path[src_len++] = '/';
  for (int i = 0; i < len && src_len < (int)sizeof(src_path) - 1; i++)
    src_path[src_len++] = name[i];
  src_path[src_len] = '\0';

  dst_path[0] = '\0';
  if (copy->dst_root) {
    for (dst_len = 0;
         copy->dst_root[dst_len] && dst_len < (int)sizeof(dst_path) - 1;
         dst_len++) {
      dst_path[dst_len] = copy->dst_root[dst_len];
    }
    dst_path[dst_len] = '\0';
  }
  if (!(dst_len == 1 && dst_path[0] == '/') && dst_len < (int)sizeof(dst_path) - 1)
    dst_path[dst_len++] = '/';
  for (int i = 0; i < len && dst_len < (int)sizeof(dst_path) - 1; i++)
    dst_path[dst_len++] = name[i];
  dst_path[dst_len] = '\0';

  if (type == 4) {
    seed_make_dir("", dst_path);
    next.src_root = src_path;
    next.dst_root = dst_path;
    next.skip_payload_roots = 0;
    dir = vfs_open(src_path, O_RDONLY, 0);
    if (!dir)
      return 0;
    vfs_readdir(dir, &next, copy_tree_callback);
    vfs_close(dir);
    return 0;
  }

  if (media_load_file(src_path, &data, &size) == 0) {
    media_install_file(dst_path, data, size);
    media_free_file(data);
  }
  return 0;
}

static int copy_tree_to_prefix(const char *src_root, const char *dst_root,
                               int skip_payload_roots) {
  struct file *dir;
  seed_copy_ctx_t ctx;

  dir = vfs_open(src_root, O_RDONLY, 0);
  if (!dir)
    return -1;

  ctx.src_root = src_root;
  ctx.dst_root = dst_root;
  ctx.skip_payload_roots = skip_payload_roots;
  vfs_readdir(dir, &ctx, copy_tree_callback);
  vfs_close(dir);
  return 0;
}

static void populate_installer_payload(void) {
#ifdef ARCH_X86_64
  extern int boot_is_installer_mode(void);
  extern void *limine_get_kernel_file_addr(void);
  extern uint64_t limine_get_kernel_file_size(void);
  extern const unsigned char installer_payload_bootx64_efi[];
  extern const unsigned char installer_payload_bootx64_efi_end[];
  extern const unsigned char installer_payload_limine_bios_sys[];
  extern const unsigned char installer_payload_limine_bios_sys_end[];
  extern const unsigned char installer_payload_limine_bios_cd_bin[];
  extern const unsigned char installer_payload_limine_bios_cd_bin_end[];
  extern const unsigned char installer_payload_limine_uefi_cd_bin[];
  extern const unsigned char installer_payload_limine_uefi_cd_bin_end[];
  static const char *installed_limine_cfg =
      "# Limine Configuration File\n"
      "# OS next stage x64\n"
      "\n"
      "timeout: 0\n"
      "default_entry: 1\n"
      "\n"
      "/OS next stage\n"
      "    protocol: limine\n"
      "    kernel_path: boot():/boot/main.sys\n";
  static const char *installer_limine_cfg =
      "# Limine Configuration File\n"
      "# OS next stage x64 installer ISO\n"
      "\n"
      "timeout: 0\n"
      "default_entry: 1\n"
      "\n"
      "/OS next stage Installer\n"
      "    protocol: limine\n"
      "    kernel_path: boot():/boot/main.sys\n"
      "    cmdline: boot=usb mode=installer\n";
  static const char *image_info =
      "OS next stage System Image\n"
      "\n"
      "This installer boot seeds a bundled system image at\n"
      "/install/system-image so the GUI installer can copy it to disk.\n";
  static const char *setup_info =
      "OS next stage Installer Media\n"
      "\n"
      "This directory mirrors the bootable installer media contents while\n"
      "running in setup mode.\n";
  const uint8_t *kernel_image;
  size_t kernel_size;
  size_t bootx64_efi_size;
  size_t limine_bios_sys_size;
  size_t limine_bios_cd_size;
  size_t limine_uefi_cd_size;

  if (!boot_is_installer_mode())
    return;

  kernel_image = (const uint8_t *)limine_get_kernel_file_addr();
  kernel_size = (size_t)limine_get_kernel_file_size();
  if (!kernel_image || kernel_size == 0) {
    printk(KERN_ERR "INSTALL: kernel image unavailable for installer payload\n");
    return;
  }
  bootx64_efi_size = (size_t)(installer_payload_bootx64_efi_end -
                              installer_payload_bootx64_efi);
  limine_bios_sys_size = (size_t)(installer_payload_limine_bios_sys_end -
                                  installer_payload_limine_bios_sys);
  limine_bios_cd_size = (size_t)(installer_payload_limine_bios_cd_bin_end -
                                 installer_payload_limine_bios_cd_bin);
  limine_uefi_cd_size = (size_t)(installer_payload_limine_uefi_cd_bin_end -
                                 installer_payload_limine_uefi_cd_bin);

  seed_make_dir("", "/install");
  seed_make_dir("", "/install/system-image");
  seed_make_dir("", "/setup");
  seed_make_dir("", "/setup/boot");
  seed_make_dir("", "/setup/EFI");
  seed_make_dir("", "/setup/EFI/BOOT");
  seed_make_dir("", "/setup/limine");
  seed_make_dir("", "/setup/install");
  seed_make_dir("", "/setup/install/system-image");
  ensure_boot_payload_dirs("/install/system-image");
  ensure_boot_payload_dirs("/setup");
  ensure_boot_payload_dirs("/setup/install/system-image");
  copy_tree_to_prefix("/", "/install/system-image", 1);
  copy_tree_to_prefix("/", "/setup/install/system-image", 1);

  if (media_install_file("/setup/boot/main.sys", kernel_image, kernel_size) !=
          0 ||
      media_install_file("/setup/boot/bootloader.sys", kernel_image,
                         kernel_size) != 0 ||
      media_install_text_file("/setup/limine.conf", installer_limine_cfg) !=
          0 ||
      media_install_text_file("/setup/boot/limine.conf", installer_limine_cfg) !=
          0 ||
      media_install_text_file("/setup/limine/limine.conf",
                              installer_limine_cfg) != 0 ||
      media_install_text_file("/setup/EFI/BOOT/limine.conf",
                              installer_limine_cfg) != 0 ||
      media_install_file("/setup/boot/limine-bios.sys",
                         installer_payload_limine_bios_sys,
                         limine_bios_sys_size) != 0 ||
      media_install_file("/setup/boot/limine-bios-cd.bin",
                         installer_payload_limine_bios_cd_bin,
                         limine_bios_cd_size) != 0 ||
      media_install_file("/setup/boot/limine-uefi-cd.bin",
                         installer_payload_limine_uefi_cd_bin,
                         limine_uefi_cd_size) != 0 ||
      media_install_file("/setup/EFI/BOOT/BOOTX64.EFI",
                         installer_payload_bootx64_efi,
                         bootx64_efi_size) != 0 ||
      media_install_text_file("/setup/SETUP_INFO.txt", setup_info) != 0 ||
      media_install_file("/install/system-image/boot/main.sys", kernel_image,
                         kernel_size) != 0 ||
      media_install_file("/install/system-image/boot/bootloader.sys",
                         kernel_image, kernel_size) != 0 ||
      media_install_text_file("/install/system-image/limine.conf",
                              installed_limine_cfg) != 0 ||
      media_install_text_file("/install/system-image/boot/limine.conf",
                              installed_limine_cfg) != 0 ||
      media_install_text_file("/install/system-image/limine/limine.conf",
                              installed_limine_cfg) != 0 ||
      media_install_text_file("/install/system-image/EFI/BOOT/limine.conf",
                              installed_limine_cfg) != 0 ||
      media_install_file("/install/system-image/boot/limine-bios.sys",
                         installer_payload_limine_bios_sys,
                         limine_bios_sys_size) != 0 ||
      media_install_file("/install/system-image/boot/limine-bios-cd.bin",
                         installer_payload_limine_bios_cd_bin,
                         limine_bios_cd_size) != 0 ||
      media_install_file("/install/system-image/boot/limine-uefi-cd.bin",
                         installer_payload_limine_uefi_cd_bin,
                         limine_uefi_cd_size) != 0 ||
      media_install_file("/install/system-image/EFI/BOOT/BOOTX64.EFI",
                         installer_payload_bootx64_efi,
                         bootx64_efi_size) != 0 ||
      media_install_file("/setup/install/system-image/boot/main.sys",
                         kernel_image, kernel_size) != 0 ||
      media_install_file("/setup/install/system-image/boot/bootloader.sys",
                         kernel_image, kernel_size) != 0 ||
      media_install_text_file("/setup/install/system-image/limine.conf",
                              installed_limine_cfg) != 0 ||
      media_install_text_file("/setup/install/system-image/boot/limine.conf",
                              installed_limine_cfg) != 0 ||
      media_install_text_file("/setup/install/system-image/limine/limine.conf",
                              installed_limine_cfg) != 0 ||
      media_install_text_file("/setup/install/system-image/EFI/BOOT/limine.conf",
                              installed_limine_cfg) != 0 ||
      media_install_file("/setup/install/system-image/boot/limine-bios.sys",
                         installer_payload_limine_bios_sys,
                         limine_bios_sys_size) != 0 ||
      media_install_file("/setup/install/system-image/boot/limine-bios-cd.bin",
                         installer_payload_limine_bios_cd_bin,
                         limine_bios_cd_size) != 0 ||
      media_install_file("/setup/install/system-image/boot/limine-uefi-cd.bin",
                         installer_payload_limine_uefi_cd_bin,
                         limine_uefi_cd_size) != 0 ||
      media_install_file("/setup/install/system-image/EFI/BOOT/BOOTX64.EFI",
                         installer_payload_bootx64_efi,
                         bootx64_efi_size) != 0 ||
      media_install_text_file("/install/system-image/IMAGE_INFO.txt",
                              image_info) != 0 ||
      media_install_text_file("/setup/install/system-image/IMAGE_INFO.txt",
                              image_info) != 0) {
    printk(KERN_ERR "INSTALL: failed to seed setup media payload\n");
    return;
  }

  printk(KERN_INFO "INSTALL: bundled system image payload seeded in RAMFS\n");
  printk(KERN_INFO "INSTALL: setup media exposed at /setup/\n");
#endif
}

/*
 * init_subsystems - Initialize all kernel subsystems
 * @dtb: Device tree blob for hardware discovery
 */
static void init_subsystems(void *dtb) {
  int ret;

  /* ================================================================= */
  /* Phase 1: Core Hardware */
  /* ================================================================= */

  printk(KERN_INFO "[INIT] Phase 1: Core Hardware\n");

  /* Parse device tree for hardware information */
  printk(KERN_INFO "  Parsing device tree...\n");
  (void)dtb; /* TODO: dtb_parse(dtb); */

  /* Initialize interrupt controller */
  printk(KERN_INFO "  Initializing interrupt controller...\n");
  arch_irq_init();

  /* Initialize system timer */
  printk(KERN_INFO "  Initializing timer...\n");
  arch_timer_init();

  /* ================================================================= */
  /* Phase 2: Memory Management */
  /* ================================================================= */

  printk(KERN_INFO "[INIT] Phase 2: Memory Management\n");

  /* Initialize physical memory manager */
  printk(KERN_INFO "  Initializing physical memory manager...\n");
  ret = pmm_init();
  if (ret < 0) {
    panic("Failed to initialize physical memory manager!");
  }
  printk(KERN_INFO "  About to init VMM...\n");

  /* Initialize virtual memory manager */
  printk(KERN_INFO "  Initializing virtual memory manager...\n");
  ret = vmm_init();
  if (ret < 0) {
    panic("Failed to initialize virtual memory manager!");
  }

  /* Initialize kernel heap */
  printk(KERN_INFO "  Initializing kernel heap...\n");
  extern void kmalloc_init(void);
  kmalloc_init();

  /* ================================================================= */
  /* Phase 3: Process Management */
  /* ================================================================= */

  printk(KERN_INFO "[INIT] Phase 3: Process Management\n");

  /* Initialize scheduler */
  printk(KERN_INFO "  Initializing scheduler...\n");
  sched_init();

  /* Initialize process subsystem */
  printk(KERN_INFO "  Initializing process subsystem...\n");
  extern void process_init(void);
  process_init();

  /* ================================================================= */
  /* Phase 4: Filesystems */
  /* ================================================================= */

  printk(KERN_INFO "[INIT] Phase 4: Filesystems\n");

  /* Initialize Virtual Filesystem */
  printk(KERN_INFO "  Initializing VFS...\n");
  /* Initialize Virtual Filesystem */
  printk(KERN_INFO "  Initializing VFS...\n");
  vfs_init();

  /* Initialize and Register RamFS */
  printk(KERN_INFO "  Initializing RamFS...\n");
  extern int ramfs_init(void);
  ramfs_init();

  /* Mount root filesystem */
  printk(KERN_INFO "  Mounting root filesystem...\n");
  if (vfs_mount("ramfs", "/", "ramfs", 0, NULL) != 0) {
    panic("Failed to mount root filesystem!");
  }

  populate_seed_filesystem();

  /* Mount proc, sys, dev (placeholders) */
  printk(KERN_INFO "  Mounting procfs...\n");

  printk(KERN_INFO "  Mounting sysfs...\n");
  printk(KERN_INFO "  Mounting devfs...\n");

  /* ================================================================= */
  /* Phase 5: Device Drivers & GUI */
  /* ================================================================= */

  printk(KERN_INFO "[INIT] Phase 5: Device Drivers\n");

  /* Initialize framebuffer driver */
  printk(KERN_INFO "  Loading framebuffer driver...\n");
  extern int fb_init(void);
  extern void fb_get_info(uint32_t **buffer, uint32_t *width, uint32_t *height);
  fb_init();

  /* Initialize GUI windowing system */
  printk(KERN_INFO "  Initializing GUI...\n");
  extern int gui_init(uint32_t *framebuffer, uint32_t width, uint32_t height,
                      uint32_t pitch);
  extern struct window *gui_create_window(const char *title, int x, int y,
                                          int w, int h);
  extern void gui_compose(void);
  extern void gui_draw_cursor(void);

  uint32_t *fb_buffer;
  uint32_t fb_width, fb_height;
  fb_get_info(&fb_buffer, &fb_width, &fb_height);

  if (fb_buffer) {
    gui_init(fb_buffer, fb_width, fb_height, fb_width * 4);

    /* Create demo windows with working terminal */
    extern struct window *gui_create_file_manager(int x, int y);
    gui_create_window("Terminal", 50, 50, 400, 300);

    /* Create and set active terminal so keyboard input works */
    {
      extern struct terminal *term_create(int x, int y, int cols, int rows);
      extern void term_set_active(struct terminal * term);
      struct terminal *term = term_create(52, 80, 48, 15);
      if (term) {
        term_set_active(term);
      }
    }

    gui_create_file_manager(200, 100);

    /* Compose and display desktop */
    gui_compose();
    gui_draw_cursor();

    printk(KERN_INFO "  GUI desktop ready!\n");
  }

  /* Initialize PCI bus and detect devices (including Audio) */
  printk(KERN_INFO "  Initializing PCI bus...\n");
  extern void pci_init(void);
  extern void storage_init(void);
  storage_init();
  pci_init();

  /* Initialize GPU driver (virtio-gpu for QEMU acceleration) */
  printk(KERN_INFO "  Initializing GPU driver...\n");
  extern int intel_gfx_is_ready(void);
  extern int intel_gfx_has_framebuffer(void);
  extern const char *intel_gfx_get_name(void);
  extern int virtio_gpu_init(pci_device_t * pci);
  extern pci_device_t *pci_find_device(uint16_t vendor, uint16_t device);
  extern void gui_refresh_hardware_acceleration_policy(void);
  if (intel_gfx_is_ready()) {
    printk(KERN_INFO "  GPU: %s initialized%s\n", intel_gfx_get_name(),
           intel_gfx_has_framebuffer() ? " with framebuffer handoff" : "");
  }

  pci_device_t *gpu = pci_find_device(0x1AF4, 0x1050); /* virtio-gpu */
  if (gpu) {
    if (virtio_gpu_init(gpu) == 0) {
      printk(KERN_INFO "  GPU: virtio-gpu initialized with 3D acceleration\n");
    } else {
      printk(KERN_INFO "  GPU: virtio-gpu init failed\n");
    }
  } else if (!intel_gfx_is_ready()) {
    printk(KERN_INFO "  GPU: No virtio-gpu found (software rendering)\n");
  }
  gui_refresh_hardware_acceleration_policy();

  printk(KERN_INFO "  Loading keyboard driver...\n");
  printk(KERN_INFO "  Loading NVMe driver...\n");
  printk(KERN_INFO "  Loading USB driver...\n");
  printk(KERN_INFO "  Loading network driver...\n");
  extern void tcpip_init(void);
  extern int virtio_net_init(void);
  tcpip_init();
  virtio_net_init();

  /* ================================================================= */
  /* Phase 6: Enable Interrupts */
  /* ================================================================= */

  printk(KERN_INFO "[INIT] Enabling interrupts...\n");
  /* Enable interrupts */
  arch_irq_enable();

  printk(KERN_INFO "[INIT] Kernel initialization complete!\n\n");
}

/*
 * start_init_process - Start the first userspace process (PID 1)
 */

/* Global terminal pointer for keyboard callback */
static void *g_active_terminal = 0;

/* Keyboard callback wrapper */
/* Keyboard callback wrapper */
static void keyboard_handler(int key) {
  /* gui_handle_key_event is now called via gui_key_callback, not here */

      /* Send to the KAPI input buffer for non-windowed apps. */
  extern void kapi_sys_key_event(int key);
  kapi_sys_key_event(key);
}

static void start_init_process(void) {
  /* Create and start init process asynchronously */
  printk(KERN_INFO "Spawning /sbin/init...\n");

  extern int process_create(const char *path, int argc, char **argv);
  extern int process_start(int pid);

  char *argv[] = {"/sbin/init", NULL};
  int pid = process_create("/sbin/init", 1, argv);
  if (pid > 0) {
    process_start(pid);
    printk(KERN_INFO "Started init process (pid %d)\n", pid);
  } else {
    printk(KERN_ERR "Failed to start /sbin/init\n");
  }

  printk(KERN_INFO "System ready.\n\n");

  /* Set up input handling */
  extern int input_init(void);
  extern void input_poll(void);
  extern void input_set_key_callback(void (*callback)(int key));
  extern void gui_compose(void);
  extern void gui_draw_cursor(void);

  input_init();

  /* Connect keyboard input to terminal */
  input_set_key_callback(keyboard_handler);

  printk(KERN_INFO "GUI: Event loop started - type in terminal!\\n");

  /* Initial render */
  gui_compose();
  gui_draw_cursor();

  /* Main GUI event loop with proper flicker-free refresh */
  uint32_t frame = 0;
  int last_mx = 0, last_my = 0;
  int last_buttons = 0;
  int needs_redraw = 1; /* Initial draw */
  int cursor_only = 0;  /* Only cursor needs updating */

  /* Timer for periodic auto-refresh (33ms = 30 FPS for responsive UI) */
  uint64_t last_refresh = arch_timer_get_ms();
  const uint64_t REFRESH_MS = 33; /* 30 FPS - responsive mouse */

  while (1) {
    /* Poll virtio input devices (keyboard/mouse) - MUST call this! */
    input_poll();

    /* Poll for keyboard input from UART as well */
    extern int uart_getc_nonblock(void);
    extern void gui_handle_key_event(int key);
    int c = uart_getc_nonblock();
    if (c >= 0) {
      /* Route to focused window */
      gui_handle_key_event(c);
      needs_redraw = 1;
    }

    /* Poll input system (Keyboard & Mouse) */
    extern void input_poll(void);
    input_poll();

    /* Get mouse state (updated by input_poll) */
    extern void mouse_get_position(int *x, int *y);
    extern int mouse_get_buttons(void);
    extern void gui_handle_mouse_event(int x, int y, int buttons);

    int mx, my;
    mouse_get_position(&mx, &my);
    int mbuttons = mouse_get_buttons();

    /* Check if mouse changed */
    if (mx != last_mx || my != last_my || mbuttons != last_buttons) {
      /* Always call mouse event handler for hover support */
      gui_handle_mouse_event(mx, my, mbuttons);

      /* Always redraw on mouse move - cursor is now composited */
      needs_redraw = 1;

      last_mx = mx;
      last_my = my;
      last_buttons = mbuttons;
    }

    /* Periodic refresh for animations (5 FPS) */
    uint64_t now = arch_timer_get_ms();
    if (now - last_refresh >= REFRESH_MS) {
      last_refresh = now;
      needs_redraw = 1;
    }

    /* Redraw when needed - compose includes cursor drawing */
    if (needs_redraw) {
      gui_compose(); /* Cursor is drawn inside compose, before blit */
      needs_redraw = 0;
      cursor_only = 0;
    }

    frame++;
    (void)frame;

    /* Check if we should yield to let userspace run */
    /* If no input events processed, yield CPU */
    extern void process_schedule_from_irq(void); // Or just wait for IRQ?
    // User processes run preemptively via timer IRQ, so we just loop here
    // But we should yield to be nice if not rendering

    /* Short yield - allows input polling without slowing mouse */
    for (volatile int i = 0; i < 500; i++) {
    }
  }
}

/*
 * panic - Halt the system with an error message
 * @msg: Error message to display
 */
void panic(const char *msg) {
  /* Disable interrupts */
  arch_irq_disable();

  printk(KERN_EMERG "\n");
  printk(KERN_EMERG "============================================\n");
  printk(KERN_EMERG "KERNEL PANIC!\n");
  printk(KERN_EMERG "============================================\n");
  printk(KERN_EMERG "%s\n", msg);
  printk(KERN_EMERG "============================================\n");
  printk(KERN_EMERG "System halted.\n");

  /* Infinite loop */
  arch_halt();
}
