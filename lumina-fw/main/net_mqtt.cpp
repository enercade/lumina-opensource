// net_mqtt.cpp - MQTT link (see net_mqtt.h).
#include "net_mqtt.h"
#include "config.h"
#include "app_control.h"
#include "net_wifi.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "NET_MQTT";

#define NVS_NAMESPACE   "lumina_net"
#define NVS_KEY_BROKER  "mqtt_uri"

#define LWT_PAYLOAD "{\"online\":false}"

static esp_mqtt_client_handle_t s_client = NULL;
static volatile bool s_connected = false;

static char s_topic_cmd[48];   // lumina-<mac>/cmd
static char s_topic_info[48];  // lumina-<mac>/info

// ----------------------------------------------------------------------------
// Broker URI storage
// ----------------------------------------------------------------------------
void netMqttGetBrokerUri(char* buf, size_t size) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t len = size;
        if (nvs_get_str(h, NVS_KEY_BROKER, buf, &len) == ESP_OK && buf[0] != '\0') {
            nvs_close(h);
            return;
        }
        nvs_close(h);
    }
    strlcpy(buf, MQTT_DEFAULT_BROKER_URI, size);
}

esp_err_t netMqttSaveBrokerUri(const char* uri) {
    if (uri == NULL || strlen(uri) < 6 || strlen(uri) > 120) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, NVS_KEY_BROKER, uri);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "broker URI stored: %s", uri);
    }
    return err;
}

// ----------------------------------------------------------------------------
// Info message
// ----------------------------------------------------------------------------
void netMqttPublishInfo(void) {
    if (!s_connected || s_client == NULL) return;

    char state[128];
    appCtlWriteStateJson(state, sizeof(state));

    char ip[16];
    char ssid[33];
    char broker[128];
    netWifiGetIp(ip, sizeof(ip));
    netWifiGetSsid(ssid, sizeof(ssid));
    netMqttGetBrokerUri(broker, sizeof(broker));

    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"online\":true,\"device\":\"%s\",\"fw\":\"%s\",\"mac\":\"%s\","
             "\"ip\":\"%s\",\"ssid\":\"%s\",\"rssi\":%d,\"uptime_s\":%lu,"
             "\"broker\":\"%s\",%s}",
             FIRMWARE_NAME, FIRMWARE_VERSION, netWifiGetMacId(),
             ip, ssid, netWifiGetRssi(),
             (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000),
             broker, state);

    esp_mqtt_client_publish(s_client, s_topic_info, payload, 0, 1, 1 /* retain */);
    ESP_LOGD(TAG, "info published");
}

// ----------------------------------------------------------------------------
// Command handling
// ----------------------------------------------------------------------------
static void handle_command(const char* data, int len) {
    cJSON* root = cJSON_ParseWithLength(data, len);
    if (root == NULL) {
        ESP_LOGW(TAG, "invalid cmd JSON");
        return;
    }

    const cJSON* item;

    if ((item = cJSON_GetObjectItem(root, "power")) != NULL) {
        if (cJSON_IsBool(item))        appCtlSetPower(cJSON_IsTrue(item));
        else if (cJSON_IsNumber(item)) appCtlSetPower(item->valueint != 0);
    }
    if (cJSON_IsString(item = cJSON_GetObjectItem(root, "color"))) {
        appCtlSetColorHex(item->valuestring);
    }
    if (cJSON_IsNumber(item = cJSON_GetObjectItem(root, "brightness"))) {
        appCtlSetBrightnessPct(item->valueint);
    }
    if (cJSON_IsString(item = cJSON_GetObjectItem(root, "effect"))) {
        appCtlSetEffectByName(item->valuestring);
    }
    if (cJSON_IsString(item = cJSON_GetObjectItem(root, "buzzer"))) {
        appCtlPlayBuzzerByName(item->valuestring);
    }
    if (cJSON_IsTrue(cJSON_GetObjectItem(root, "magia"))) {
        appCtlMagiaStart();
    }
    if (cJSON_IsString(item = cJSON_GetObjectItem(root, "cnmode"))) {
        appCtlSetCenterModeByName(item->valuestring);
    }
    if (cJSON_GetObjectItem(root, "get") != NULL) {
        netMqttPublishInfo();  // explicit state request
    }

    cJSON_Delete(root);
    // State changes are picked up by the main loop (appCtlConsumeStateChanged)
    // which publishes a fresh info message.
}

// ----------------------------------------------------------------------------
// MQTT events
// ----------------------------------------------------------------------------
static void mqtt_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)data;

    switch ((esp_mqtt_event_id_t)id) {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            esp_mqtt_client_subscribe(s_client, s_topic_cmd, 1);
            ESP_LOGI(TAG, "connected - listening on %s", s_topic_cmd);
            netMqttPublishInfo();
            break;

        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "disconnected (auto-reconnect)");
            break;

        case MQTT_EVENT_DATA:
            if (event->topic_len == strlen(s_topic_cmd) &&
                strncmp(event->topic, s_topic_cmd, event->topic_len) == 0) {
                handle_command(event->data, event->data_len);
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "transport error");
            break;

        default:
            break;
    }
}

// ----------------------------------------------------------------------------
// Start / stop
// ----------------------------------------------------------------------------
esp_err_t netMqttStart(void) {
    if (s_client != NULL) return ESP_OK;

    const char* mac_id = netWifiGetMacId();
    snprintf(s_topic_cmd, sizeof(s_topic_cmd), MQTT_TOPIC_PREFIX "%s/cmd", mac_id);
    snprintf(s_topic_info, sizeof(s_topic_info), MQTT_TOPIC_PREFIX "%s/info", mac_id);

    char broker[128];
    netMqttGetBrokerUri(broker, sizeof(broker));

    char client_id[32];
    snprintf(client_id, sizeof(client_id), MQTT_TOPIC_PREFIX "%s", mac_id);

    esp_mqtt_client_config_t config = {};
    config.broker.address.uri = broker;
    config.credentials.client_id = client_id;
    config.session.last_will.topic = s_topic_info;
    config.session.last_will.msg = LWT_PAYLOAD;
    config.session.last_will.msg_len = strlen(LWT_PAYLOAD);
    config.session.last_will.qos = 1;
    config.session.last_will.retain = 1;
    config.session.keepalive = 30;

    s_client = esp_mqtt_client_init(&config);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "client init failed");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "client start failed: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return err;
    }

    ESP_LOGI(TAG, "connecting to %s as %s", broker, client_id);
    return ESP_OK;
}

void netMqttStop(void) {
    if (s_client == NULL) return;
    s_connected = false;
    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    ESP_LOGI(TAG, "stopped");
}

bool netMqttIsConnected(void) {
    return s_connected;
}
