// main.cpp - Lumina firmware entry point.
//
// Lumina joins the home WiFi network (provisioned via setup hotspot or WPS)
// and exposes itself over MQTT on lumina-<mac>/cmd + lumina-<mac>/info.
//
// Physical controls (Rev A00):
//   - SW1 (GPIO0): brightness +
//   - SW2 (GPIO9): AUX button
//       short press          -> brightness -
//       long press  (3-10s)  -> WPS push-button pairing
//       very long   (>= 10s) -> clear WiFi credentials, restart in setup mode
//   - CN1 (GPIO6): central button -> action selectable over MQTT
//       (magia sparkle / buzzer / both)
//
// The LED ring doubles as an ambient network indicator until connected:
//   setup hotspot = breathing amber, connecting = breathing blue,
//   WPS window = breathing violet, connected = halo light blue.
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nvs_flash.h"

#include "hw_leds.h"
#include "hw_btns.h"
#include "hw_buzz.h"
#include "app_control.h"
#include "net_wifi.h"
#include "net_mqtt.h"

// Ambient LED feedback for the network states (only until connected; once
// online the ring belongs to the user/MQTT).
static void show_network_state(net_wifi_state_t state) {
    switch (state) {
        case NET_WIFI_STATE_SETUP:       // amber: waiting for configuration
            hwLedsSetColor(255, 120, 0);
            hwLedsSetEffect(LED_EFFECT_BREATH);
            hwLedsSetEnabled(true);
            break;
        case NET_WIFI_STATE_CONNECTING:  // blue: joining the home network
            hwLedsSetColor(0, 90, 255);
            hwLedsSetEffect(LED_EFFECT_BREATH);
            hwLedsSetEnabled(true);
            break;
        case NET_WIFI_STATE_WPS:         // violet: WPS window open
            hwLedsSetColor(160, 60, 255);
            hwLedsSetEffect(LED_EFFECT_BREATH);
            hwLedsSetEnabled(true);
            break;
        case NET_WIFI_STATE_CONNECTED:   // light blue halo: ready
            hwLedsSetColor(0, 170, 255);
            hwLedsSetBrightness(128);
            hwLedsSetEffect(LED_EFFECT_HALO);
            hwLedsSetEnabled(true);
            break;
        default:
            break;
    }
}

// WiFi state transitions (called from the event task).
static void on_wifi_state(net_wifi_state_t state) {
    show_network_state(state);

    switch (state) {
        case NET_WIFI_STATE_CONNECTED:
            hwBuzzPlaySuccess();
            netMqttStart();
            break;
        case NET_WIFI_STATE_SETUP:
            netMqttStop();
            hwBuzzPlayWarning();
            break;
        case NET_WIFI_STATE_WPS:
            hwBuzzPlayInfo();
            break;
        default:
            break;
    }
}

// AUX button (SW2), evaluated on release: the press duration selects
// brightness- / WPS / credentials reset (see the header comment).
static void handle_aux_release(uint32_t press_duration_ms) {
    if (press_duration_ms >= AUX_RESET_PRESS_MS) {
        LOG_MAIN("AUX very long press - clearing WiFi credentials");
        hwBuzzPlayError();
        netWifiClearCredentialsAndRestart();          // does not return
    } else if (press_duration_ms >= AUX_WPS_PRESS_MS) {
        LOG_MAIN("AUX long press - starting WPS");
        netWifiStartWps();
    } else {
        appCtlAdjustBrightnessPct(-BRIGHTNESS_STEP_PCT);
    }
}

static void handle_button_event(const button_event_t* ev) {
    switch (ev->button_id) {
        case BUTTON_SW1:
            if (ev->event_type == BUTTON_EVENT_PRESS) {
                appCtlAdjustBrightnessPct(+BRIGHTNESS_STEP_PCT);
            }
            break;

        case BUTTON_SW2:
            if (ev->event_type == BUTTON_EVENT_LONG_PRESS) {
                hwBuzzPlayInfo();  // 3s reached: releasing now starts WPS
            } else if (ev->event_type == BUTTON_EVENT_RELEASE) {
                handle_aux_release(ev->press_duration);
            }
            break;

        case BUTTON_CN1:
            if (ev->event_type == BUTTON_EVENT_PRESS) {
                appCtlCenterButtonPressed();
            }
            break;

        default:
            break;
    }
}

extern "C" void app_main(void) {
    LOG_MAIN("=== Lumina ===");
    LOG_MAIN("Version: v%s (build %s %s)", FIRMWARE_VERSION, FIRMWARE_BUILD_DATE, FIRMWARE_BUILD_TIME);

    // --- NVS (WiFi stack + stored configuration) ---
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // --- LED power supply enable (Rev A00: GPIO20, active low) ---
    gpio_config_t led_enable_conf = {
        .pin_bit_mask = (1ULL << LED_ENABLE_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_enable_conf);
    gpio_set_level(LED_ENABLE_PIN, 0);  // 0 = LEDs powered
    LOG_MAIN("LED power enabled (GPIO%d)", LED_ENABLE_PIN);

    // --- Hardware drivers ---
    ret = hwLedsInit();
    if (ret != ESP_OK) {
        LOG_MAIN_ERROR("LED init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = hwBtnsInit();
    if (ret != ESP_OK) {
        LOG_MAIN_ERROR("Button init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = hwBuzzInit();
    if (ret != ESP_OK) {
        LOG_MAIN_ERROR("Buzzer init failed: %s", esp_err_to_name(ret));
        return;
    }
    LOG_MAIN("Hardware ready: LEDs(GPIO%d) buttons(SW1=%d SW2=%d CN1=%d) buzzer(GPIO%d)",
             LED_PIN, SW1_PIN, SW2_PIN, CN1_PIN, BUZZER_PIN);

    appCtlInit();

    // --- Networking (WiFi manager decides: station vs setup hotspot) ---
    ESP_ERROR_CHECK(netWifiInit(on_wifi_state));
    ESP_ERROR_CHECK(netWifiStart());

    // --- Main loop: buttons, magia timer, MQTT info refresh ---
    uint32_t last_info_ms = 0;
    while (true) {
        while (hwBtnsHasEvent()) {
            button_event_t ev = hwBtnsGetEvent();
            handle_button_event(&ev);
        }

        appCtlTick();

        // Publish a fresh info message on any state change, plus a periodic
        // refresh so uptime/RSSI stay current.
        uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        bool changed = appCtlConsumeStateChanged();
        bool periodic = netMqttIsConnected() && (now - last_info_ms >= MQTT_INFO_PERIOD_MS);
        if (changed || periodic) {
            netMqttPublishInfo();
            last_info_ms = now;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
