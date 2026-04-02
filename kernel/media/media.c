/*
 * OS8 - Media helpers (JPEG/MP3 decoding)
 */

#include "media/media.h"
#include "fs/vfs.h"
#include "mm/kmalloc.h"
#include "printk.h"
#include "types.h"
#include "sandbox/sandbox.h"

static const char *media_persistent_roots[] = {"/Persist", "/persist", "/disk",
                                               "/mnt/disk"};
#define MEDIA_PERSISTENT_ROOT_COUNT                                           \
  ((int)(sizeof(media_persistent_roots) / sizeof(media_persistent_roots[0])))

static int media_strlen(const char *str) {
  int len = 0;
  while (str && str[len])
    len++;
  return len;
}

static int media_try_build_persistent_path(const char *path, char *out,
                                           size_t out_size) {
  for (int root_idx = 0; root_idx < MEDIA_PERSISTENT_ROOT_COUNT; root_idx++) {
    const char *root = media_persistent_roots[root_idx];
    struct file *dir = vfs_open(root, O_RDONLY, 0);
    if (!dir)
      continue;
    vfs_close(dir);

    int idx = 0;
    for (int i = 0; root[i] && idx < (int)out_size - 1; i++)
      out[idx++] = root[i];
    for (int i = 0; path[i] && idx < (int)out_size - 1; i++)
      out[idx++] = path[i];
    out[idx] = '\0';
    return 0;
  }
  return -ENOENT;
}

static void media_ensure_parent_dirs(const char *path) {
  char partial[256];
  int idx = 0;

  if (!path)
    return;

  for (int i = 0; path[i] && idx < (int)sizeof(partial) - 1; i++) {
    partial[idx++] = path[i];
    partial[idx] = '\0';
    if (i > 0 && path[i] == '/') {
      vfs_mkdir(partial, 0755);
    }
  }
}

static int media_write_raw_file(const char *path, const uint8_t *data,
                                size_t size) {
  struct file *f;
  ssize_t written;

  if (!path || (!data && size > 0))
    return -EINVAL;

  media_ensure_parent_dirs(path);
  vfs_unlink(path);
  f = vfs_open(path, O_CREAT | O_WRONLY, 0644);
  if (!f)
    return -ENOENT;
  written = vfs_write(f, (const char *)data, size);
  vfs_close(f);
  return (written < 0) ? (int)written : 0;
}

/* --------------------------------------------------------------------- */
/* File loading                                                          */
/* --------------------------------------------------------------------- */

static int media_load_file_from_exact_path(const char *path, uint8_t **out_data,
                                           size_t *out_size) {
  struct file *f;
  struct inode *inode;
  uint8_t *buf;
  size_t size;
  size_t total_read = 0;

  if (!path || !out_data || !out_size)
    return -EINVAL;

  f = vfs_open(path, O_RDONLY, 0);
  if (!f)
    return -ENOENT;

  inode = f->f_dentry ? f->f_dentry->d_inode : NULL;
  if (!inode || inode->i_size <= 0) {
    vfs_close(f);
    return -EINVAL;
  }

  size = (size_t)inode->i_size;
  buf = (uint8_t *)kmalloc(size, GFP_KERNEL);
  if (!buf) {
    vfs_close(f);
    return -ENOMEM;
  }

  while (total_read < size) {
    ssize_t read_bytes = vfs_read(f, (char *)buf + total_read, size - total_read);
    if (read_bytes < 0) {
      vfs_close(f);
      kfree(buf);
      return (int)read_bytes;
    }
    if (read_bytes == 0)
      break;
    total_read += (size_t)read_bytes;
  }
  vfs_close(f);

  if (total_read != size) {
    printk(KERN_ERR "MEDIA: short read for '%s' (%u/%u bytes)\n", path,
           (unsigned)total_read, (unsigned)size);
    kfree(buf);
    return -EIO;
  }

  *out_data = buf;
  *out_size = total_read;
  return 0;
}

int media_load_file(const char *path, uint8_t **out_data, size_t *out_size) {
  char persistent_path[256];
  int ret;

  if (!path || !out_data || !out_size)
    return -EINVAL;

  if (media_try_build_persistent_path(path, persistent_path,
                                      sizeof(persistent_path)) == 0) {
    ret = media_load_file_from_exact_path(persistent_path, out_data, out_size);
    if (ret == 0)
      return 0;
  }

  return media_load_file_from_exact_path(path, out_data, out_size);
}

