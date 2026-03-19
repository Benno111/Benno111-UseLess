#include "drivers/usb/usb.h"

#include "arch/arch.h"
#include "mm/vmm.h"
#include "printk.h"

#define XHCI_MMIO_MAP_SIZE 0x4000

#define XHCI_CAPLENGTH 0x00
#define XHCI_HCSPARAMS1 0x04
#define XHCI_HCCPARAMS1 0x10

#define XHCI_USBCMD 0x00
#define XHCI_USBSTS 0x04
#define XHCI_CONFIG 0x38
#define XHCI_PORTSC_BASE 0x400
#define XHCI_PORTSC_STRIDE 0x10

#define XHCI_USBCMD_RUN_STOP (1U << 0)
#define XHCI_USBCMD_HCRST (1U << 1)

#define XHCI_USBSTS_HCH (1U << 0)
#define XHCI_USBSTS_CNR (1U << 11)

#define XHCI_PORTSC_CCS (1U << 0)

#define XHCI_EXT_CAP_ID_LEGACY 1U
#define XHCI_LEGACY_BIOS_OWNED (1U << 16)
#define XHCI_LEGACY_OS_OWNED (1U << 24)
#define XHCI_PORTSC_SPEED_SHIFT 10
#define XHCI_PORTSC_SPEED_MASK (0xFU << XHCI_PORTSC_SPEED_SHIFT)

#define XHCI_MAX_ENUM_DEVICES 32

typedef struct {
  struct usb_device dev;
  char name[48];
  int port_number;
} xhci_enum_device_t;

typedef struct {
  int initialized;
  uint64_t mmio_base;
  volatile uint8_t *mmio;
  volatile uint8_t *op;
  uint8_t caplength;
  uint32_t hcsparams1;
  uint32_t hccparams1;
  int max_ports;
  int connected_ports;
  xhci_enum_device_t devices[XHCI_MAX_ENUM_DEVICES];
  int device_count;
} xhci_state_t;

static xhci_state_t g_xhci = {0};

static inline uint32_t xhci_read32(volatile uint8_t *base, uint32_t offset) {
  return *(volatile uint32_t *)(base + offset);
}

static inline void xhci_write32(volatile uint8_t *base, uint32_t offset,
                                uint32_t value) {
  *(volatile uint32_t *)(base + offset) = value;
}

static int xhci_wait_op_bits(volatile uint8_t *base, uint32_t offset,
                             uint32_t mask, uint32_t expected,
                             uint64_t timeout_ms) {
  uint64_t start = arch_timer_get_ms();

  while (arch_timer_get_ms() - start < timeout_ms) {
    uint32_t value = xhci_read32(base, offset);
    if ((value & mask) == expected)
      return 0;
  }

  return -1;
}

static int xhci_legacy_handoff(xhci_state_t *xhci) {
  uint32_t hccparams1;
  uint32_t ext_offset_dwords;
  uint32_t ext_offset;

  if (!xhci || !xhci->mmio)
    return -1;

  hccparams1 = xhci->hccparams1;
  ext_offset_dwords = (hccparams1 >> 16) & 0xFFFFU;
  ext_offset = ext_offset_dwords * 4U;

  while (ext_offset >= 0x10 && ext_offset < XHCI_MMIO_MAP_SIZE) {
    uint32_t cap = xhci_read32(xhci->mmio, ext_offset);
    uint32_t cap_id = cap & 0xFFU;
    uint32_t next = (cap >> 8) & 0xFFU;

    if (cap_id == XHCI_EXT_CAP_ID_LEGACY) {
      uint32_t sem = xhci_read32(xhci->mmio, ext_offset + 0x04);
      sem |= XHCI_LEGACY_OS_OWNED;
      xhci_write32(xhci->mmio, ext_offset + 0x04, sem);

      if (sem & XHCI_LEGACY_BIOS_OWNED) {
        uint64_t start = arch_timer_get_ms();
        while (arch_timer_get_ms() - start < 1000) {
          sem = xhci_read32(xhci->mmio, ext_offset + 0x04);
          if (!(sem & XHCI_LEGACY_BIOS_OWNED))
            break;
        }
      }

      printk(KERN_INFO "xHCI: legacy handoff completed\n");
      return 0;
    }

    if (!next)
      break;
    ext_offset += next * 4U;
  }

  return 0;
}

