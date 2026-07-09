// hw_btns.h - Hardware Buttons Control
#ifndef HW_BTNS_H
#define HW_BTNS_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"

// Button identifiers
typedef enum {
    BUTTON_SW1 = 0,
    BUTTON_SW2 = 1,
    BUTTON_CN1 = 2,
    BUTTON_CN2 = 3,
    BUTTON_COUNT = 4
} button_id_t;

// Button event types
typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_PRESS,
    BUTTON_EVENT_RELEASE,
    BUTTON_EVENT_LONG_PRESS
} button_event_type_t;

// Types
typedef struct {
    button_id_t button_id;
    button_event_type_t event_type;
    uint32_t timestamp;             // Event timestamp in ms
    uint32_t press_duration;        // Duration of press (for release events)
} button_event_t;

typedef struct {
    bool pressed;                   // Current pressed state
    bool long_pressed;              // Currently in long press
    uint32_t press_time;            // Time when pressed (0 if not pressed)
    uint32_t last_event_time;       // Last event timestamp
    uint32_t press_count;           // Total press count for this button
} button_state_t;

typedef struct {
    bool initialized;
    bool task_running;
    button_state_t buttons[BUTTON_COUNT];
    uint32_t scan_count;            // Total scans performed
    uint32_t event_count;           // Total events generated
} buttons_status_t;

// Core functions
esp_err_t hwBtnsInit(void);
esp_err_t hwBtnsDestroy(void);
bool hwBtnsIsReady(void);
buttons_status_t hwBtnsGetStatus(void);

// Event functions
bool hwBtnsHasEvent(void);
button_event_t hwBtnsGetEvent(void);
void hwBtnsClearEvents(void);

// State functions
button_state_t hwBtnsGetState(button_id_t button_id);
bool hwBtnsIsPressed(button_id_t button_id);
bool hwBtnsIsLongPressed(button_id_t button_id);

// Utility functions
const char* hwBtnsGetButtonName(button_id_t button_id);
const char* hwBtnsGetEventName(button_event_type_t event_type);

// Debug functions for bounce monitoring
typedef struct {
    uint32_t bounce_count;          // Number of bounces detected
    uint32_t short_press_count;     // Number of presses shorter than min duration
    uint32_t last_bounce_time;      // Last bounce timestamp
    uint32_t min_press_duration;    // Shortest press duration seen
    uint32_t max_press_duration;    // Longest press duration seen
} button_debug_t;

button_debug_t hwBtnsGetDebugInfo(button_id_t button_id);
void hwBtnsResetDebugInfo(button_id_t button_id);

#endif // HW_BTNS_H
