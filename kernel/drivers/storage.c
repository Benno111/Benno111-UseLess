#include "drivers/storage.h"
#include "printk.h"

#define STORAGE_MAX_CONTROLLERS 16
#define STORAGE_MAX_DISKS 16

typedef struct {
  storage_kind_t kind;
  uint16_t vendor_id;
  uint16_t device_id;
  uint8_t bus;
  uint8_t slot;
  uint8_t func;
  char name[48];
  char bus_name[16];
} storage_controller_t;

typedef struct {
  storage_kind_t kind;
  uint8_t controller_index;
  uint8_t disk_index;
  char name[48];
  char location[24];
} storage_disk_t;

static storage_controller_t storage_controllers[STORAGE_MAX_CONTROLLERS];
static storage_disk_t storage_disks[STORAGE_MAX_DISKS];
static int storage_controller_count = 0;
static int storage_disk_count = 0;
static int storage_kind_counts[STORAGE_KIND_APPLE_ANS + 1];
static int storage_initialized = 0;

static void storage_copy_string(char *dst, const char *src, int max) {
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

static void storage_append_string(char *dst, int max, const char *src) {
  int idx = 0;
  if (!dst || !src || max <= 0)
    return;
  while (idx < max - 1 && dst[idx])
    idx++;
  for (int i = 0; src[i] && idx < max - 1; i++)
    dst[idx++] = src[i];
  dst[idx] = '\0';
}

static void storage_append_decimal(char *dst, int max, int value) {
  char digits[16];
  int count = 0;
  int idx = 0;

  if (!dst || max <= 0)
    return;
  while (idx < max - 1 && dst[idx])
    idx++;

  if (value == 0) {
    if (idx < max - 1) {
      dst[idx++] = '0';
      dst[idx] = '\0';
    }
    return;
  }

  if (value < 0 && idx < max - 1) {
    dst[idx++] = '-';
    value = -value;
  }

  while (value > 0 && count < (int)sizeof(digits)) {
    digits[count++] = (char)('0' + (value % 10));
    value /= 10;
  }

  while (count > 0 && idx < max - 1) {
    dst[idx++] = digits[--count];
  }
  dst[idx] = '\0';
}

static void storage_append_location(char *buf, int max, const char *prefix,
                                    int value) {
  storage_append_string(buf, max, prefix);
  storage_append_decimal(buf, max, value);
}

const char *storage_kind_name(storage_kind_t kind) {
  switch (kind) {
  case STORAGE_KIND_IDE:
    return "IDE/PATA";
  case STORAGE_KIND_AHCI:
    return "AHCI SATA";
  case STORAGE_KIND_SATA:
    return "SATA";
  case STORAGE_KIND_RAID:
    return "RAID";
  case STORAGE_KIND_NVME:
    return "NVMe";
  case STORAGE_KIND_USB_MASS_STORAGE:
    return "USB Mass Storage";
  case STORAGE_KIND_APPLE_ANS:
    return "Apple ANS NVMe";
  default:
    return "Unknown Storage";
  }
}

static storage_kind_t storage_classify_pci(const pci_device_t *dev) {
  if (!dev)
    return STORAGE_KIND_UNKNOWN;

  if (dev->class_code != 0x01)
    return STORAGE_KIND_UNKNOWN;

  if (dev->subclass == 0x01)
    return STORAGE_KIND_IDE;
  if (dev->subclass == 0x04)
    return STORAGE_KIND_RAID;
  if (dev->subclass == 0x06) {
    if (dev->prog_if == 0x01)
      return STORAGE_KIND_AHCI;
    return STORAGE_KIND_SATA;
  }
  if (dev->subclass == 0x08)
    return STORAGE_KIND_NVME;

  return STORAGE_KIND_UNKNOWN;
}

static int storage_controller_exists(storage_kind_t kind, uint8_t bus,
                                     uint8_t slot, uint8_t func,
                                     const char *name) {
  for (int i = 0; i < storage_controller_count; i++) {
    storage_controller_t *ctrl = &storage_controllers[i];
    if (name && ctrl->bus_name[0] && ctrl->bus == 0xFF && ctrl->slot == 0xFF &&
        ctrl->func == 0xFF) {
      int j = 0;
      while (name[j] && ctrl->name[j] && name[j] == ctrl->name[j])
        j++;
      if (name[j] == '\0' && ctrl->name[j] == '\0')
        return 1;
    }
    if (ctrl->kind == kind && ctrl->bus == bus && ctrl->slot == slot &&
        ctrl->func == func) {
      return 1;
    }
  }
  return 0;
}

static void storage_record_controller(storage_kind_t kind, uint16_t vendor,
                                      uint16_t device, uint8_t bus,
                                      uint8_t slot, uint8_t func,
                                      const char *name, const char *bus_name) {
  storage_controller_t *ctrl;

  if (kind <= STORAGE_KIND_UNKNOWN || kind > STORAGE_KIND_APPLE_ANS)
    return;
  if (storage_controller_count >= STORAGE_MAX_CONTROLLERS)
    return;
  if (storage_controller_exists(kind, bus, slot, func, name))
    return;

  ctrl = &storage_controllers[storage_controller_count++];
  ctrl->kind = kind;
  ctrl->vendor_id = vendor;
  ctrl->device_id = device;
  ctrl->bus = bus;
  ctrl->slot = slot;
  ctrl->func = func;
  storage_copy_string(ctrl->name, name ? name : storage_kind_name(kind),
                      sizeof(ctrl->name));
  storage_copy_string(ctrl->bus_name, bus_name ? bus_name : "pci",
                      sizeof(ctrl->bus_name));
  storage_kind_counts[kind]++;

  printk(KERN_INFO "STORAGE: Registered %s controller via %s\n", ctrl->name,
         ctrl->bus_name);
}

static int storage_disk_exists(const char *name, const char *location) {
  for (int i = 0; i < storage_disk_count; i++) {
    int j = 0;
    while (name[j] && storage_disks[i].name[j] &&
           name[j] == storage_disks[i].name[j]) {
      j++;
    }
    if (name[j] != '\0' || storage_disks[i].name[j] != '\0')
      continue;

    j = 0;
    while (location[j] && storage_disks[i].location[j] &&
           location[j] == storage_disks[i].location[j]) {
      j++;
    }
    if (location[j] == '\0' && storage_disks[i].location[j] == '\0')
      return 1;
  }
  return 0;
}

static void storage_register_disk(storage_kind_t kind, int controller_index,
                                  int disk_index, const char *name,
                                  const char *location) {
  storage_disk_t *disk;

  if (!name || !location)
    return;
  if (storage_disk_count >= STORAGE_MAX_DISKS)
    return;
  if (storage_disk_exists(name, location))
    return;

  disk = &storage_disks[storage_disk_count++];
  disk->kind = kind;
  disk->controller_index = (uint8_t)controller_index;
  disk->disk_index = (uint8_t)disk_index;
  storage_copy_string(disk->name, name, sizeof(disk->name));
  storage_copy_string(disk->location, location, sizeof(disk->location));

  printk(KERN_INFO "STORAGE: Registered disk %s at %s\n", disk->name,
         disk->location);
}

static void storage_seed_disks_for_controller(int controller_index) {
  storage_controller_t *ctrl;
  char disk_name[48];
  char location[24];

  if (controller_index < 0 || controller_index >= storage_controller_count)
    return;

  ctrl = &storage_controllers[controller_index];
  disk_name[0] = '\0';
  location[0] = '\0';

  switch (ctrl->kind) {
  case STORAGE_KIND_IDE:
    storage_copy_string(disk_name, "IDE Hard Disk", sizeof(disk_name));
    storage_append_location(location, sizeof(location), "ata", storage_disk_count);
    storage_register_disk(ctrl->kind, controller_index, 0, disk_name, location);
    storage_copy_string(disk_name, "IDE Hard Disk", sizeof(disk_name));
    location[0] = '\0';
    storage_append_location(location, sizeof(location), "ata", storage_disk_count);
    storage_register_disk(ctrl->kind, controller_index, 1, disk_name, location);
    break;
  case STORAGE_KIND_AHCI:
  case STORAGE_KIND_SATA:
    storage_copy_string(disk_name, "SATA Hard Disk", sizeof(disk_name));
    storage_append_location(location, sizeof(location), "sd", storage_disk_count);
    storage_register_disk(ctrl->kind, controller_index, 0, disk_name, location);
    break;
  case STORAGE_KIND_NVME:
  case STORAGE_KIND_APPLE_ANS:
    storage_copy_string(disk_name, "NVMe Disk", sizeof(disk_name));
    storage_append_location(location, sizeof(location), "nvme", storage_disk_count);
    storage_register_disk(ctrl->kind, controller_index, 0, disk_name, location);
    break;
  case STORAGE_KIND_USB_MASS_STORAGE:
    storage_copy_string(disk_name, "USB Hard Disk", sizeof(disk_name));
    storage_append_location(location, sizeof(location), "usb", storage_disk_count);
    storage_register_disk(ctrl->kind, controller_index, 0, disk_name, location);
    break;
  case STORAGE_KIND_RAID:
    storage_copy_string(disk_name, "RAID Volume", sizeof(disk_name));
    storage_append_location(location, sizeof(location), "md", storage_disk_count);
    storage_register_disk(ctrl->kind, controller_index, 0, disk_name, location);
    break;
  default:
    break;
  }
}

void storage_init(void) {
  if (storage_initialized)
    return;

  storage_initialized = 1;
  storage_controller_count = 0;
  storage_disk_count = 0;
  for (int i = 0; i <= STORAGE_KIND_APPLE_ANS; i++)
    storage_kind_counts[i] = 0;

#ifdef ARCH_ARM64
  extern int ans_nvme_init(void);
  if (ans_nvme_init() == 0) {
    storage_register_platform_controller("Apple ANS NVMe",
                                         STORAGE_KIND_APPLE_ANS, "platform");
  }
#endif
}

void storage_register_pci_controller(pci_device_t *dev) {
  storage_kind_t kind;
  const char *name;

  if (!storage_initialized)
    storage_init();
  if (!dev)
    return;

  kind = storage_classify_pci(dev);
  if (kind == STORAGE_KIND_UNKNOWN)
    return;

  name = storage_kind_name(kind);
  storage_record_controller(kind, dev->vendor_id, dev->device_id, dev->bus,
                            dev->slot, dev->func, name, "pci");
  storage_seed_disks_for_controller(storage_controller_count - 1);
}

void storage_register_platform_controller(const char *name, storage_kind_t kind,
                                          const char *bus_name) {
  if (!storage_initialized)
    storage_init();

  storage_record_controller(kind, 0, 0, 0xFF, 0xFF, 0xFF, name, bus_name);
  storage_seed_disks_for_controller(storage_controller_count - 1);
}

int storage_get_controller_count(void) { return storage_controller_count; }

int storage_get_disk_count(void) { return storage_disk_count; }

int storage_get_kind_count(storage_kind_t kind) {
  if (kind <= STORAGE_KIND_UNKNOWN || kind > STORAGE_KIND_APPLE_ANS)
    return 0;
  return storage_kind_counts[kind];
}

int storage_describe_controller(int index, char *buf, int max) {
  storage_controller_t *ctrl;

  if (!buf || max <= 0 || index < 0 || index >= storage_controller_count)
    return -1;

  ctrl = &storage_controllers[index];
  buf[0] = '\0';
  storage_append_string(buf, max, ctrl->name);
  storage_append_string(buf, max, " [");
  storage_append_string(buf, max, ctrl->bus_name);
  if (ctrl->bus != 0xFF) {
    storage_append_string(buf, max, " ");
    storage_append_decimal(buf, max, ctrl->bus);
    storage_append_string(buf, max, ":");
    storage_append_decimal(buf, max, ctrl->slot);
    storage_append_string(buf, max, ".");
    storage_append_decimal(buf, max, ctrl->func);
  }
  storage_append_string(buf, max, "]");
  return 0;
}

void storage_build_overview(char *buf, int max) {
  int total;

  if (!buf || max <= 0)
    return;

  buf[0] = '\0';
  total = storage_controller_count;
  if (total == 0) {
    storage_append_string(buf, max, "No storage controllers detected");
    return;
  }

  storage_append_decimal(buf, max, total);
  storage_append_string(buf, max, " controller");
  if (total != 1)
    storage_append_string(buf, max, "s");

  if (storage_get_kind_count(STORAGE_KIND_NVME) > 0 ||
      storage_get_kind_count(STORAGE_KIND_APPLE_ANS) > 0) {
    storage_append_string(buf, max, "  NVMe:");
    storage_append_decimal(buf, max,
                           storage_get_kind_count(STORAGE_KIND_NVME) +
                               storage_get_kind_count(STORAGE_KIND_APPLE_ANS));
  }
  if (storage_get_kind_count(STORAGE_KIND_AHCI) > 0 ||
      storage_get_kind_count(STORAGE_KIND_SATA) > 0) {
    storage_append_string(buf, max, "  SATA:");
    storage_append_decimal(buf, max,
                           storage_get_kind_count(STORAGE_KIND_AHCI) +
                               storage_get_kind_count(STORAGE_KIND_SATA));
  }
  if (storage_get_kind_count(STORAGE_KIND_IDE) > 0) {
    storage_append_string(buf, max, "  IDE:");
    storage_append_decimal(buf, max, storage_get_kind_count(STORAGE_KIND_IDE));
  }
  if (storage_get_kind_count(STORAGE_KIND_RAID) > 0) {
    storage_append_string(buf, max, "  RAID:");
    storage_append_decimal(buf, max, storage_get_kind_count(STORAGE_KIND_RAID));
  }
}

int storage_describe_disk(int index, char *buf, int max) {
  storage_disk_t *disk;

  if (!buf || max <= 0 || index < 0 || index >= storage_disk_count)
    return -1;

  disk = &storage_disks[index];
  buf[0] = '\0';
  storage_append_string(buf, max, disk->name);
  storage_append_string(buf, max, " [");
  storage_append_string(buf, max, disk->location);
  storage_append_string(buf, max, "]");
  return 0;
}

void storage_build_disk_overview(char *buf, int max) {
  if (!buf || max <= 0)
    return;

  buf[0] = '\0';
  if (storage_disk_count == 0) {
    storage_append_string(buf, max, "No hard disks registered");
    return;
  }

  storage_append_decimal(buf, max, storage_disk_count);
  storage_append_string(buf, max, " disk");
  if (storage_disk_count != 1)
    storage_append_string(buf, max, "s");
  storage_append_string(buf, max, " ready");
}
