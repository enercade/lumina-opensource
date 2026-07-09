// hw_btns.cpp - Hardware Buttons Control Implementation
#include "hw_btns.h"
#include "config.h"  // Include main config for pin definitions and constants
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

// Task configuration
#define BUTTON_TASK_STACK_SIZE TASK_STACK_SIZE_SMALL
#define BUTTON_TASK_PRIORITY TASK_PRIORITY_LOW

// GPIO pin mapping
static const gpio_num_t BUTTON_PINS[BUTTON_COUNT] = {
    SW1_PIN,    // BUTTON_SW1
    SW2_PIN,    // BUTTON_SW2  
    CN1_PIN,    // BUTTON_CN1
    CN2_PIN     // BUTTON_CN2
};

// Button names for debugging
static const char* BUTTON_NAMES[BUTTON_COUNT] = {
    "SW1",
    "SW2", 
    "CN1",
    "CN2"
};

// Event names for debugging
static const char* EVENT_NAMES[] = {
    "NONE",
    "PRESS",
    "RELEASE", 
    "LONG_PRESS"
};

// Internal state
static bool module_initialized = false;
static bool task_running = false;
static TaskHandle_t button_task_handle = NULL;
static QueueHandle_t event_queue = NULL;

// Button states
static button_state_t button_states[BUTTON_COUNT];
static button_debug_t button_debug[BUTTON_COUNT];  // Debug information
static uint32_t scan_count = 0;
static uint32_t event_count = 0;

// Internal functions
static void button_task(void* parameter);
static esp_err_t configure_gpio_pins(void);
static void scan_buttons(void);
static bool read_button_raw(button_id_t button_id);
static void process_button(button_id_t button_id, bool current_state);
static void send_event(button_id_t button_id, button_event_type_t event_type, uint32_t press_duration);
static uint32_t get_time_ms(void);

// =============== CORE FUNCTIONS ===============

esp_err_t hwBtnsInit(void) {
    esp_err_t result = ESP_OK;
    bool queue_created = false;
    
    if (!module_initialized) {
        // Initialize button states
        memset(button_states, 0, sizeof(button_states));
        memset(button_debug, 0, sizeof(button_debug));  // Initialize debug info
        scan_count = 0;
        event_count = 0;
        
        // Configure GPIO pins
        result = configure_gpio_pins();
        if (result == ESP_OK) {
            // Create event queue
            event_queue = xQueueCreate(BUTTON_EVENT_QUEUE_SIZE, sizeof(button_event_t));
            if (event_queue != NULL) {
                queue_created = true;
                
                // Create button scanning task
                BaseType_t task_result = xTaskCreate(
                    button_task,
                    "hw_btns",
                    BUTTON_TASK_STACK_SIZE,
                    NULL,
                    BUTTON_TASK_PRIORITY,
                    &button_task_handle
                );
                
                if (task_result == pdPASS) {
                    task_running = true;
                    module_initialized = true;
                } else {
                    result = ESP_ERR_NO_MEM;
                }
            } else {
                result = ESP_ERR_NO_MEM;
            }
            
            // Cleanup on failure
            if (result != ESP_OK && queue_created) {
                vQueueDelete(event_queue);
                event_queue = NULL;
            }
        }
    } else {
        result = ESP_ERR_INVALID_STATE;
    }
    
    return result;
}

esp_err_t hwBtnsDestroy(void) {
    esp_err_t result = ESP_OK;
    
    if (!module_initialized) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        // Stop task
        if (button_task_handle != NULL) {
            vTaskDelete(button_task_handle);
            button_task_handle = NULL;
        }
        
        // Cleanup queue
        if (event_queue != NULL) {
            vQueueDelete(event_queue);
            event_queue = NULL;
        }
        
        // Reset state
        task_running = false;
        module_initialized = false;
        memset(button_states, 0, sizeof(button_states));
    }
    
    return result;
}

bool hwBtnsIsReady(void) {
    return module_initialized && task_running;
}

buttons_status_t hwBtnsGetStatus(void) {
    buttons_status_t status;
    
    memset(&status, 0, sizeof(status));
    status.initialized = module_initialized;
    status.task_running = task_running;
    status.scan_count = scan_count;
    status.event_count = event_count;
    
    // Copy button states
    memcpy(status.buttons, button_states, sizeof(button_states));
    
    return status;
}

