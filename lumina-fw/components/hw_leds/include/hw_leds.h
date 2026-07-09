// hw_leds.h - LED hardware control
#ifndef HW_LEDS_H
#define HW_LEDS_H

#include "config.h"

// Effect configuration parameters
#define BREATH_SPEED 0.01f              // Animation speed for breathing effect
#define HALO_SPEED 0.008f               // Animation speed for halo movement
#define BOUNCE_SPEED 0.015f             // Animation speed for bounce movement
#define HALO_CENTER_BRIGHTNESS 1.0f     // Center LED brightness (100%)
#define HALO_RING1_BRIGHTNESS 0.85f     // First ring brightness (85%)
#define HALO_RING2_BRIGHTNESS 0.6f      // Second ring brightness (60%)
#define HALO_RING3_BRIGHTNESS 0.35f     // Third ring brightness (35%)
#define HALO_RING4_BRIGHTNESS 0.15f     // Fourth ring brightness (15%)
#define HALO_RING5_BRIGHTNESS 0.03f     // Fifth ring brightness (3%)
#define HALO_CENTER_RADIUS 0.5f         // Center zone radius
#define HALO_RING1_RADIUS 0.5f          // First ring outer radius
#define HALO_RING2_RADIUS 1.0f          // Second ring outer radius
#define HALO_RING3_RADIUS 2.0f          // Third ring outer radius
#define HALO_RING4_RADIUS 3.0f          // Fourth ring outer radius
#define HALO_RING5_RADIUS 4.0f          // Fifth ring outer radius (fade edge)

// Types
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_color_t;

typedef enum {
    LED_EFFECT_FIX,
    LED_EFFECT_BREATH,
    LED_EFFECT_HALO,
    LED_EFFECT_BOUNCE,
    LED_EFFECT_DEMO,    // Rotating showcase of multiple light effects
    LED_EFFECT_MAGIA    // Sparkling "magic" twinkles (random colored sparkles)
} led_effect_t;

typedef struct {
    bool initialized;
    bool task_running;
    bool enabled;
    uint8_t brightness;
    led_effect_t effect;
    led_color_t color;
    float animation_phase;
    uint32_t frame_count;
} led_status_t;

// Core functions
esp_err_t hwLedsInit(void);
esp_err_t hwLedsDestroy(void);
bool hwLedsIsReady(void);
led_status_t hwLedsGetStatus(void);

// Color control
void hwLedsSetColor(uint8_t r, uint8_t g, uint8_t b);
void hwLedsSetColorStruct(led_color_t color);
led_color_t hwLedsGetColor(void);

// Brightness control
void hwLedsSetBrightness(uint8_t brightness);
uint8_t hwLedsGetBrightness(void);

// Effect control
void hwLedsSetEffect(led_effect_t effect);
bool hwLedsSetEffectByName(const char* effect_name);
led_effect_t hwLedsGetEffect(void);
const char* hwLedsGetEffectName(led_effect_t effect);

// Power control
void hwLedsSetEnabled(bool enabled);
void hwLedsToggle(void);
void hwLedsOff(void);
void hwLedsBlink(uint8_t r, uint8_t g, uint8_t b, int times);
void hwLedsShowSignalLevel(int leds_count, uint8_t r, uint8_t g, uint8_t b);  // Show N LEDs lit on ring
void hwLedsReleaseSignalOverride(void);  // Resume normal effect engine after signal level mode

// Utilities
led_color_t hwLedsCreateColor(uint8_t r, uint8_t g, uint8_t b);
led_color_t hwLedsApplyBrightness(led_color_t color, uint8_t brightness);

#endif // HW_LEDS_H