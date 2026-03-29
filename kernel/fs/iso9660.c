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

typedef struct iso_remove_ctx {
  char root[256];
  int failed;
} iso_remove_ctx_t;

typedef struct iso_volume_info {
  uint32_t root_extent;
  uint32_t root_size;
  int joliet;
} iso_volume_info_t;

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

static int iso_is_joliet_descriptor(const uint8_t *desc) {
  if (!desc)
    return 0;
  if (desc[0] != 2)
    return 0;
  if (desc[88] != 0x25 || desc[89] != 0x2F)
    return 0;
  return desc[90] == 0x40 || desc[90] == 0x43 || desc[90] == 0x45;
}

static void iso_trim_name(char *dst, int max, const uint8_t *src, int len,
                          int joliet) {
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
  if (joliet) {
    for (int i = 0; i + 1 < len && idx < max - 1; i += 2) {
      uint8_t hi = src[i];
      uint8_t lo = src[i + 1];

      if (hi == 0 && lo == ';')
        break;
      if (hi == 0 && lo != 0) {
        dst[idx++] = (char)lo;
      } else if (hi == 0 && lo == 0) {
        break;
      } else {
        dst[idx++] = '_';
      }
    }
  } else {
    for (int i = 0; i < len && idx < max - 1; i++) {
      if (src[i] == ';')
        break;
      dst[idx++] = (char)src[i];
    }
  }
  if (idx > 0 && dst[idx - 1] == '.')
    idx--;
  dst[idx] = '\0';
}

