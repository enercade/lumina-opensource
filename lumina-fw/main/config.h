// config.h - Lumina firmware configuration (ESP32-C6, Rev C00)
//
// Hardware pins, timings and network defaults. No secrets live here:
// WiFi credentials are provisioned at runtime (setup hotspot or WPS) and
// stored in NVS. For development convenience an optional, git-ignored
// wifi_dev_credentials.h can pre-seed them (see the .example.h template).
#ifndef CONFIG_H
#define CONFIG_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"

// Firmware info
#define FIRMWARE_NAME "lumina"
#define FIRMWARE_VERSION "2.0.0"
#define FIRMWARE_BUILD_DATE __DATE__
#define FIRMWARE_BUILD_TIME __TIME__

// ============================================================================
// LED hardware (required by the hw_leds component)
// ============================================================================
#define LED_PIN GPIO_NUM_5          // WS2812 data line
#define NUM_LEDS 12                 // Number of LEDs on the ring
#define LED_ENABLE_PIN GPIO_NUM_20  // LED power supply enable (Rev C00, active low)
#define LED_EFFECT_INTERVAL_MS 20   // 50 FPS animation tick

// ============================================================================
// Buttons (Rev C00)
// ============================================================================
// SW1 = brightness +, SW2 = AUX (short: brightness -, long: WPS, very long:
// clear WiFi credentials), CN1 = central button, CN2 = spare connector button.
#define SW1_PIN GPIO_NUM_0          // Brightness +
#define SW2_PIN GPIO_NUM_9          // AUX: brightness - / WPS / WiFi reset
#define CN1_PIN GPIO_NUM_6          // Central button (magia / buzzer / both)
#define CN2_PIN GPIO_NUM_7          // Connector button (unused for now)

#define BUTTON_DEBOUNCE_MS 50           // Debounce window
#define BUTTON_MIN_PRESS_MS 75          // Minimum press duration to be valid
#define BUTTON_LONG_PRESS_MS 3000       // Long press threshold
#define BUTTON_SCAN_INTERVAL_MS 10      // Scan period
#define BUTTON_EVENT_QUEUE_SIZE 8       // Max queued button events

// AUX button (SW2) gesture thresholds, evaluated on release
#define AUX_WPS_PRESS_MS 3000           // 3s..10s  -> start WPS pairing
#define AUX_RESET_PRESS_MS 10000        // >= 10s   -> clear WiFi creds, setup mode

// ============================================================================
// Buzzer (Rev C00, PWM via LEDC)
// ============================================================================
#define BUZZER_PIN GPIO_NUM_23          // Passive buzzer
#define BUZZER_QUEUE_SIZE 8             // Max queued sound patterns
#define BUZZER_PWM_RESOLUTION 8         // 8-bit PWM resolution
#define BUZZER_PWM_FREQUENCY 5000       // Base PWM frequency (Hz)

// Brightness step applied by the SW1/SW2 short presses (percent)
#define BRIGHTNESS_STEP_PCT 10
// Duration of the "magia" sparkle flourish before the previous state is restored
#define MAGIA_DURATION_MS 3000

// ============================================================================
// WiFi station (home network)
// ============================================================================
#define WIFI_STA_MAX_RETRY 8            // Failed connects before falling back
                                        // to the setup hotspot
#define WIFI_WPS_TIMEOUT_MS 120000      // WPS pairing window (2 minutes)

// ============================================================================
// Setup hotspot (provisioning portal)
// ============================================================================
// Open network named "<prefix><last 3 MAC bytes>", e.g. lumina-setup-a1b2c3.
// A captive portal at 192.168.4.1 lets the user enter WiFi credentials.
#define SETUP_AP_SSID_PREFIX "lumina-setup-"
#define SETUP_AP_CHANNEL     1
#define SETUP_AP_MAX_CONN    4
#define SETUP_HTTP_PORT      80

// ============================================================================
// MQTT (HiveMQ public broker by default; override from the setup portal)
// ============================================================================
// Topic root is "lumina-<mac>" (12 lowercase hex digits, no separators):
//   lumina-<mac>/cmd   commands to the device (JSON)
//   lumina-<mac>/info  retained state/config published by the device (JSON)
#define MQTT_DEFAULT_BROKER_URI "mqtt://broker.hivemq.com:1883"
#define MQTT_TOPIC_PREFIX       "lumina-"
#define MQTT_INFO_PERIOD_MS     60000   // Periodic info refresh (uptime/RSSI)

// ============================================================================
// FreeRTOS
// ============================================================================
#define TASK_PRIORITY_HIGH 3
#define TASK_PRIORITY_NORMAL 2
#define TASK_PRIORITY_LOW 1
#define TASK_STACK_SIZE_SMALL 2048
#define TASK_STACK_SIZE_MEDIUM 4096

// ============================================================================
// Logging
// ============================================================================
#define TAG_MAIN "MAIN"
#define LOG_MAIN(format, ...) ESP_LOGI(TAG_MAIN, "- " format, ##__VA_ARGS__)
#define LOG_MAIN_ERROR(format, ...) ESP_LOGE(TAG_MAIN, "- " format, ##__VA_ARGS__)

// Math
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#endif // CONFIG_H
