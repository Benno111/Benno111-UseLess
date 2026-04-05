/*
 * OS8 - Intel Integrated Graphics Driver
 *
 * Conservative first-stage Intel graphics support:
 * - Detect Intel display-class PCI devices
 * - Enable PCI memory/bus mastering
 * - Map the MMIO BAR for later register work
 * - Reuse the bootloader-provided framebuffer for display output
 */

#include "drivers/intel_gfx.h"
#include "mm/vmm.h"
#include "printk.h"

#define INTEL_GFX_MMIO_MAP_SIZE 0x200000

typedef struct {
  bool initialized;
  bool probe_attempted;
  bool init_failed;
  bool has_framebuffer;
  bool supports_gpu_rendering;
  bool mmio_mapped;
  uint16_t device_id;
  uint64_t mmio_base;
  uint64_t aperture_base;
  uint32_t irq;
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  volatile uint8_t *mmio;
} intel_gfx_state_t;

static intel_gfx_state_t intel_gfx_state = {0};

static int intel_gfx_is_display_device(const pci_device_t *pci_dev) {
  if (!pci_dev)
    return 0;
  if (pci_dev->vendor_id != INTEL_GPU_VENDOR_ID)
    return 0;
  if (pci_dev->class_code != 0x03)
    return 0;
  return 1;
}

static int intel_gfx_is_ivybridge_device(uint16_t device_id) {
  switch (device_id) {
  case 0x0152:
  case 0x0156:
  case 0x015A:
  case 0x0162:
  case 0x0166:
  case 0x016A:
    return 1;
  default:
    return 0;
  }
}

static int intel_gfx_is_haswell_device(uint16_t device_id) {
  switch (device_id) {
  case 0x0402:
  case 0x0406:
  case 0x040A:
  case 0x0412:
  case 0x0416:
  case 0x041A:
  case 0x0A02:
  case 0x0A06:
  case 0x0A0A:
  case 0x0A12:
  case 0x0A16:
  case 0x0A1A:
  case 0x0D22:
  case 0x0D26:
  case 0x0D2A:
    return 1;
  default:
    return 0;
  }
}

static int intel_gfx_is_2012_2013_supported(uint16_t device_id) {
  return intel_gfx_is_ivybridge_device(device_id) ||
         intel_gfx_is_haswell_device(device_id);
}

static const char *intel_gfx_detect_name(uint16_t device_id) {
  switch (device_id) {
  case 0x0152:
  case 0x0156:
  case 0x015A:
  case 0x0162:
  case 0x0166:
  case 0x016A:
    return "Intel HD Graphics 4000";
  case 0x0402:
  case 0x0406:
  case 0x040A:
  case 0x0412:
  case 0x0416:
  case 0x041A:
  case 0x0A02:
  case 0x0A06:
  case 0x0A0A:
  case 0x0A12:
  case 0x0A16:
  case 0x0A1A:
  case 0x0D22:
  case 0x0D26:
  case 0x0D2A:
    return "Intel HD Graphics 4600";
  case 0x5916:
  case 0x591B:
  case 0x5912:
    return "Intel HD Graphics 630";
  case 0x3E92:
  case 0x3E9B:
  case 0x3EA5:
    return "Intel UHD Graphics 630";
  case 0x9BC5:
  case 0x9BC8:
    return "Intel UHD Graphics (Comet Lake)";
  case 0x46A6:
  case 0x46A8:
  case 0x46D1:
    return "Intel Iris Xe Graphics";
  default:
    return "Intel Integrated Graphics";
  }
}

static int intel_gfx_is_supported_subclass(const pci_device_t *pci_dev) {
  if (!pci_dev)
    return 0;
  return pci_dev->subclass == 0x00 || pci_dev->subclass == 0x80;
}

static int intel_gfx_mmio_bar_is_sane(uint64_t addr) {
  if (!addr)
    return 0;
  if (addr == 0xFFFFFFFFULL || addr == 0xFFFFFFFFFFFFFFFFULL)
    return 0;
  if ((addr & 0xFFFULL) != 0)
    return 0;
  return 1;
}