void media_free_file(uint8_t *data) {
  if (data)
    kfree(data);
}

int media_install_file(const char *path, const uint8_t *data, size_t size) {
  char persistent_path[256];
  int ret = media_write_raw_file(path, data, size);
  if (ret < 0)
    return ret;

  if (media_try_build_persistent_path(path, persistent_path,
                                      sizeof(persistent_path)) == 0) {
    media_write_raw_file(persistent_path, data, size);
  }

  return 0;
}

int media_install_text_file(const char *path, const char *content) {
  if (!content)
    return -EINVAL;
  return media_install_file(path, (const uint8_t *)content,
                            (size_t)media_strlen(content));
}

/* --------------------------------------------------------------------- */
/* JPEG decoding (picojpeg)                                               */
/* --------------------------------------------------------------------- */

#include "picojpeg.h"

typedef struct {
  const uint8_t *data;
  size_t size;
  size_t offset;
} jpeg_mem_t;

static unsigned char jpeg_need_bytes(unsigned char *pBuf,
                                     unsigned char buf_size,
                                     unsigned char *pBytes_actually_read,
                                     void *pCallback_data) {
  jpeg_mem_t *mem = (jpeg_mem_t *)pCallback_data;
  if (!mem || mem->offset >= mem->size) {
    *pBytes_actually_read = 0;
    return 0;
  }

  size_t remaining = mem->size - mem->offset;
  size_t to_copy = remaining < buf_size ? remaining : buf_size;
  for (size_t i = 0; i < to_copy; i++) {
    pBuf[i] = mem->data[mem->offset + i];
  }

  mem->offset += to_copy;
  *pBytes_actually_read = (unsigned char)to_copy;
  return 0;
}

int media_decode_jpeg_buffer(const uint8_t *data, size_t size,
                             media_image_t *out, uint32_t *buffer,
                             size_t buffer_size) {
  if (!data || !size || !out)
    return -EINVAL;

  jpeg_mem_t mem = {data, size, 0};
  pjpeg_image_info_t info;
  unsigned char status = pjpeg_decode_init(&info, jpeg_need_bytes, &mem, 0);
  if (status) {
    printk(KERN_ERR "JPEG: decode_init failed (%u)\n", status);
    return -EINVAL;
  }

  if (info.m_width <= 0 || info.m_height <= 0)
    return -EINVAL;

  /* Check for integer overflow in pixel count calculation */
  if ((size_t)info.m_width > SIZE_MAX / (size_t)info.m_height) {
    printk(KERN_ERR "JPEG: dimensions too large (integer overflow)\n");
    return -EINVAL;
  }

  size_t pixel_count = (size_t)info.m_width * (size_t)info.m_height;

  /* Prevent excessively large allocations (64MB max image - 4K support) */
  if (pixel_count > 16 * 1024 * 1024) {
    printk(KERN_ERR "JPEG: image too large (%zu pixels)\n", pixel_count);
    return -EINVAL;
  }

  size_t required_bytes = pixel_count * sizeof(uint32_t);

  uint32_t *pixels = NULL;
  bool allocated = false;

  if (buffer) {
    if (buffer_size < required_bytes) {
      printk(KERN_ERR "JPEG: buffer too small (need %d, got %d)\n",
             (int)required_bytes, (int)buffer_size);
      return -ENOMEM;
    }
    pixels = buffer;
  } else {
    pixels = (uint32_t *)kmalloc(required_bytes, GFP_KERNEL);
    if (!pixels)
      return -ENOMEM;
    allocated = true;
  }

  int mcu_x = 0;
  int mcu_y = 0;
  while (1) {
    status = pjpeg_decode_mcu();
    if (status) {
      if (status == PJPG_NO_MORE_BLOCKS)
        break;
      printk(KERN_ERR "JPEG: decode_mcu failed (%u)\n", status);
      if (allocated)
        kfree(pixels);
      return -EINVAL;
    }

    int mcu_width = info.m_MCUWidth;
    int mcu_height = info.m_MCUHeight;
    int blocks_per_row = mcu_width / 8;

    for (int y = 0; y < mcu_height; y++) {
      int yy = mcu_y * mcu_height + y;
      if (yy >= info.m_height)
        continue;
      for (int x = 0; x < mcu_width; x++) {
        int xx = mcu_x * mcu_width + x;
        if (xx >= info.m_width)
          continue;

        int block_x = x / 8;
        int block_y = y / 8;
        int block_index = block_y * blocks_per_row + block_x;
        int block_offset = block_index * 64;
        int pixel_offset = block_offset + (y % 8) * 8 + (x % 8);

        uint8_t r = info.m_pMCUBufR[pixel_offset];
        uint8_t g = info.m_pMCUBufG ? info.m_pMCUBufG[pixel_offset] : r;
        uint8_t b = info.m_pMCUBufB ? info.m_pMCUBufB[pixel_offset] : r;
        pixels[yy * info.m_width + xx] =
            ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
      }
    }

    mcu_x++;
    if (mcu_x == info.m_MCUSPerRow) {
      mcu_x = 0;
      mcu_y++;
    }
  }

  out->width = (uint32_t)info.m_width;
  out->height = (uint32_t)info.m_height;
  out->pixels = pixels;
  return 0;
}

