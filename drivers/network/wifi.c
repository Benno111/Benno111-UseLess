/*
 * OS8 - Wi-Fi Driver Layer
 *
 * This is a safe first-pass wireless subsystem. It detects a curated set of
 * PCI adapters, exposes them to the UI, and simulates scan/connect state
 * without touching adapter MMIO yet. That gives the OS a real Wi-Fi menu and
 * driver ownership model without introducing unstable hardware bring-up.
 */

#include "drivers/pci.h"
#include "drivers/wifi.h"
#include "printk.h"

typedef struct {
  uint16_t vendor_id;
  uint16_t device_id;
  const char *adapter_name;
  const char *driver_name;
} wifi_pci_match_t;

typedef struct {
  const char *ssid;
  int signal;
  int secure;
} wifi_network_t;

static const wifi_pci_match_t wifi_supported_devices[] = {
    {0x8086, 0x0082, "Intel Centrino Advanced-N 6205",
     "intel iwlwifi driver"},
    {0x8086, 0x0085, "Intel Centrino Advanced-N + WiMAX 6250",
     "intel iwlwifi driver"},
    {0x8086, 0x008A, "Intel Centrino Wireless-N 1030",
     "intel iwlwifi driver"},
    {0x8086, 0x008B, "Intel Centrino Wireless-N 130",
     "intel iwlwifi driver"},
    {0x8086, 0x0887, "Intel Centrino Wireless-N 2230",
     "intel iwlwifi driver"},
    {0x8086, 0x0891, "Intel Centrino Wireless-N 2200",
     "intel iwlwifi driver"},
    {0x8086, 0x088E, "Intel Centrino Advanced-N 6235",
     "intel iwlwifi driver"},
    {0x8086, 0x088F, "Intel Centrino Advanced-N 6235",
     "intel iwlwifi driver"},
    {0x8086, 0x08B1, "Intel Dual Band Wireless-AC 7260",
     "intel iwlwifi driver"},
    {0x8086, 0x08B2, "Intel Dual Band Wireless-N 7260",
     "intel iwlwifi driver"},
    {0x8086, 0x08B3, "Intel Dual Band Wireless-AC 3160",
     "intel iwlwifi driver"},
    {0x8086, 0x08B4, "Intel Dual Band Wireless-AC 7260",
     "intel iwlwifi driver"},
    {0x168C, 0x0030, "Qualcomm Atheros AR93xx",
     "ath9k compatibility driver"},
    {0x14E4, 0x4359, "Broadcom BCM43228", "brcmsmac compatibility driver"},
    {0x10EC, 0x8178, "Realtek RTL8192CE", "rtl8192ce compatibility driver"},
};

static const wifi_network_t wifi_catalog[] = {
    {"HomeLab", 82, 1},
    {"Workshop-5G", 74, 1},
    {"VibOS Guest", 63, 0},
    {"RetroLAN", 48, 1},
};

static int wifi_signal_levels[] = {82, 74, 63, 48};
static int wifi_visible_networks[4] = {-1, -1, -1, -1};

typedef struct {
  int initialized;
  int adapter_present;
  int intel_adapter;
  int connected;
  int scan_ready;
  int visible_count;
  int selected_network;
  int connected_network;
  int scan_generation;
  const char *adapter_name;
  const char *driver_name;
  char status_text[96];
} wifi_state_t;

static wifi_state_t wifi_state = {
    0, 0, 0, 0, 0, 0, -1, -1, 0, "No supported Wi-Fi adapter detected",
    "No wireless driver bound", "Wireless drivers are standing by.",
};

static void wifi_copy_text(char *dst, int max, const char *src) {
  int i;

  if (!dst || max <= 0)
    return;

  for (i = 0; src && src[i] && i < max - 1; i++)
    dst[i] = src[i];
  dst[i] = '\0';
}

static void wifi_set_status(const char *text) {
  wifi_copy_text(wifi_state.status_text, sizeof(wifi_state.status_text), text);
}

static void wifi_set_status_with_suffix(const char *prefix, const char *suffix) {
  int len = 0;

  wifi_copy_text(wifi_state.status_text, sizeof(wifi_state.status_text), prefix);
  while (wifi_state.status_text[len] &&
         len < (int)sizeof(wifi_state.status_text) - 1)
    len++;
  wifi_copy_text(wifi_state.status_text + len,
                 (int)(sizeof(wifi_state.status_text) - len), suffix);
}

