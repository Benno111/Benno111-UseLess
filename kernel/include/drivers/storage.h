#ifndef DRIVERS_STORAGE_H
#define DRIVERS_STORAGE_H

#include "drivers/pci.h"
#include "types.h"

typedef enum {
  STORAGE_KIND_UNKNOWN = 0,
  STORAGE_KIND_IDE,
  STORAGE_KIND_AHCI,
  STORAGE_KIND_SATA,
  STORAGE_KIND_RAID,
  STORAGE_KIND_NVME,
  STORAGE_KIND_USB_MASS_STORAGE,
  STORAGE_KIND_APPLE_ANS
} storage_kind_t;

void storage_init(void);
void storage_register_pci_controller(pci_device_t *dev);
void storage_register_platform_controller(const char *name, storage_kind_t kind,
                                          const char *bus_name);

int storage_get_controller_count(void);
int storage_get_kind_count(storage_kind_t kind);
int storage_describe_controller(int index, char *buf, int max);
void storage_build_overview(char *buf, int max);
const char *storage_kind_name(storage_kind_t kind);

int storage_get_disk_count(void);
int storage_describe_disk(int index, char *buf, int max);
void storage_build_disk_overview(char *buf, int max);

#endif