int media_decode_jpeg(const uint8_t *data, size_t size, media_image_t *out) {
  return media_decode_jpeg_buffer(data, size, out, NULL, 0);
}

void media_free_image(media_image_t *image) {
  if (!image)
    return;
  if (image->pixels) {
    kfree(image->pixels);
    image->pixels = NULL;
  }
  image->width = 0;
  image->height = 0;
}

/* --------------------------------------------------------------------- */
/* SVG decode (embedded data URI image extraction)                        */
/* --------------------------------------------------------------------- */

static int media_bytes_starts_with(const uint8_t *data, size_t size,
                                   size_t at, const char *needle) {
  size_t n = 0;
  if (!data || !needle || at >= size)
    return 0;
  while (needle[n]) {
    if (at + n >= size || data[at + n] != (uint8_t)needle[n])
      return 0;
    n++;
  }
  return 1;
}

static int media_find_bytes(const uint8_t *data, size_t size, const char *needle,
                            size_t *out_pos) {
  size_t needle_len = 0;
  if (!data || !needle || !out_pos)
    return -EINVAL;

  while (needle[needle_len])
    needle_len++;
  if (needle_len == 0 || needle_len > size)
    return -EINVAL;

  for (size_t i = 0; i + needle_len <= size; i++) {
    if (media_bytes_starts_with(data, size, i, needle)) {
      *out_pos = i;
      return 0;
    }
  }
  return -ENOENT;
}

static int media_base64_value(uint8_t ch) {
  if (ch >= 'A' && ch <= 'Z')
    return (int)(ch - 'A');
  if (ch >= 'a' && ch <= 'z')
    return (int)(ch - 'a') + 26;
  if (ch >= '0' && ch <= '9')
    return (int)(ch - '0') + 52;
  if (ch == '+')
    return 62;
  if (ch == '/')
    return 63;
  return -1;
}

