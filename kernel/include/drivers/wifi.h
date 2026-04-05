/*
 * OS8 - Wi-Fi Driver Abstraction
 *
 * First-pass wireless support used by Settings and system status surfaces.
 * This layer is intentionally conservative: it detects a small set of
 * supported adapters, exposes scan/connect state, and falls back cleanly when
 * no supported hardware is present.
 */

#ifndef DRIVERS_WIFI_H
#define DRIVERS_WIFI_H

#include "types.h"

void wifi_init(void);
int wifi_has_supported_adapter(void);
int wifi_is_intel_adapter(void);
int wifi_is_connected(void);
int wifi_has_scan_results(void);
const char *wifi_get_adapter_name(void);
const char *wifi_get_driver_name(void);
const char *wifi_get_status_text(void);
const char *wifi_get_connected_ssid(void);
int wifi_get_signal_strength(void);
int wifi_get_network_count(void);
const char *wifi_get_network_ssid(int index);
int wifi_get_network_signal(int index);
int wifi_get_network_secure(int index);
int wifi_is_network_connected(int index);
int wifi_get_selected_network(void);
void wifi_select_network(int index);
int wifi_scan(void);
int wifi_connect_selected(void);
void wifi_disconnect(void);

#endif
