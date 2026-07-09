// net_wifi.h - WiFi connection manager.
//
// Connects to the home network using credentials stored in NVS. When no
// credentials exist (or the network is unreachable) it opens the setup
// hotspot with a captive portal where the user enters them. WPS push-button
// pairing is available at any time via the AUX button.
#ifndef NET_WIFI_H
#define NET_WIFI_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NET_WIFI_STATE_IDLE = 0,
    NET_WIFI_STATE_CONNECTING,   // Station trying to join the home network
    NET_WIFI_STATE_CONNECTED,    // Got an IP address
    NET_WIFI_STATE_WPS,          // WPS push-button pairing in progress
    NET_WIFI_STATE_SETUP         // Setup hotspot + captive portal active
} net_wifi_state_t;

// Called on every state transition (from the WiFi event task context).
typedef void (*net_wifi_state_cb_t)(net_wifi_state_t state);

// Initialize netif/event loop/WiFi driver and load stored credentials.
esp_err_t netWifiInit(net_wifi_state_cb_t state_cb);

// Start networking: station mode if credentials exist, setup hotspot otherwise.
esp_err_t netWifiStart(void);

// Begin WPS push-button pairing (2-minute window). On success the received
// credentials are stored and the device restarts into station mode.
esp_err_t netWifiStartWps(void);

// Store new credentials (from the setup portal or WPS).
esp_err_t netWifiSaveCredentials(const char* ssid, const char* password);

// Erase stored credentials and restart into setup mode.
void netWifiClearCredentialsAndRestart(void);

bool netWifiHasCredentials(void);
net_wifi_state_t netWifiGetState(void);

// Device identity: station MAC as 12 lowercase hex digits (no separators).
// Stable after netWifiInit(); used as the MQTT topic suffix.
const char* netWifiGetMacId(void);

// Diagnostics for the info message (safe fallbacks when not connected).
void netWifiGetIp(char* buf, size_t size);
void netWifiGetSsid(char* buf, size_t size);
int netWifiGetRssi(void);

#ifdef __cplusplus
}
#endif

#endif // NET_WIFI_H
