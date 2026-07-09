// net_mqtt.h - MQTT link to the broker (HiveMQ public broker by default).
//
// Topics (topic root = "lumina-<mac>", 12 lowercase hex digits):
//   lumina-<mac>/cmd   subscribed; JSON commands, e.g.
//                      {"power":1,"color":"00AAFF","brightness":60,
//                       "effect":"halo","buzzer":"success","magia":true,
//                       "cnmode":"both","get":"info"}
//   lumina-<mac>/info  published (retained, QoS 1): full device state; also
//                      the last-will message {"online":false}.
#ifndef NET_MQTT_H
#define NET_MQTT_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Connect to the broker (call once WiFi is up). Idempotent.
esp_err_t netMqttStart(void);

// Tear down the client (e.g. when WiFi drops into setup mode).
void netMqttStop(void);

bool netMqttIsConnected(void);

// Publish the retained info message with the current state.
void netMqttPublishInfo(void);

// Broker URI stored in NVS; falls back to MQTT_DEFAULT_BROKER_URI.
void netMqttGetBrokerUri(char* buf, size_t size);
esp_err_t netMqttSaveBrokerUri(const char* uri);

#ifdef __cplusplus
}
#endif

#endif // NET_MQTT_H