static int intel_gfx_framebuffer_is_sane(uint32_t *fb, uint32_t width,
                                         uint32_t height, uint32_t pitch) {
  if (!fb || !width || !height)
    return 0;
  if (width > 16384 || height > 16384)
    return 0;
  if (pitch < width * sizeof(uint32_t))
    return 0;
  if (pitch > width * sizeof(uint32_t) * 8)
    return 0;
  return 1;
}

static uint64_t intel_gfx_read_bar(const pci_device_t *pci_dev,
                                   uint8_t bar_offset) {
  uint32_t low;
  uint64_t addr;

  if (!pci_dev)
    return 0;

  low = pci_read32(pci_dev->bus, pci_dev->slot, pci_dev->func, bar_offset);
  if (low == 0 || low == 0xFFFFFFFF)
    return 0;
  if (low & 0x1)
    return 0;

  addr = (uint64_t)(low & 0xFFFFFFF0U);
  if ((low & 0x6) == 0x4) {
    uint32_t high =
        pci_read32(pci_dev->bus, pci_dev->slot, pci_dev->func, bar_offset + 4);
    addr |= ((uint64_t)high << 32);
  }

  return addr;
}

int intel_gfx_init(pci_device_t *pci_dev) {
  uint32_t *fb = NULL;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t pitch = 0;
  int safe_framebuffer = 0;
  int use_framebuffer_only_probe = 0;

  if (!intel_gfx_is_display_device(pci_dev))
    return -1;
  if (!intel_gfx_is_supported_subclass(pci_dev)) {
    printk(KERN_WARNING "IGFX: Unsupported display subclass 0x%02x, skipping\n",
           pci_dev->subclass);
    return -1;
  }
  if (intel_gfx_state.initialized)
    return 0;
  if (intel_gfx_state.probe_attempted && intel_gfx_state.init_failed)
    return -1;

  intel_gfx_state = (intel_gfx_state_t){0};
  intel_gfx_state.probe_attempted = true;
  intel_gfx_state.device_id = pci_dev->device_id;
  intel_gfx_state.irq = pci_dev->irq;

  /*
   * On x86 bring-up, touching Intel display PCI command/BAR state too early can
   * disrupt the firmware framebuffer and crash startup. Keep the probe read-only
   * there and reuse only the bootloader framebuffer until a real modesetting
   * path exists.
   */
#if defined(ARCH_X86_64) || defined(ARCH_X86)
  use_framebuffer_only_probe = 1;
#endif

  if (!use_framebuffer_only_probe) {
    pci_enable_device(pci_dev);
    intel_gfx_state.mmio_base = intel_gfx_read_bar(pci_dev, PCI_BAR0);
    intel_gfx_state.aperture_base = intel_gfx_read_bar(pci_dev, PCI_BAR2);
  }

  if (!intel_gfx_mmio_bar_is_sane(intel_gfx_state.mmio_base)) {
    if (intel_gfx_state.mmio_base) {
      printk(KERN_WARNING "IGFX: Ignoring unsafe MMIO BAR0=0x%llx\n",
             (unsigned long long)intel_gfx_state.mmio_base);
    }
    intel_gfx_state.mmio_base = 0;
  }

  if (!intel_gfx_mmio_bar_is_sane(intel_gfx_state.aperture_base)) {
    if (intel_gfx_state.aperture_base) {
      printk(KERN_WARNING "IGFX: Ignoring unsafe aperture BAR2=0x%llx\n",
             (unsigned long long)intel_gfx_state.aperture_base);
    }
    intel_gfx_state.aperture_base = 0;
  }

  if (intel_gfx_state.mmio_base) {
#if defined(ARCH_X86_64) || defined(ARCH_X86)
    printk(KERN_INFO "IGFX: MMIO BAR present at 0x%llx, deferred on x86 bring-up "
                     "path\n",
           (unsigned long long)intel_gfx_state.mmio_base);
#else
    if (vmm_map_range(intel_gfx_state.mmio_base, intel_gfx_state.mmio_base,
                      INTEL_GFX_MMIO_MAP_SIZE, VM_DEVICE) == 0) {
      intel_gfx_state.mmio =
          (volatile uint8_t *)(uintptr_t)intel_gfx_state.mmio_base;
      intel_gfx_state.mmio_mapped = true;
    } else {
      printk(KERN_WARNING "IGFX: Failed to map MMIO BAR0=0x%llx\n",
             (unsigned long long)intel_gfx_state.mmio_base);
      intel_gfx_state.mmio_base = 0;
    }
#endif
  }

  extern void fb_get_info(uint32_t **buffer, uint32_t *width, uint32_t *height);
  extern uint32_t fb_get_pitch(void);

  fb_get_info(&fb, &width, &height);
  pitch = fb_get_pitch();
  safe_framebuffer = intel_gfx_framebuffer_is_sane(fb, width, height, pitch);

  if (safe_framebuffer) {
    intel_gfx_state.width = width;
    intel_gfx_state.height = height;
    intel_gfx_state.pitch = pitch;
    intel_gfx_state.has_framebuffer = true;
    intel_gfx_state.supports_gpu_rendering =
        intel_gfx_is_2012_2013_supported(pci_dev->device_id);
  } else if (fb || width || height || pitch) {
    printk(KERN_WARNING
           "IGFX: Ignoring invalid framebuffer handoff fb=%p %ux%u pitch=%u\n",
           fb, width, height, pitch);
  }

  if (!intel_gfx_state.has_framebuffer && !intel_gfx_state.mmio_base) {
    intel_gfx_state.init_failed = true;
    printk(KERN_WARNING
           "IGFX: No safe framebuffer or MMIO path available, driver disabled\n");
    return -1;
  }

  intel_gfx_state.initialized = true;

  printk(KERN_INFO "IGFX: %s detected (%04x:%04x) at %02x:%02x.%x\n",
         intel_gfx_detect_name(pci_dev->device_id), pci_dev->vendor_id,
         pci_dev->device_id, pci_dev->bus, pci_dev->slot, pci_dev->func);
  if (use_framebuffer_only_probe) {
    printk(KERN_INFO "IGFX: Using framebuffer-only probe on x86 startup path\n");
  }
  printk(KERN_INFO "IGFX: MMIO BAR0=0x%llx aperture BAR2=0x%llx IRQ=%u\n",
         (unsigned long long)intel_gfx_state.mmio_base,
         (unsigned long long)intel_gfx_state.aperture_base,
         intel_gfx_state.irq);

  if (intel_gfx_state.has_framebuffer) {
    printk(KERN_INFO "IGFX: Using boot framebuffer %ux%u pitch=%u\n",
           intel_gfx_state.width, intel_gfx_state.height, intel_gfx_state.pitch);
    if (intel_gfx_state.supports_gpu_rendering) {
      printk(KERN_INFO
             "IGFX: Enabling compositor GPU rendering path for 2012-2013 Intel GPU\n");
    } else {
      printk(KERN_INFO
             "IGFX: Leaving compositor in framebuffer mode for this Intel GPU generation\n");
    }
  } else if (intel_gfx_state.mmio_base) {
    printk(KERN_INFO "IGFX: Running in conservative MMIO-only mode\n");
  } else {
    printk(KERN_WARNING "IGFX: No active framebuffer available yet\n");
  }

  return 0;
}

int intel_gfx_is_ready(void) { return intel_gfx_state.initialized ? 1 : 0; }

int intel_gfx_has_framebuffer(void) {
  return intel_gfx_state.initialized && intel_gfx_state.has_framebuffer;
}

int intel_gfx_supports_gpu_rendering(void) {
  return intel_gfx_state.initialized && intel_gfx_state.has_framebuffer &&
         intel_gfx_state.supports_gpu_rendering;
}

void intel_gfx_get_display_info(uint32_t *width, uint32_t *height,
                                uint32_t *pitch) {
  if (width)
    *width = intel_gfx_state.width;
  if (height)
    *height = intel_gfx_state.height;
  if (pitch)
    *pitch = intel_gfx_state.pitch;
}

const char *intel_gfx_get_name(void) {
  return intel_gfx_detect_name(intel_gfx_state.device_id);
}
