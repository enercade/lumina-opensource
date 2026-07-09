// app_control.h - Lumina application state: LED/buzzer commands, the "magia"
// flourish and the central-button mode. Single entry point for state changes
// coming from the physical buttons and from MQTT, so both stay consistent.
#ifndef APP_CONTROL_H
#define APP_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Action performed by the central button (CN1). Selected via MQTT.
typedef enum {
    CENTER_MODE_MAGIA = 0,   // Only the "magia" sparkle flourish
    CENTER_MODE_BUZZER,      // Only the buzzer
    CENTER_MODE_BOTH         // Buzzer + "magia" sparkle flourish
} center_btn_mode_t;

// Initialize internal state. Call after hwLedsInit()/hwBuzzInit().
void appCtlInit(void);

// Periodic tick from the main loop; drives the magia flourish timer.
void appCtlTick(void);

// --- Commands (each marks the state as changed) ---
void appCtlSetPower(bool on);
void appCtlSetColor(uint8_t r, uint8_t g, uint8_t b);
bool appCtlSetColorHex(const char* rrggbb);        // "RRGGBB" or "#RRGGBB"
void appCtlSetBrightnessPct(int pct);              // clamped to 0..100
void appCtlAdjustBrightnessPct(int delta_pct);     // beeps at the end stops
bool appCtlSetEffectByName(const char* name);      // fix|breath|halo|bounce|demo|magia
bool appCtlPlayBuzzerByName(const char* name);     // info|success|warning|error
void appCtlMagiaStart(void);                       // temporary sparkle flourish

// Central-button mode
void appCtlSetCenterMode(center_btn_mode_t mode);
bool appCtlSetCenterModeByName(const char* name);  // magia|buzzer|both
center_btn_mode_t appCtlGetCenterMode(void);
const char* appCtlGetCenterModeName(void);

// Run the central-button action according to the configured mode.
void appCtlCenterButtonPressed(void);

// True once after any state change; reading clears the flag. The main loop
// uses this to publish a fresh info message over MQTT.
bool appCtlConsumeStateChanged(void);

// Current LED state as a JSON fragment (no braces), e.g.
//   "power":1,"brightness":50,"color":"00AAFF","effect":"halo","cnmode":"both"
// Returns the number of characters written.
int appCtlWriteStateJson(char* out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif // APP_CONTROL_H