static int iso_find_volume_info(int disk_index, iso_volume_info_t *info) {
  uint8_t desc[ISO_BLOCK_SIZE];
  int saw_primary = 0;

  if (!info)
    return -1;

  info->root_extent = 0;
  info->root_size = 0;
  info->joliet = 0;

  for (uint32_t lba = 16; lba < 64; lba++) {
    if (storage_read_block(disk_index, lba, desc, ISO_BLOCK_SIZE) != 0)
      return -1;
    if (desc[1] != 'C' || desc[2] != 'D' || desc[3] != '0' || desc[4] != '0' ||
        desc[5] != '1')
      return -1;
    if (desc[0] == 1 && !saw_primary) {
      info->root_extent = iso_le32(&desc[158]);
      info->root_size = iso_le32(&desc[166]);
      info->joliet = 0;
      saw_primary = 1;
    } else if (iso_is_joliet_descriptor(desc)) {
      info->root_extent = iso_le32(&desc[158]);
      info->root_size = iso_le32(&desc[166]);
      info->joliet = 1;
      return 0;
    } else if (desc[0] == 255) {
      return saw_primary ? 0 : -1;
    }
  }

  return saw_primary ? 0 : -1;
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

static int iso_remove_tree_callback(void *ctx, const char *name, int len,
                                    loff_t offset, ino_t ino, unsigned type) {
  iso_remove_ctx_t *rm = (iso_remove_ctx_t *)ctx;
  char path[256];
  struct file *dir;

  (void)offset;
  (void)ino;

  if (!rm || !name || len <= 0)
    return 0;
  if ((len == 1 && name[0] == '.') ||
      (len == 2 && name[0] == '.' && name[1] == '.'))
    return 0;
  if (iso_build_path(path, sizeof(path), rm->root, name) != 0)
    return 0;

  if (type == 4) {
    iso_remove_ctx_t child = {{0}, 0};
    int i = 0;
    while (path[i] && i < (int)sizeof(child.root) - 1) {
      child.root[i] = path[i];
      i++;
    }
    child.root[i] = '\0';
    dir = vfs_open(path, O_RDONLY, 0);
    if (dir) {
      vfs_readdir(dir, &child, iso_remove_tree_callback);
      vfs_close(dir);
    }
    if (child.failed)
      rm->failed = child.failed;
    if (vfs_rmdir(path) != 0)
      rm->failed = 1;
    return 0;
  }

  if (vfs_unlink(path) != 0)
    rm->failed = 1;
  return 0;
}

static int iso_clear_destination(const char *dst_root) {
  struct file *dir;
  iso_remove_ctx_t ctx = {{0}, 0};
  int i = 0;

  if (!dst_root)
    return -1;
  while (dst_root[i] && i < (int)sizeof(ctx.root) - 1) {
    ctx.root[i] = dst_root[i];
    i++;
  }
  ctx.root[i] = '\0';

  dir = vfs_open(dst_root, O_RDONLY, 0);
  if (!dir) {
    vfs_mkdir(dst_root, 0755);
    return 0;
  }
  vfs_readdir(dir, &ctx, iso_remove_tree_callback);
  vfs_close(dir);
  return ctx.failed ? -1 : 0;
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
                                  const char *dst_root, int joliet) {
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

  while (pos < size) {
    uint8_t record_len = dir_data[pos];
    char name[NAME_MAX + 1];
    char child_path[256];
    uint32_t child_extent;
    uint32_t child_size;
    uint8_t flags;
    uint8_t name_len;

    if (record_len == 0) {
      pos = ((pos / ISO_BLOCK_SIZE) + 1) * ISO_BLOCK_SIZE;
      continue;
    }
    if (record_len < 34) {
      printk(KERN_WARNING
             "ISO9660: invalid record length %u at offset %u in '%s'\n",
             record_len, pos, dst_root ? dst_root : "/");
      pos = ((pos / ISO_BLOCK_SIZE) + 1) * ISO_BLOCK_SIZE;
      continue;
    }
    if (pos + record_len > total_size)
      break;
    name_len = dir_data[pos + 32];
    if ((uint32_t)(33 + name_len) > record_len) {
      printk(KERN_WARNING
             "ISO9660: invalid name length %u at offset %u in '%s'\n",
             name_len, pos, dst_root ? dst_root : "/");
      pos = ((pos / ISO_BLOCK_SIZE) + 1) * ISO_BLOCK_SIZE;
      continue;
    }

    child_extent = iso_le32(&dir_data[pos + 2]);
    child_size = iso_le32(&dir_data[pos + 10]);
    flags = dir_data[pos + 25];
    iso_trim_name(name, sizeof(name), &dir_data[pos + 33], name_len, joliet);

    if (name[0] != '\0' &&
        !(name[0] == '.' && name[1] == '\0') &&
        !(name[0] == '.' && name[1] == '.' && name[2] == '\0')) {
      iso_build_path(child_path, sizeof(child_path), dst_root, name);
      if (flags & 0x02) {
        vfs_mkdir(child_path, 0755);
        if (iso_copy_dir_recursive(disk_index, child_extent, child_size,
                                   child_path, joliet) != 0) {
          kfree(dir_data);
          return -1;
        }
      } else {
        if (iso_copy_file_from_extent(disk_index, child_extent, child_size,
                                      child_path) != 0) {
          kfree(dir_data);
          return -1;
        }
      }
    }

    pos += record_len;
  }

  kfree(dir_data);
  return 0;
}

int iso9660_copy_to_ramfs(const char *disk_location, const char *dst_root) {
  int disk_index;
  iso_volume_info_t volume_info;

  if (!disk_location || !dst_root)
    return -1;

  disk_index = storage_get_disk_index_by_location(disk_location);
  if (disk_index < 0)
    return -1;
  if (iso_find_volume_info(disk_index, &volume_info) != 0)
    return -1;
  if (iso_clear_destination(dst_root) != 0)
    return -1;
  vfs_mkdir(dst_root, 0755);
  printk(KERN_INFO "ISO9660: Copying '%s' to '%s' using %s catalog\n",
         disk_location, dst_root, volume_info.joliet ? "Joliet" : "primary");
  return iso_copy_dir_recursive(disk_index, volume_info.root_extent,
                                volume_info.root_size, dst_root,
                                volume_info.joliet);
}
