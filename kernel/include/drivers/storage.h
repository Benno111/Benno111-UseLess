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

typedef enum {
  STORAGE_PARTITION_UNKNOWN = 0,
  STORAGE_PARTITION_EFI,
  STORAGE_PARTITION_SYSTEM,
  STORAGE_PARTITION_DATA,
  STORAGE_PARTITION_SWAP
} storage_partition_kind_t;

typedef int (*storage_disk_read_fn_t)(uint64_t lba, uint32_t count,
                                      void *buffer, void *ctx);
typedef int (*storage_disk_write_fn_t)(uint64_t lba, uint32_t count,
                                       const void *buffer, void *ctx);

void storage_init(void);
void storage_register_pci_controller(pci_device_t *dev);
void storage_register_platform_controller(const char *name, storage_kind_t kind,
                                          const char *bus_name);
void storage_register_disk_device(const char *name, storage_kind_t kind,
                                  const char *location);
int storage_register_disk_backend(const char *location,
                                  storage_disk_read_fn_t read_fn,
                                  storage_disk_write_fn_t write_fn,
                                  void *ctx);
int storage_disk_supports_partition_writes(int disk_index);

int storage_get_controller_count(void);
int storage_get_kind_count(storage_kind_t kind);
int storage_describe_controller(int index, char *buf, int max);
void storage_build_overview(char *buf, int max);
const char *storage_kind_name(storage_kind_t kind);

int storage_get_disk_count(void);
int storage_describe_disk(int index, char *buf, int max);
void storage_build_disk_overview(char *buf, int max);
int storage_get_partition_count(int disk_index);
int storage_describe_partition(int disk_index, int partition_index, char *buf,
                               int max);
int storage_create_partition(int disk_index, storage_partition_kind_t kind,
                             uint32_t size_mib);
int storage_update_partition(int disk_index, int partition_index,
                             storage_partition_kind_t kind,
                             uint32_t size_mib);
int storage_delete_partition(int disk_index, int partition_index);
int storage_has_efi_partition(int disk_index);
int storage_ensure_install_partitions(int disk_index);
const char *storage_partition_kind_name(storage_partition_kind_t kind);

#endif