static int wifi_catalog_count(void) {
  return (int)(sizeof(wifi_catalog) / sizeof(wifi_catalog[0]));
}

static int wifi_visible_catalog_index(int index) {
  if (index < 0 || index >= wifi_state.visible_count)
    return -1;
  return wifi_visible_networks[index];
}

static void wifi_sort_visible_networks(void) {
  for (int i = 0; i < wifi_state.visible_count; i++) {
    for (int j = i + 1; j < wifi_state.visible_count; j++) {
      int left = wifi_visible_networks[i];
      int right = wifi_visible_networks[j];
      if (left < 0 || right < 0)
        continue;
      if (wifi_signal_levels[right] > wifi_signal_levels[left]) {
        int tmp = wifi_visible_networks[i];
        wifi_visible_networks[i] = wifi_visible_networks[j];
        wifi_visible_networks[j] = tmp;
      }
    }
  }
}

static void wifi_set_default_selection(void) {
  if (wifi_state.visible_count <= 0) {
    wifi_state.selected_network = -1;
    return;
  }

  for (int i = 0; i < wifi_state.visible_count; i++) {
    if (wifi_visible_networks[i] == wifi_state.connected_network) {
      wifi_state.selected_network = i;
      return;
    }
  }

  wifi_state.selected_network = 0;
}

static const wifi_pci_match_t *wifi_probe_supported_adapter(void) {
  int i;

  for (i = 0; i < (int)(sizeof(wifi_supported_devices) /
                        sizeof(wifi_supported_devices[0]));
       i++) {
    if (pci_find_device(wifi_supported_devices[i].vendor_id,
                        wifi_supported_devices[i].device_id))
      return &wifi_supported_devices[i];
  }

  return 0;
}

void wifi_init(void) {
  const wifi_pci_match_t *match;

  if (wifi_state.initialized)
    return;

  wifi_state.initialized = 1;
  wifi_state.selected_network = -1;
  wifi_state.connected_network = -1;

  printk(KERN_INFO "WIFI: Loading wireless drivers...\n");

  match = wifi_probe_supported_adapter();
  if (!match) {
    wifi_set_status("No supported Wi-Fi adapter detected.");
    printk(KERN_INFO "WIFI: No supported PCI Wi-Fi adapter detected\n");
    return;
  }

  wifi_state.adapter_present = 1;
  wifi_state.intel_adapter = match->vendor_id == 0x8086 ? 1 : 0;
  wifi_state.adapter_name = match->adapter_name;
  wifi_state.driver_name = match->driver_name;
  wifi_set_status(wifi_state.intel_adapter
                      ? "Intel Wi-Fi adapter ready. Run a scan to list networks."
                      : "Wi-Fi adapter ready. Run a scan to list networks.");

  printk(KERN_INFO "WIFI: Bound %s using %s\n", wifi_state.adapter_name,
         wifi_state.driver_name);
}

int wifi_has_supported_adapter(void) { return wifi_state.adapter_present; }

int wifi_is_intel_adapter(void) { return wifi_state.intel_adapter; }

int wifi_is_connected(void) { return wifi_state.connected; }

int wifi_has_scan_results(void) {
  return wifi_state.adapter_present && wifi_state.scan_ready &&
         wifi_state.visible_count > 0;
}

const char *wifi_get_adapter_name(void) { return wifi_state.adapter_name; }

const char *wifi_get_driver_name(void) { return wifi_state.driver_name; }

const char *wifi_get_status_text(void) { return wifi_state.status_text; }

const char *wifi_get_connected_ssid(void) {
  if (!wifi_state.connected || wifi_state.connected_network < 0 ||
      wifi_state.connected_network >=
          (int)(sizeof(wifi_catalog) / sizeof(wifi_catalog[0])))
    return "Not connected";

  return wifi_catalog[wifi_state.connected_network].ssid;
}

int wifi_get_signal_strength(void) {
  if (!wifi_state.connected || wifi_state.connected_network < 0 ||
      wifi_state.connected_network >=
          (int)(sizeof(wifi_catalog) / sizeof(wifi_catalog[0])))
    return 0;

  return wifi_signal_levels[wifi_state.connected_network];
}

int wifi_get_network_count(void) {
  if (!wifi_has_scan_results())
    return 0;

  return wifi_state.visible_count;
}

const char *wifi_get_network_ssid(int index) {
  int catalog_index = wifi_visible_catalog_index(index);
  if (catalog_index < 0)
    return "";
  return wifi_catalog[catalog_index].ssid;
}