// =============== EVENT FUNCTIONS ===============

bool hwBtnsHasEvent(void) {
    bool has_event = false;
    
    if (event_queue != NULL) {
        has_event = (uxQueueMessagesWaiting(event_queue) > 0);
    }
    
    return has_event;
}

button_event_t hwBtnsGetEvent(void) {
    button_event_t event;
    BaseType_t result;
    
    // Initialize event with defaults
    memset(&event, 0, sizeof(event));
    event.event_type = BUTTON_EVENT_NONE;
    
    if (event_queue != NULL) {
        result = xQueueReceive(event_queue, &event, 0);
        if (result != pdPASS) {
            event.event_type = BUTTON_EVENT_NONE;
        }
    }
    
    return event;
}

void hwBtnsClearEvents(void) {
    if (event_queue != NULL) {
        xQueueReset(event_queue);
    }
}

// =============== STATE FUNCTIONS ===============

button_state_t hwBtnsGetState(button_id_t button_id) {
    button_state_t state;
    
    memset(&state, 0, sizeof(state));
    
    if (button_id >= 0 && button_id < BUTTON_COUNT) {
        memcpy(&state, &button_states[button_id], sizeof(state));
    }
    
    return state;
}

bool hwBtnsIsPressed(button_id_t button_id) {
    bool pressed = false;
    
    if (button_id >= 0 && button_id < BUTTON_COUNT) {
        pressed = button_states[button_id].pressed;
    }
    
    return pressed;
}

bool hwBtnsIsLongPressed(button_id_t button_id) {
    bool long_pressed = false;
    
    if (button_id >= 0 && button_id < BUTTON_COUNT) {
        long_pressed = button_states[button_id].long_pressed;
    }
    
    return long_pressed;
}

// =============== UTILITY FUNCTIONS ===============

const char* hwBtnsGetButtonName(button_id_t button_id) {
    const char* name = "UNKNOWN";
    
    if (button_id >= 0 && button_id < BUTTON_COUNT) {
        name = BUTTON_NAMES[button_id];
    }
    
    return name;
}

const char* hwBtnsGetEventName(button_event_type_t event_type) {
    const char* name = "UNKNOWN";
    
    if (event_type >= 0 && event_type < (sizeof(EVENT_NAMES)/sizeof(EVENT_NAMES[0]))) {
        name = EVENT_NAMES[event_type];
    }
    
    return name;
}

// =============== INTERNAL FUNCTIONS ===============

static void button_task(void* parameter) {
    TickType_t last_wake_time;
    const TickType_t scan_period = pdMS_TO_TICKS(BUTTON_SCAN_INTERVAL_MS);
    
    // Initialize wake time
    last_wake_time = xTaskGetTickCount();
    
    while (1) {
        scan_buttons();
        
        // Wait for next scan period
        vTaskDelayUntil(&last_wake_time, scan_period);
    }
}

static esp_err_t configure_gpio_pins(void) {
    esp_err_t result = ESP_OK;
    gpio_config_t gpio_conf;
    int i;
    
    // Configure all button pins as inputs with pull-up
    memset(&gpio_conf, 0, sizeof(gpio_conf));
    gpio_conf.mode = GPIO_MODE_INPUT;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.intr_type = GPIO_INTR_DISABLE;
    
    for (i = 0; i < BUTTON_COUNT; i++) {
        gpio_conf.pin_bit_mask = (1ULL << BUTTON_PINS[i]);
        result = gpio_config(&gpio_conf);
        if (result != ESP_OK) {
            break;
        }
    }
    
    return result;
}

static void scan_buttons(void) {
    int i;
    bool current_state;
    
    scan_count++;
    
    for (i = 0; i < BUTTON_COUNT; i++) {
        current_state = read_button_raw((button_id_t)i);
        process_button((button_id_t)i, current_state);
    }
}

static bool read_button_raw(button_id_t button_id) {
    bool pressed = false;
    int level;
    
    if (button_id >= 0 && button_id < BUTTON_COUNT) {
        level = gpio_get_level(BUTTON_PINS[button_id]);
        pressed = (level == 0);  // Active low with pull-up
    }
    
    return pressed;
}

