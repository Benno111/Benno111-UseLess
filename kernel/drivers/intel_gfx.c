/*
 * Vib-OS - Intel Integrated Graphics Driver
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
  bool has_framebuffer;
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

static const char *intel_gfx_detect_name(uint16_t device_id) {
  switch (device_id) {
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

int intel_gfx_init(pci_device_t *pci_dev) {
  uint32_t *fb = NULL;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t pitch = 0;

  if (!intel_gfx_is_display_device(pci_dev))
    return -1;
  if (intel_gfx_state.initialized)
    return 0;

  pci_enable_device(pci_dev);

  intel_gfx_state.device_id = pci_dev->device_id;
  intel_gfx_state.mmio_base = pci_dev->bar0;
  intel_gfx_state.aperture_base = pci_dev->bar2;
  intel_gfx_state.irq = pci_dev->irq;

  if (intel_gfx_state.mmio_base) {
    vmm_map_range(intel_gfx_state.mmio_base, intel_gfx_state.mmio_base,
                  INTEL_GFX_MMIO_MAP_SIZE, VM_DEVICE);
    intel_gfx_state.mmio = (volatile uint8_t *)(uintptr_t)intel_gfx_state.mmio_base;
  }

  extern void fb_get_info(uint32_t **buffer, uint32_t *width, uint32_t *height);
  extern uint32_t fb_get_pitch(void);

  fb_get_info(&fb, &width, &height);
  pitch = fb_get_pitch();

  intel_gfx_state.width = width;
  intel_gfx_state.height = height;
  intel_gfx_state.pitch = pitch;
  intel_gfx_state.has_framebuffer = (fb != NULL && width != 0 && height != 0);
  intel_gfx_state.initialized = true;

  printk(KERN_INFO "IGFX: %s detected (%04x:%04x) at %02x:%02x.%x\n",
         intel_gfx_detect_name(pci_dev->device_id), pci_dev->vendor_id,
         pci_dev->device_id, pci_dev->bus, pci_dev->slot, pci_dev->func);
  printk(KERN_INFO "IGFX: MMIO BAR0=0x%llx aperture BAR2=0x%llx IRQ=%u\n",
         (unsigned long long)intel_gfx_state.mmio_base,
         (unsigned long long)intel_gfx_state.aperture_base,
         intel_gfx_state.irq);

  if (intel_gfx_state.has_framebuffer) {
    printk(KERN_INFO "IGFX: Using boot framebuffer %ux%u pitch=%u\n",
           intel_gfx_state.width, intel_gfx_state.height, intel_gfx_state.pitch);
  } else {
    printk(KERN_WARNING "IGFX: No active framebuffer available yet\n");
  }

  return 0;
}

int intel_gfx_is_ready(void) { return intel_gfx_state.initialized ? 1 : 0; }

int intel_gfx_has_framebuffer(void) {
  return intel_gfx_state.initialized && intel_gfx_state.has_framebuffer;
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