int wifi_get_network_signal(int index) {
  int catalog_index = wifi_visible_catalog_index(index);
  if (catalog_index < 0)
    return 0;
  return wifi_signal_levels[catalog_index];
}

int wifi_get_network_secure(int index) {
  int catalog_index = wifi_visible_catalog_index(index);
  if (catalog_index < 0)
    return 0;
  return wifi_catalog[catalog_index].secure;
}

int wifi_is_network_connected(int index) {
  int catalog_index = wifi_visible_catalog_index(index);
  return catalog_index >= 0 && wifi_state.connected &&
         catalog_index == wifi_state.connected_network;
}

int wifi_get_selected_network(void) { return wifi_state.selected_network; }

void wifi_select_network(int index) {
  if (index < 0 || index >= wifi_get_network_count())
    return;
  wifi_state.selected_network = index;
  wifi_set_status("Network selected. Connect when ready.");
}

int wifi_scan(void) {
  int i;
  int base_visible_count;
  int connected_visible = 0;

  if (!wifi_state.adapter_present) {
    wifi_set_status("No supported Wi-Fi adapter detected.");
    return 0;
  }

  wifi_state.scan_generation++;
  for (i = 0; i < wifi_catalog_count(); i++) {
    int base = wifi_catalog[i].signal;
    int delta = ((wifi_state.scan_generation + i) % 3) * 2;
    wifi_signal_levels[i] = base - 2 + delta;
    if (wifi_signal_levels[i] < 25)
      wifi_signal_levels[i] = 25;
  }

  for (i = 0; i < wifi_catalog_count(); i++)
    wifi_visible_networks[i] = -1;

  base_visible_count = 2 + (wifi_state.scan_generation % 3);
  if (base_visible_count > wifi_catalog_count())
    base_visible_count = wifi_catalog_count();

  wifi_state.visible_count = base_visible_count;
  for (i = 0; i < wifi_state.visible_count; i++)
    wifi_visible_networks[i] = i;

  if (wifi_state.connected && wifi_state.connected_network >= 0) {
    for (i = 0; i < wifi_state.visible_count; i++) {
      if (wifi_visible_networks[i] == wifi_state.connected_network) {
        connected_visible = 1;
        break;
      }
    }
    if (!connected_visible && wifi_state.visible_count < wifi_catalog_count()) {
      wifi_visible_networks[wifi_state.visible_count++] =
          wifi_state.connected_network;
    } else if (!connected_visible && wifi_state.visible_count > 0) {
      wifi_visible_networks[wifi_state.visible_count - 1] =
          wifi_state.connected_network;
    }
  }

  wifi_sort_visible_networks();
  wifi_state.scan_ready = 1;
  wifi_set_default_selection();
  wifi_set_status("Scan complete. Nearby Wi-Fi networks refreshed.");
  return wifi_state.visible_count;
}

int wifi_connect_selected(const char *password) {
  int index = wifi_state.selected_network;
  int catalog_index;
  const char *ssid;

  if (!wifi_state.adapter_present) {
    wifi_set_status("No supported Wi-Fi adapter detected.");
    return 0;
  }

  if (!wifi_state.scan_ready || wifi_state.visible_count <= 0) {
    wifi_set_status("Run a Wi-Fi scan before connecting.");
    return 0;
  }

  if (index < 0 || index >= wifi_get_network_count()) {
    wifi_set_status("Select a Wi-Fi network first.");
    return 0;
  }

  catalog_index = wifi_visible_catalog_index(index);
  if (catalog_index < 0) {
    wifi_set_status("Selected Wi-Fi network is no longer available.");
    return 0;
  }

  if (wifi_catalog[catalog_index].secure && (!password || !password[0])) {
    wifi_set_status("Enter the Wi-Fi password before connecting.");
    return 0;
  }

  ssid = wifi_catalog[catalog_index].ssid;
  wifi_state.connected = 1;
  wifi_state.connected_network = catalog_index;
  wifi_set_status_with_suffix("Connected to ", ssid);
  return 1;
}

void wifi_disconnect(void) {
  if (!wifi_state.adapter_present) {
    wifi_set_status("No supported Wi-Fi adapter detected.");
    return;
  }

  wifi_state.connected = 0;
  wifi_state.connected_network = -1;
  wifi_set_status("Disconnected from Wi-Fi.");
  wifi_set_default_selection();
}
