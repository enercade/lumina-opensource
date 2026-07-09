// net_wifi.cpp - WiFi connection manager (see net_wifi.h).
#include "net_wifi.h"
#include "config.h"
#include "net_portal.h"

#include <string.h>
#include <stdio.h>

#include "esp_wifi.h"
#include "esp_wps.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"

// Optional, git-ignored development credentials (see the .example.h template).
// When present and NVS holds no credentials yet, they are seeded on first boot.
#if __has_include("wifi_dev_credentials.h")
#include "wifi_dev_credentials.h"
#endif

static const char* TAG = "NET_WIFI";

#define NVS_NAMESPACE "lumina_net"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_PASS  "pass"

static net_wifi_state_cb_t s_state_cb = NULL;
static net_wifi_state_t s_state = NET_WIFI_STATE_IDLE;

static esp_netif_t* s_netif_sta = NULL;
static esp_netif_t* s_netif_ap = NULL;

static char s_mac_id[13] = {0};        // 12 lowercase hex digits + NUL
static char s_ssid[33] = {0};          // Stored credentials (RAM copy)
static char s_password[65] = {0};
static int s_retry_count = 0;
static bool s_wps_from_setup = false;  // WPS launched while the hotspot was up

// ----------------------------------------------------------------------------
// State handling
// ----------------------------------------------------------------------------
static void set_state(net_wifi_state_t state) {
    if (s_state == state) return;
    s_state = state;
    ESP_LOGI(TAG, "state -> %d", (int)state);
    if (s_state_cb) s_state_cb(state);
}

net_wifi_state_t netWifiGetState(void) {
    return s_state;
}

// ----------------------------------------------------------------------------
// Credential storage (NVS)
// ----------------------------------------------------------------------------
static bool load_credentials(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;

    size_t ssid_len = sizeof(s_ssid);
    size_t pass_len = sizeof(s_password);
    bool ok = (nvs_get_str(h, NVS_KEY_SSID, s_ssid, &ssid_len) == ESP_OK) &&
              (nvs_get_str(h, NVS_KEY_PASS, s_password, &pass_len) == ESP_OK) &&
              (s_ssid[0] != '\0');
    nvs_close(h);

    if (!ok) {
        s_ssid[0] = '\0';
        s_password[0] = '\0';
    }
    return ok;
}

esp_err_t netWifiSaveCredentials(const char* ssid, const char* password) {
    if (ssid == NULL || ssid[0] == '\0' || strlen(ssid) > 32) {
        return ESP_ERR_INVALID_ARG;
    }
    if (password == NULL) password = "";
    if (strlen(password) > 64) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_PASS, password);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) {
        strlcpy(s_ssid, ssid, sizeof(s_ssid));
        strlcpy(s_password, password, sizeof(s_password));
        ESP_LOGI(TAG, "credentials stored for SSID '%s'", ssid);
    }
    return err;
}

void netWifiClearCredentialsAndRestart(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY_SSID);
        nvs_erase_key(h, NVS_KEY_PASS);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGW(TAG, "WiFi credentials cleared - restarting into setup mode");
    vTaskDelay(pdMS_TO_TICKS(500));  // let the log line flush
    esp_restart();
}

bool netWifiHasCredentials(void) {
    return s_ssid[0] != '\0';
}

// ----------------------------------------------------------------------------
// Identity / diagnostics
// ----------------------------------------------------------------------------
const char* netWifiGetMacId(void) {
    return s_mac_id;
}

void netWifiGetIp(char* buf, size_t size) {
    esp_netif_ip_info_t ip_info;
    if (s_state == NET_WIFI_STATE_CONNECTED && s_netif_sta != NULL &&
        esp_netif_get_ip_info(s_netif_sta, &ip_info) == ESP_OK) {
        snprintf(buf, size, IPSTR, IP2STR(&ip_info.ip));
    } else {
        strlcpy(buf, "0.0.0.0", size);
    }
}

void netWifiGetSsid(char* buf, size_t size) {
    strlcpy(buf, s_ssid, size);
}

