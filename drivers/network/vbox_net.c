/*
 * OS8 - VirtualBox PCNet Network Driver
 *
 * This binds the AMD PCnet adapter VirtualBox commonly exposes by default.
 * Bring-up is intentionally conservative: we enable the PCI function, read its
 * MAC address, and register a kernel network interface without touching the
 * riskier DMA path yet.
 */

#include "arch/arch.h"
#include "drivers/pci.h"
#include "drivers/vbox_net.h"
#include "net/net.h"
#include "printk.h"

#define VBOX_PCNET_VENDOR_ID 0x1022
#define VBOX_PCNET_DEVICE_ID 0x2000
#define VBOX_PCNET_ALT_DEVICE_ID 0x2001

static pci_device_t *vbox_pci_dev = 0;
static struct net_interface *vbox_iface = 0;
static uint16_t vbox_io_base = 0;
static uint8_t vbox_mac[ETH_ALEN];
static char vbox_name[40] = "VirtualBox AMD PCnet";
static int vbox_warned_send = 0;

static int vbox_net_send(struct net_interface *iface, const void *data,
                         size_t len) {
  (void)iface;
  (void)data;
  (void)len;

  if (!vbox_warned_send) {
    printk(KERN_WARNING
           "VBOX-NET: Packet TX requested before DMA ring support is implemented\n");
    vbox_warned_send = 1;
  }
  return -1;
}

static pci_device_t *vbox_find_pcnet(void) {
  pci_device_t *dev = pci_find_device(VBOX_PCNET_VENDOR_ID, VBOX_PCNET_DEVICE_ID);
  if (dev)
    return dev;
  return pci_find_device(VBOX_PCNET_VENDOR_ID, VBOX_PCNET_ALT_DEVICE_ID);
}

#if defined(ARCH_X86_64) || defined(ARCH_X86)
static uint16_t vbox_read_io_base(pci_device_t *dev) {
  uint32_t bar0_raw;

  if (!dev)
    return 0;

  bar0_raw = pci_read32(dev->bus, dev->slot, dev->func, PCI_BAR0);
  if ((bar0_raw & 0x1) == 0)
    return 0;

  return (uint16_t)(bar0_raw & 0xFFFC);
}

static void vbox_read_mac(uint16_t io_base, uint8_t *mac) {
  for (int i = 0; i < ETH_ALEN; i++)
    mac[i] = inb((uint16_t)(io_base + i));
}
#endif

int vbox_net_init(void) {
#if !defined(ARCH_X86_64) && !defined(ARCH_X86)
  printk(KERN_INFO
         "VBOX-NET: VirtualBox PCNet probe skipped on non-x86 platform\n");
  return -1;
#else
  printk(KERN_INFO "VBOX-NET: Probing VirtualBox PCNet adapter...\n");

  vbox_pci_dev = vbox_find_pcnet();
  if (!vbox_pci_dev) {
    printk(KERN_INFO "VBOX-NET: No VirtualBox PCNet adapter detected\n");
    return -1;
  }

  pci_enable_device(vbox_pci_dev);
  vbox_io_base = vbox_read_io_base(vbox_pci_dev);
  if (!vbox_io_base) {
    printk(KERN_WARNING "VBOX-NET: Adapter found but IO BAR0 is unavailable\n");
    return -1;
  }

  vbox_read_mac(vbox_io_base, vbox_mac);
  printk(KERN_INFO
         "VBOX-NET: Found VirtualBox PCNet at %02x:%02x.%x IO=0x%04x MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
         vbox_pci_dev->bus, vbox_pci_dev->slot, vbox_pci_dev->func,
         vbox_io_base, vbox_mac[0], vbox_mac[1], vbox_mac[2], vbox_mac[3],
         vbox_mac[4], vbox_mac[5]);

  vbox_iface =
      net_add_interface("eth0", vbox_mac, 0x0A00020F, 0xFFFFFF00, 0x0A000202);
  if (!vbox_iface) {
    printk(KERN_WARNING "VBOX-NET: Failed to register network interface\n");
    return -1;
  }

  vbox_iface->send = vbox_net_send;
  return 0;
#endif
}

int vbox_net_is_ready(void) { return vbox_iface != 0; }

void vbox_net_poll(void) {}

const char *vbox_net_get_name(void) { return vbox_name; }