static void process_button(button_id_t button_id, bool current_state) {
    button_state_t* state;
    uint32_t current_time;
    uint32_t press_duration;
    uint32_t state_duration;
    bool debounced_press;
    bool debounced_release;
    bool long_press_detected;
    
    if (button_id >= 0 && button_id < BUTTON_COUNT) {
        state = &button_states[button_id];
        current_time = get_time_ms();
        
        // Handle press detection with improved debouncing
        debounced_press = false;
        if (current_state && !state->pressed) {
            // Button physically pressed
            if (state->press_time == 0) {
                // Start debounce timer
                state->press_time = current_time;
            } else {
                // Check if debounce period has passed
                state_duration = current_time - state->press_time;
                if (state_duration >= BUTTON_DEBOUNCE_MS) {
                    // Debounced press confirmed
                state->pressed = true;
                state->last_event_time = current_time;  // Record actual press time
                state->press_count++;
                debounced_press = true;
            }
        }
    } else if (!current_state && state->press_time != 0) {
        // Button released before debounce completed - this is a bounce!
        button_debug[button_id].bounce_count++;
        button_debug[button_id].last_bounce_time = current_time;
        state->press_time = 0;
    }
    
    // Handle release detection with improved debouncing
    debounced_release = false;
    press_duration = 0;
    if (!current_state && state->pressed) {
        // Button physically released - calculate actual press duration
        press_duration = current_time - state->last_event_time;
        
        // Ensure minimum press duration to avoid bounce-induced short presses
        if (press_duration >= BUTTON_MIN_PRESS_MS) {
            // Valid release - reset state
            state->pressed = false;
            state->long_pressed = false;
            state->press_time = 0;
            debounced_release = true;
            
            // Update debug statistics
            if (press_duration < button_debug[button_id].min_press_duration) {
                button_debug[button_id].min_press_duration = press_duration;
            }
            if (press_duration > button_debug[button_id].max_press_duration) {
                button_debug[button_id].max_press_duration = press_duration;
            }
        } else {
            // Press too short - count as bounce
            button_debug[button_id].short_press_count++;
        }
    }
    
    // Handle long press detection
    long_press_detected = false;
    if (state->pressed && !state->long_pressed) {
        press_duration = current_time - state->last_event_time;
        if (press_duration >= BUTTON_LONG_PRESS_MS) {
            state->long_pressed = true;
            long_press_detected = true;
        }
    }
    
        // Send events only for properly debounced actions
        if (debounced_press) {
            send_event(button_id, BUTTON_EVENT_PRESS, 0);
        }
        
        if (long_press_detected) {
            send_event(button_id, BUTTON_EVENT_LONG_PRESS, press_duration);
        }
        
        if (debounced_release) {
            send_event(button_id, BUTTON_EVENT_RELEASE, press_duration);
        }
    }
}

static void send_event(button_id_t button_id, button_event_type_t event_type, uint32_t press_duration) {
    button_event_t event;
    BaseType_t result;
    
    if (event_queue != NULL) {
        // Prepare event
        memset(&event, 0, sizeof(event));
        event.button_id = button_id;
        event.event_type = event_type;
        event.timestamp = get_time_ms();
        event.press_duration = press_duration;
        
        // Send to queue (non-blocking)
        result = xQueueSend(event_queue, &event, 0);
        if (result == pdPASS) {
            event_count++;
        }
    }
}

static uint32_t get_time_ms(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

// =============== DEBUG FUNCTIONS ===============

button_debug_t hwBtnsGetDebugInfo(button_id_t button_id) {
    button_debug_t debug;
    
    memset(&debug, 0, sizeof(debug));
    
    if (button_id >= 0 && button_id < BUTTON_COUNT) {
        memcpy(&debug, &button_debug[button_id], sizeof(debug));
    }
    
    return debug;
}

void hwBtnsResetDebugInfo(button_id_t button_id) {
    if (button_id >= 0 && button_id < BUTTON_COUNT) {
        memset(&button_debug[button_id], 0, sizeof(button_debug_t));
        // Reset min duration to max value so first measurement sets it correctly
        button_debug[button_id].min_press_duration = 0xFFFFFFFF;
    }
}