int netWifiGetRssi(void) {
    wifi_ap_record_t ap;
    if (s_state == NET_WIFI_STATE_CONNECTED && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Station / setup-hotspot bring-up
// ----------------------------------------------------------------------------
static void connect_station(void) {
    wifi_config_t sta_config = {};
    strlcpy((char*)sta_config.sta.ssid, s_ssid, sizeof(sta_config.sta.ssid));
    strlcpy((char*)sta_config.sta.password, s_password, sizeof(sta_config.sta.password));

    s_retry_count = 0;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    set_state(NET_WIFI_STATE_CONNECTING);
    ESP_LOGI(TAG, "connecting to '%s'...", s_ssid);
}

// Open the setup hotspot (APSTA: the station interface stays available for
// network scans and WPS) and start the captive portal.
static void start_setup_mode(void) {
    wifi_config_t ap_config = {};
    snprintf((char*)ap_config.ap.ssid, sizeof(ap_config.ap.ssid),
             SETUP_AP_SSID_PREFIX "%s", &s_mac_id[6]);  // last 3 MAC bytes
    ap_config.ap.ssid_len = strlen((char*)ap_config.ap.ssid);
    ap_config.ap.channel = SETUP_AP_CHANNEL;
    ap_config.ap.max_connection = SETUP_AP_MAX_CONN;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    netPortalStart();
    set_state(NET_WIFI_STATE_SETUP);
    ESP_LOGI(TAG, "setup hotspot '%s' up - portal at http://192.168.4.1",
             (char*)ap_config.ap.ssid);
}

// ----------------------------------------------------------------------------
// WPS push-button pairing
// ----------------------------------------------------------------------------
esp_err_t netWifiStartWps(void) {
    if (s_state == NET_WIFI_STATE_WPS) return ESP_OK;

    s_wps_from_setup = (s_state == NET_WIFI_STATE_SETUP);
    if (s_state == NET_WIFI_STATE_CONNECTING || s_state == NET_WIFI_STATE_CONNECTED) {
        esp_wifi_disconnect();
    }

    esp_wps_config_t wps_config = WPS_CONFIG_INIT_DEFAULT(WPS_TYPE_PBC);
    esp_err_t err = esp_wifi_wps_enable(&wps_config);
    if (err == ESP_OK) err = esp_wifi_wps_start(0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WPS start failed: %s", esp_err_to_name(err));
        esp_wifi_wps_disable();
        return err;
    }

    set_state(NET_WIFI_STATE_WPS);
    ESP_LOGI(TAG, "WPS pairing window open - press the WPS button on the router");
    return ESP_OK;
}

// After a failed/expired WPS window, fall back to whatever we were doing.
static void wps_abort_fallback(void) {
    esp_wifi_wps_disable();
    if (s_wps_from_setup || !netWifiHasCredentials()) {
        set_state(NET_WIFI_STATE_SETUP);   // hotspot is still up (APSTA)
    } else {
        s_retry_count = 0;
        esp_wifi_connect();
        set_state(NET_WIFI_STATE_CONNECTING);
    }
}

// ----------------------------------------------------------------------------
// WiFi / IP event handling
// ----------------------------------------------------------------------------
static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_START:
                if (s_state == NET_WIFI_STATE_CONNECTING) {
                    esp_wifi_connect();
                }
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                if (s_state == NET_WIFI_STATE_WPS) {
                    break;  // expected while WPS negotiates
                }
                if (s_state == NET_WIFI_STATE_CONNECTING || s_state == NET_WIFI_STATE_CONNECTED) {
                    if (++s_retry_count <= WIFI_STA_MAX_RETRY) {
                        ESP_LOGW(TAG, "disconnected - retry %d/%d", s_retry_count, WIFI_STA_MAX_RETRY);
                        set_state(NET_WIFI_STATE_CONNECTING);
                        esp_wifi_connect();
                    } else {
                        ESP_LOGW(TAG, "cannot reach '%s' - opening setup hotspot", s_ssid);
                        start_setup_mode();
                    }
                }
                break;

            case WIFI_EVENT_STA_WPS_ER_SUCCESS: {
                ESP_LOGI(TAG, "WPS success - storing credentials and restarting");
                wifi_event_sta_wps_er_success_t* evt = (wifi_event_sta_wps_er_success_t*)data;
                if (evt != NULL && evt->ap_cred_cnt > 0) {
                    netWifiSaveCredentials((const char*)evt->ap_cred[0].ssid,
                                           (const char*)evt->ap_cred[0].passphrase);
                } else {
                    // Single-AP case: the driver already placed the credentials
                    // in the station config.
                    wifi_config_t cfg;
                    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK) {
                        netWifiSaveCredentials((const char*)cfg.sta.ssid,
                                               (const char*)cfg.sta.password);
                    }
                }
                esp_wifi_wps_disable();
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();  // clean boot straight into station mode
                break;
            }

            case WIFI_EVENT_STA_WPS_ER_FAILED:
                ESP_LOGW(TAG, "WPS failed");
                wps_abort_fallback();
                break;

            case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
                ESP_LOGW(TAG, "WPS window expired");
                wps_abort_fallback();
                break;

            default:
                break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* evt = (ip_event_got_ip_t*)data;
        ESP_LOGI(TAG, "connected - IP " IPSTR, IP2STR(&evt->ip_info.ip));
        s_retry_count = 0;
        set_state(NET_WIFI_STATE_CONNECTED);
    }
}

// ----------------------------------------------------------------------------
// Init / start
// ----------------------------------------------------------------------------
esp_err_t netWifiInit(net_wifi_state_cb_t state_cb) {
    s_state_cb = state_cb;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif_sta = esp_netif_create_default_wifi_sta();
    s_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    // Device identity from the station MAC.
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(s_mac_id, sizeof(s_mac_id), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "device id: %s%s", MQTT_TOPIC_PREFIX, s_mac_id);

    bool has_creds = load_credentials();

#ifdef LUMINA_DEV_WIFI_SSID
    if (!has_creds) {
        ESP_LOGW(TAG, "seeding development WiFi credentials (wifi_dev_credentials.h)");
        if (netWifiSaveCredentials(LUMINA_DEV_WIFI_SSID, LUMINA_DEV_WIFI_PASSWORD) == ESP_OK) {
            has_creds = true;
        }
    }
#endif

    ESP_LOGI(TAG, "credentials %s", has_creds ? "found" : "not set");
    return ESP_OK;
}

esp_err_t netWifiStart(void) {
    if (netWifiHasCredentials()) {
        connect_station();
    } else {
        start_setup_mode();
    }
    return ESP_OK;
}
