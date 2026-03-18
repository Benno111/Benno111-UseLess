#include "drivers/storage.h"
#include "fs/iso9660.h"
#include "fs/vfs.h"
#include "mm/kmalloc.h"
#include "printk.h"

#define ISO_BLOCK_SIZE 2048

typedef struct iso_copy_ctx {
  int disk_index;
  const char *dst_root;
} iso_copy_ctx_t;

static uint32_t iso_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static int iso_build_path(char *dst, int max, const char *root,
                          const char *name) {
  int idx = 0;
  if (!dst || max <= 0)
    return -1;
  if (root) {
    while (root[idx] && idx < max - 1) {
      dst[idx] = root[idx];
      idx++;
    }
  }
  if (idx > 0 && dst[idx - 1] != '/' && idx < max - 1)
    dst[idx++] = '/';
  for (int i = 0; name && name[i] && idx < max - 1; i++)
    dst[idx++] = name[i];
  dst[idx] = '\0';
  return 0;
}

static void iso_trim_name(char *dst, int max, const uint8_t *src, int len) {
  int idx = 0;
  if (!dst || max <= 0)
    return;
  if (len == 1 && src[0] == 0) {
    dst[0] = '\0';
    return;
  }
  if (len == 1 && src[0] == 1) {
    dst[0] = '.';
    dst[1] = '.';
    dst[2] = '\0';
    return;
  }
  for (int i = 0; i < len && idx < max - 1; i++) {
    if (src[i] == ';')
      break;
    dst[idx++] = (char)src[i];
  }
  if (idx > 0 && dst[idx - 1] == '.')
    idx--;
  dst[idx] = '\0';
}

static void iso_ensure_parent_dirs(const char *path) {
  char partial[256];
  int idx = 0;

  if (!path)
    return;
  for (int i = 0; path[i] && idx < (int)sizeof(partial) - 1; i++) {
    partial[idx++] = path[i];
    partial[idx] = '\0';
    if (i > 0 && path[i] == '/')
      vfs_mkdir(partial, 0755);
  }
}

static int iso_write_file(const char *path, const uint8_t *data, size_t size) {
  struct file *f;
  ssize_t written;

  iso_ensure_parent_dirs(path);
  vfs_unlink(path);
  f = vfs_open(path, O_CREAT | O_WRONLY, 0644);
  if (!f)
    return -1;
  written = vfs_write(f, (const char *)data, size);
  vfs_close(f);
  return (written < 0) ? -1 : 0;
}

static int iso_copy_file_from_extent(int disk_index, uint32_t extent,
                                     uint32_t file_size, const char *dst_path) {
  uint8_t *data;
  uint8_t sector[ISO_BLOCK_SIZE];
  uint32_t remaining = file_size;
  uint32_t lba = extent;
  uint32_t offset = 0;

  data = kmalloc(file_size ? file_size : 1, GFP_KERNEL);
  if (!data)
    return -1;

  while (remaining > 0) {
    uint32_t chunk = remaining > ISO_BLOCK_SIZE ? ISO_BLOCK_SIZE : remaining;
    if (storage_read_block(disk_index, lba++, sector, ISO_BLOCK_SIZE) != 0) {
      kfree(data);
      return -1;
    }
    for (uint32_t i = 0; i < chunk; i++)
      data[offset + i] = sector[i];
    offset += chunk;
    remaining -= chunk;
  }

  if (iso_write_file(dst_path, data, file_size) != 0) {
    kfree(data);
    return -1;
  }
  kfree(data);
  return 0;
}

static int iso_copy_dir_recursive(int disk_index, uint32_t extent, uint32_t size,
                                  const char *dst_root) {
  uint8_t *dir_data;
  uint8_t sector[ISO_BLOCK_SIZE];
  uint32_t sector_count = (size + ISO_BLOCK_SIZE - 1) / ISO_BLOCK_SIZE;
  uint32_t total_size = sector_count * ISO_BLOCK_SIZE;
  uint32_t pos = 0;

  dir_data = kmalloc(total_size ? total_size : ISO_BLOCK_SIZE, GFP_KERNEL);
  if (!dir_data)
    return -1;

  for (uint32_t i = 0; i < sector_count; i++) {
    if (storage_read_block(disk_index, extent + i, sector, ISO_BLOCK_SIZE) != 0) {
      kfree(dir_data);
      return -1;
    }
    for (uint32_t j = 0; j < ISO_BLOCK_SIZE; j++)
      dir_data[i * ISO_BLOCK_SIZE + j] = sector[j];
  }

  while (pos < total_size) {
    uint8_t record_len = dir_data[pos];
    char name[NAME_MAX + 1];
    char child_path[256];
    uint32_t child_extent;
    uint32_t child_size;
    uint8_t flags;

    if (record_len == 0) {
      pos = ((pos / ISO_BLOCK_SIZE) + 1) * ISO_BLOCK_SIZE;
      continue;
    }
    if (pos + record_len > total_size)
      break;

    child_extent = iso_le32(&dir_data[pos + 2]);
    child_size = iso_le32(&dir_data[pos + 10]);
    flags = dir_data[pos + 25];
    iso_trim_name(name, sizeof(name), &dir_data[pos + 33], dir_data[pos + 32]);

    if (name[0] != '\0' &&
        !(name[0] == '.' && name[1] == '\0') &&
        !(name[0] == '.' && name[1] == '.' && name[2] == '\0')) {
      iso_build_path(child_path, sizeof(child_path), dst_root, name);
      if (flags & 0x02) {
        vfs_mkdir(child_path, 0755);
        iso_copy_dir_recursive(disk_index, child_extent, child_size, child_path);
      } else {
        iso_copy_file_from_extent(disk_index, child_extent, child_size, child_path);
      }
    }

    pos += record_len;
  }

  kfree(dir_data);
  return 0;
}

int iso9660_copy_to_ramfs(const char *disk_location, const char *dst_root) {
  int disk_index;
  uint8_t pvd[ISO_BLOCK_SIZE];
  uint32_t root_extent;
  uint32_t root_size;

  if (!disk_location || !dst_root)
    return -1;

  disk_index = storage_get_disk_index_by_location(disk_location);
  if (disk_index < 0)
    return -1;
  if (storage_read_block(disk_index, 16, pvd, ISO_BLOCK_SIZE) != 0)
    return -1;
  if (pvd[0] != 1 || pvd[1] != 'C' || pvd[2] != 'D' || pvd[3] != '0' ||
      pvd[4] != '0' || pvd[5] != '1')
    return -1;

  root_extent = iso_le32(&pvd[158]);
  root_size = iso_le32(&pvd[166]);
  vfs_mkdir(dst_root, 0755);
  printk(KERN_INFO "ISO9660: Copying '%s' to '%s'\n", disk_location, dst_root);
  return iso_copy_dir_recursive(disk_index, root_extent, root_size, dst_root);
}