static int media_decode_base64(const uint8_t *src, size_t src_len, uint8_t **out,
                               size_t *out_len) {
  uint8_t *dst;
  size_t cap;
  size_t wr = 0;
  uint32_t acc = 0;
  int acc_bits = 0;
  int saw_padding = 0;

  if (!src || !out || !out_len)
    return -EINVAL;

  cap = (src_len / 4) * 3 + 3;
  dst = (uint8_t *)kmalloc(cap, GFP_KERNEL);
  if (!dst)
    return -ENOMEM;

  for (size_t i = 0; i < src_len; i++) {
    uint8_t ch = src[i];
    int val;

    if (ch == '=')
      saw_padding = 1;
    if (ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ')
      continue;
    if (ch == '=') {
      break;
    }

    val = media_base64_value(ch);
    if (val < 0 || saw_padding) {
      kfree(dst);
      return -EINVAL;
    }

    acc = (acc << 6) | (uint32_t)val;
    acc_bits += 6;

    while (acc_bits >= 8) {
      acc_bits -= 8;
      if (wr >= cap) {
        kfree(dst);
        return -EOVERFLOW;
      }
      dst[wr++] = (uint8_t)((acc >> acc_bits) & 0xFF);
    }
  }

  *out = dst;
  *out_len = wr;
  return 0;
}

int media_decode_svg(const uint8_t *data, size_t size, media_image_t *out) {
  static const char *k_svg_png = "data:image/png;base64,";
  static const char *k_svg_jpg = "data:image/jpeg;base64,";
  static const char *k_svg_jpg_alt = "data:image/jpg;base64,";
  const char *uri = NULL;
  size_t uri_pos = 0;
  size_t base64_start = 0;
  size_t base64_end = 0;
  uint8_t *decoded = NULL;
  size_t decoded_len = 0;
  int ret;

  if (!data || !size || !out)
    return -EINVAL;

  if (media_find_bytes(data, size, k_svg_png, &uri_pos) == 0) {
    uri = k_svg_png;
  } else if (media_find_bytes(data, size, k_svg_jpg, &uri_pos) == 0) {
    uri = k_svg_jpg;
  } else if (media_find_bytes(data, size, k_svg_jpg_alt, &uri_pos) == 0) {
    uri = k_svg_jpg_alt;
  } else {
    return -ENOENT;
  }

  while (uri[base64_start])
    base64_start++;
  base64_start += uri_pos;
  if (base64_start >= size)
    return -EINVAL;

  base64_end = base64_start;
  while (base64_end < size) {
    uint8_t ch = data[base64_end];
    if (ch == '"' || ch == '\'' || ch == '<' || ch == '>' || ch == ' ')
      break;
    base64_end++;
  }

  if (base64_end <= base64_start)
    return -EINVAL;

  ret = media_decode_base64(data + base64_start, base64_end - base64_start,
                            &decoded, &decoded_len);
  if (ret != 0)
    return ret;

  if (uri == k_svg_png) {
    ret = media_decode_png(decoded, decoded_len, out);
  } else {
    ret = media_decode_jpeg(decoded, decoded_len, out);
  }

  kfree(decoded);
  return ret;
}

/* --------------------------------------------------------------------- */
/* MP3 decoding (minimp3)                                                 */
/* --------------------------------------------------------------------- */

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#define MINIMP3_NO_SIMD
#define malloc(sz) kmalloc((sz))
#define free(ptr) kfree((ptr))
#define realloc(ptr, sz) krealloc((ptr), (sz), 0)
#include "minimp3_ex.h"
#undef malloc
#undef free
#undef realloc

int media_decode_mp3(const uint8_t *data, size_t size, media_audio_t *out) {
  if (!data || !size || !out)
    return -EINVAL;

  mp3dec_t dec;
  mp3dec_file_info_t info;
  int ret = mp3dec_load_buf(&dec, data, size, &info, NULL, NULL);
  if (ret < 0 || !info.buffer || !info.samples) {
    return -EINVAL;
  }

  out->samples = (int16_t *)info.buffer;
  out->sample_count =
      (uint32_t)(info.samples / (info.channels ? info.channels : 1));
  out->sample_rate = (uint32_t)info.hz;
  out->channels = (uint8_t)info.channels;
  return 0;
}

void media_free_audio(media_audio_t *audio) {
  if (!audio)
    return;
  if (audio->samples) {
    kfree(audio->samples);
    audio->samples = NULL;
  }
  audio->sample_count = 0;
  audio->sample_rate = 0;
  audio->channels = 0;
}

/* --------------------------------------------------------------------- */
/* PNG decoding (tPNG)                                                    */
/* --------------------------------------------------------------------- */

#include "tpng.h"

int media_decode_png(const uint8_t *data, size_t size, media_image_t *out) {
  if (!data || !size || !out)
    return -EINVAL;

  uint32_t width = 0, height = 0;
  uint8_t *rgba = tpng_decode(data, (uint32_t)size, &width, &height);

  if (!rgba || width == 0 || height == 0) {
    printk(KERN_ERR "PNG: decode failed\n");
    if (rgba)
      kfree(rgba);
    return -EINVAL;
  }

  /* Check for excessively large images (16M pixels max, same as JPEG) */
  size_t pixel_count = (size_t)width * (size_t)height;
  if (pixel_count > 16 * 1024 * 1024) {
    printk(KERN_ERR "PNG: image too large (%zu pixels)\n", pixel_count);
    kfree(rgba);
    return -EINVAL;
  }

  /* Convert RGBA (uint8_t*) to 0xAARRGGBB so PNG transparency is preserved. */
  uint32_t *pixels =
      (uint32_t *)kmalloc(pixel_count * sizeof(uint32_t), GFP_KERNEL);
  if (!pixels) {
    kfree(rgba);
    return -ENOMEM;
  }

  for (size_t i = 0; i < pixel_count; i++) {
    uint8_t r = rgba[i * 4 + 0];
    uint8_t g = rgba[i * 4 + 1];
    uint8_t b = rgba[i * 4 + 2];
    uint8_t a = rgba[i * 4 + 3];
    pixels[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                ((uint32_t)g << 8) | b;
  }

  kfree(rgba);

  out->width = width;
  out->height = height;
  out->pixels = pixels;
  return 0;
}