static const char *xhci_speed_name(uint8_t speed) {
  switch (speed) {
  case 1:
    return "full-speed";
  case 2:
    return "low-speed";
  case 3:
    return "high-speed";
  case 4:
    return "super-speed";
  case 5:
    return "super-speed+";
  default:
    return "unknown-speed";
  }
}

static void xhci_append_text(char *dst, int max, const char *src) {
  int idx = 0;

  if (!dst || max <= 0 || !src)
    return;
  while (dst[idx] && idx < max - 1)
    idx++;
  for (int i = 0; src[i] && idx < max - 1; i++)
    dst[idx++] = src[i];
  dst[idx] = '\0';
}

static void xhci_append_decimal(char *dst, int max, int value) {
  char tmp[12];
  int len = 0;

  if (value <= 0) {
    xhci_append_text(dst, max, "0");
    return;
  }

  while (value > 0 && len < (int)sizeof(tmp)) {
    tmp[len++] = (char)('0' + (value % 10));
    value /= 10;
  }
  for (int i = len - 1; i >= 0; i--) {
    char one[2] = {tmp[i], '\0'};
    xhci_append_text(dst, max, one);
  }
}

static void xhci_refresh_device_list(xhci_state_t *xhci) {
  int connected = 0;
  int device_count = 0;

  if (!xhci || !xhci->op)
    return;

  for (int port = 0; port < xhci->max_ports; port++) {
    uint32_t portsc =
        xhci_read32(xhci->op, XHCI_PORTSC_BASE + port * XHCI_PORTSC_STRIDE);
    uint8_t speed =
        (uint8_t)((portsc & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT);

    if (!(portsc & XHCI_PORTSC_CCS))
      continue;

    connected++;
    if (device_count >= XHCI_MAX_ENUM_DEVICES)
      continue;

    xhci->devices[device_count].dev.bus_id = 0;
    xhci->devices[device_count].dev.dev_addr = (uint8_t)(port + 1);
    xhci->devices[device_count].dev.speed = speed;
    xhci->devices[device_count].dev.vendor_id = 0;
    xhci->devices[device_count].dev.product_id = 0;
    xhci->devices[device_count].dev.controller = (void *)xhci;
    xhci->devices[device_count].dev.data = NULL;
    xhci->devices[device_count].port_number = port + 1;

    xhci->devices[device_count].name[0] = '\0';
    xhci_append_text(xhci->devices[device_count].name,
                     sizeof(xhci->devices[device_count].name), "USB Device P");
    xhci_append_decimal(xhci->devices[device_count].name,
                        sizeof(xhci->devices[device_count].name), port + 1);
    xhci_append_text(xhci->devices[device_count].name,
                     sizeof(xhci->devices[device_count].name), " ");
    xhci_append_text(xhci->devices[device_count].name,
                     sizeof(xhci->devices[device_count].name),
                     xhci_speed_name(speed));
    device_count++;
  }

  xhci->connected_ports = connected;
  xhci->device_count = device_count;
}

static void xhci_refresh_port_status(xhci_state_t *xhci) {
  xhci_refresh_device_list(xhci);
}

int xhci_init(phys_addr_t mmio_base) {
  uint32_t hcsparams1;
  uint32_t usbcmd;

  if (!mmio_base)
    return -1;
  if (g_xhci.initialized && g_xhci.mmio_base == mmio_base)
    return 0;

  vmm_map_range(mmio_base, mmio_base, XHCI_MMIO_MAP_SIZE, VM_DEVICE);

  g_xhci.mmio_base = mmio_base;
  g_xhci.mmio = (volatile uint8_t *)(uintptr_t)mmio_base;
  g_xhci.caplength = *(volatile uint8_t *)(g_xhci.mmio + XHCI_CAPLENGTH);
  g_xhci.hcsparams1 = xhci_read32(g_xhci.mmio, XHCI_HCSPARAMS1);
  g_xhci.hccparams1 = xhci_read32(g_xhci.mmio, XHCI_HCCPARAMS1);
  g_xhci.op = g_xhci.mmio + g_xhci.caplength;

  hcsparams1 = g_xhci.hcsparams1;
  g_xhci.max_ports = (int)((hcsparams1 >> 24) & 0xFFU);

  printk(KERN_INFO "xHCI: MMIO=0x%llx caplen=%u ports=%d\n",
         (unsigned long long)g_xhci.mmio_base, g_xhci.caplength,
         g_xhci.max_ports);

  xhci_legacy_handoff(&g_xhci);

  usbcmd = xhci_read32(g_xhci.op, XHCI_USBCMD);
  usbcmd &= ~XHCI_USBCMD_RUN_STOP;
  xhci_write32(g_xhci.op, XHCI_USBCMD, usbcmd);
  if (xhci_wait_op_bits(g_xhci.op, XHCI_USBSTS, XHCI_USBSTS_HCH,
                        XHCI_USBSTS_HCH, 250) != 0) {
    printk(KERN_WARNING "xHCI: controller did not halt before reset\n");
  }

  usbcmd = xhci_read32(g_xhci.op, XHCI_USBCMD);
  xhci_write32(g_xhci.op, XHCI_USBCMD, usbcmd | XHCI_USBCMD_HCRST);
  if (xhci_wait_op_bits(g_xhci.op, XHCI_USBCMD, XHCI_USBCMD_HCRST, 0, 1000) !=
      0) {
    printk(KERN_ERR "xHCI: host controller reset timed out\n");
    return -1;
  }

  if (xhci_wait_op_bits(g_xhci.op, XHCI_USBSTS, XHCI_USBSTS_CNR, 0, 1000) !=
      0) {
    printk(KERN_ERR "xHCI: controller not ready after reset\n");
    return -1;
  }

  xhci_write32(g_xhci.op, XHCI_CONFIG, 0);
  xhci_refresh_port_status(&g_xhci);
  g_xhci.initialized = 1;

  printk(KERN_INFO "xHCI: controller ready, %d/%d ports connected\n",
         g_xhci.connected_ports, g_xhci.max_ports);
  return 0;
}

int xhci_is_ready(void) { return g_xhci.initialized; }

int xhci_get_port_count(void) {
  if (!g_xhci.initialized)
    return 0;
  return g_xhci.max_ports;
}

int xhci_get_connected_count(void) {
  if (!g_xhci.initialized)
    return 0;
  xhci_refresh_port_status(&g_xhci);
  return g_xhci.connected_ports;
}

int usb_device_count(void) {
  if (!g_xhci.initialized)
    return 0;
  xhci_refresh_device_list(&g_xhci);
  return g_xhci.device_count;
}

int usb_device_info(int idx, uint16_t *vid, uint16_t *pid, char *name,
                    int name_len) {
  if (!g_xhci.initialized)
    return 0;
  xhci_refresh_device_list(&g_xhci);
  if (idx < 0 || idx >= g_xhci.device_count)
    return 0;

  if (vid)
    *vid = g_xhci.devices[idx].dev.vendor_id;
  if (pid)
    *pid = g_xhci.devices[idx].dev.product_id;
  if (name && name_len > 0) {
    int out = 0;
    while (g_xhci.devices[idx].name[out] && out < name_len - 1) {
      name[out] = g_xhci.devices[idx].name[out];
      out++;
    }
    name[out] = '\0';
  }
  return 1;
}

int usb_msd_init(struct usb_device *dev) {
  (void)dev;
  printk(KERN_WARNING "USB MSD: transport layer not implemented yet\n");
  return -1;
}

int usb_hid_init(struct usb_device *dev) {
  (void)dev;
  printk(KERN_WARNING "USB HID: transport layer not implemented yet\n");
  return -1;
}
