// app_control.cpp - Lumina application state (see app_control.h).
#include "app_control.h"
#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hw_leds.h"
#include "hw_buzz.h"

static const char* TAG = "APP_CTL";

static volatile center_btn_mode_t s_center_mode = CENTER_MODE_BOTH;
static volatile bool s_state_changed = false;

// "magia" flourish: a temporary sparkle that takes over the ring for
// MAGIA_DURATION_MS and then restores the previous LED state.
static bool         s_magia_active = false;
static uint32_t     s_magia_start_ms = 0;
static led_effect_t s_saved_effect;
static led_color_t  s_saved_color;
static uint8_t      s_saved_brightness;
static bool         s_saved_enabled;

static inline uint32_t now_ms(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void mark_changed(void) {
    s_state_changed = true;
}

void appCtlInit(void) {
    s_center_mode = CENTER_MODE_BOTH;
    s_magia_active = false;
    s_state_changed = false;
}

void appCtlTick(void) {
    if (s_magia_active && (now_ms() - s_magia_start_ms >= MAGIA_DURATION_MS)) {
        hwLedsSetColor(s_saved_color.r, s_saved_color.g, s_saved_color.b);
        hwLedsSetBrightness(s_saved_brightness);
        hwLedsSetEffect(s_saved_effect);
        hwLedsSetEnabled(s_saved_enabled);
        s_magia_active = false;
        mark_changed();
        ESP_LOGI(TAG, "Magia flourish ended - previous state restored");
    }
}

void appCtlSetPower(bool on) {
    hwLedsSetEnabled(on);
    mark_changed();
    ESP_LOGI(TAG, "power -> %s", on ? "ON" : "OFF");
}

void appCtlSetColor(uint8_t r, uint8_t g, uint8_t b) {
    hwLedsSetColor(r, g, b);
    mark_changed();
    ESP_LOGI(TAG, "color -> %02X%02X%02X", r, g, b);
}

// Parse 6 hex chars "RRGGBB" (optionally prefixed with '#').
bool appCtlSetColorHex(const char* rrggbb) {
    if (rrggbb == NULL) return false;
    if (rrggbb[0] == '#') rrggbb++;
    if (strlen(rrggbb) < 6) return false;

    char buf[3] = {0};
    uint8_t rgb[3];
    for (int i = 0; i < 3; i++) {
        buf[0] = rrggbb[i * 2];
        buf[1] = rrggbb[i * 2 + 1];
        rgb[i] = (uint8_t)strtol(buf, NULL, 16);
    }
    appCtlSetColor(rgb[0], rgb[1], rgb[2]);
    return true;
}

void appCtlSetBrightnessPct(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    hwLedsSetBrightness((uint8_t)((pct * 255) / 100));
    mark_changed();
    ESP_LOGI(TAG, "brightness -> %d%%", pct);
}

void appCtlAdjustBrightnessPct(int delta_pct) {
    int pct = (hwLedsGetBrightness() * 100) / 255;  // 0-255 -> 0-100%
    pct += delta_pct;
    if (pct >= 100 || pct <= 0) {
        hwBuzzPlayInfo();  // audible end-stop cue
    }
    appCtlSetBrightnessPct(pct);
}

bool appCtlSetEffectByName(const char* name) {
    if (name == NULL || !hwLedsSetEffectByName(name)) {
        ESP_LOGW(TAG, "unknown effect: %s", name ? name : "(null)");
        return false;
    }
    mark_changed();
    ESP_LOGI(TAG, "effect -> %s", name);
    return true;
}

bool appCtlPlayBuzzerByName(const char* name) {
    if (name == NULL) return false;
    if (strcmp(name, "info") == 0)    return hwBuzzPlayInfo() == ESP_OK;
    if (strcmp(name, "success") == 0) return hwBuzzPlaySuccess() == ESP_OK;
    if (strcmp(name, "warning") == 0) return hwBuzzPlayWarning() == ESP_OK;
    if (strcmp(name, "error") == 0)   return hwBuzzPlayError() == ESP_OK;
    ESP_LOGW(TAG, "unknown buzzer pattern: %s", name);
    return false;
}

void appCtlMagiaStart(void) {
    if (!s_magia_active) {
        // Save the current state so we can restore it when the flourish ends.
        led_status_t st = hwLedsGetStatus();
        s_saved_effect     = st.effect;
        s_saved_color      = st.color;
        s_saved_brightness = st.brightness;
        s_saved_enabled    = st.enabled;
    }
    // (Re)start the flourish - make sure it is actually visible.
    hwLedsSetEnabled(true);
    if (hwLedsGetBrightness() < 64) {
        hwLedsSetBrightness(180);
    }
    hwLedsSetEffect(LED_EFFECT_MAGIA);
    s_magia_active   = true;
    s_magia_start_ms = now_ms();
    mark_changed();
    ESP_LOGI(TAG, "Magia flourish started");
}

void appCtlSetCenterMode(center_btn_mode_t mode) {
    s_center_mode = mode;
    mark_changed();
    ESP_LOGI(TAG, "cnmode -> %s", appCtlGetCenterModeName());
}

bool appCtlSetCenterModeByName(const char* name) {
    if (name == NULL) return false;
    if (strcmp(name, "magia") == 0)  { appCtlSetCenterMode(CENTER_MODE_MAGIA);  return true; }
    if (strcmp(name, "buzzer") == 0) { appCtlSetCenterMode(CENTER_MODE_BUZZER); return true; }
    if (strcmp(name, "both") == 0)   { appCtlSetCenterMode(CENTER_MODE_BOTH);   return true; }
    ESP_LOGW(TAG, "unknown cnmode: %s", name);
    return false;
}

center_btn_mode_t appCtlGetCenterMode(void) {
    return s_center_mode;
}

const char* appCtlGetCenterModeName(void) {
    switch (s_center_mode) {
        case CENTER_MODE_MAGIA:  return "magia";
        case CENTER_MODE_BUZZER: return "buzzer";
        default:                 return "both";
    }
}

void appCtlCenterButtonPressed(void) {
    center_btn_mode_t mode = s_center_mode;
    ESP_LOGI(TAG, "Central button pressed (mode: %s)", appCtlGetCenterModeName());

    if (mode == CENTER_MODE_BUZZER || mode == CENTER_MODE_BOTH) {
        hwBuzzPlaySuccess();
    }
    if (mode == CENTER_MODE_MAGIA || mode == CENTER_MODE_BOTH) {
        appCtlMagiaStart();
    }
}

bool appCtlConsumeStateChanged(void) {
    bool changed = s_state_changed;
    s_state_changed = false;
    return changed;
}

int appCtlWriteStateJson(char* out, size_t out_size) {
    led_color_t c = hwLedsGetColor();
    int pct = (hwLedsGetBrightness() * 100) / 255;   // 0-255 -> 0-100%
    bool on = hwLedsGetStatus().enabled;
    return snprintf(out, out_size,
                    "\"power\":%d,\"brightness\":%d,\"color\":\"%02X%02X%02X\","
                    "\"effect\":\"%s\",\"cnmode\":\"%s\"",
                    on ? 1 : 0, pct, c.r, c.g, c.b,
                    hwLedsGetEffectName(hwLedsGetEffect()),
                    appCtlGetCenterModeName());
}
