// hw_buzz.cpp - Hardware Buzzer Control Implementation
#include "hw_buzz.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

// Task configuration
#define BUZZER_TASK_STACK_SIZE TASK_STACK_SIZE_SMALL
#define BUZZER_TASK_PRIORITY TASK_PRIORITY_LOW

// LEDC configuration for buzzer
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_8_BIT
#define LEDC_DUTY_50_PERCENT    (128)  // 50% duty cycle for 8-bit resolution

// Sound pattern definitions
static const buzzer_note_t PATTERN_INFO[] = {
    {1500, 150},      // Single medium beep
    {0, 0}            // End marker
};

static const buzzer_note_t PATTERN_SUCCESS[] = {
    {800, 100},       // Low tone
    {1200, 100},      // Mid tone  
    {1600, 150},      // High tone
    {0, 0}            // End marker
};

static const buzzer_note_t PATTERN_WARNING[] = {
    {1000, 200},      // First warning
    {0, 100},         // Pause
    {1500, 200},      // Second warning
    {0, 0}            // End marker
};

static const buzzer_note_t PATTERN_ERROR[] = {
    {2000, 100},      // High harsh
    {1000, 100},      // Mid harsh
    {500, 200},       // Low harsh
    {0, 100},         // Pause
    {500, 200},       // Repeat low
    {0, 0}            // End marker
};

// Pattern lookup table
static const buzzer_note_t* SOUND_PATTERNS[BUZZ_PATTERN_COUNT] = {
    PATTERN_INFO,
    PATTERN_SUCCESS,
    PATTERN_WARNING,
    PATTERN_ERROR
};

// Pattern names for debugging
static const char* PATTERN_NAMES[BUZZ_PATTERN_COUNT] = {
    "INFO",
    "SUCCESS",
    "WARNING",
    "ERROR"
};

// Internal state
static bool module_initialized = false;
static bool task_running = false;
static bool currently_playing = false;
static TaskHandle_t buzzer_task_handle = NULL;
static QueueHandle_t buzzer_queue = NULL;
static buzzer_pattern_t current_pattern = BUZZ_INFO;
static uint32_t play_count = 0;

// Internal functions
static void buzzer_task(void* parameter);
static esp_err_t configure_ledc(void);
static void play_pattern(const buzzer_note_t* pattern);
static void play_note(uint32_t frequency, uint32_t duration);
static void buzzer_stop_sound(void);
static uint32_t get_time_ms(void);

// =============== CORE FUNCTIONS ===============

esp_err_t hwBuzzInit(void) {
    esp_err_t result = ESP_OK;
    bool queue_created = false;
    
    if (!module_initialized) {
        // Initialize state
        currently_playing = false;
        play_count = 0;
        current_pattern = BUZZ_INFO;
        
        // Configure LEDC for PWM generation
        result = configure_ledc();
        if (result == ESP_OK) {
            // Create pattern queue
            buzzer_queue = xQueueCreate(BUZZER_QUEUE_SIZE, sizeof(buzzer_pattern_t));
            if (buzzer_queue != NULL) {
                queue_created = true;
                
                // Create buzzer task
                BaseType_t task_result = xTaskCreate(
                    buzzer_task,
                    "hw_buzz",
                    BUZZER_TASK_STACK_SIZE,
                    NULL,
                    BUZZER_TASK_PRIORITY,
                    &buzzer_task_handle
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
                vQueueDelete(buzzer_queue);
                buzzer_queue = NULL;
            }
        }
    } else {
        result = ESP_ERR_INVALID_STATE;
    }
    
    return result;
}

esp_err_t hwBuzzDestroy(void) {
    esp_err_t result = ESP_OK;
    
    if (module_initialized) {
        // Stop any playing sound
        buzzer_stop_sound();
        
        // Stop task
        if (buzzer_task_handle != NULL) {
            vTaskDelete(buzzer_task_handle);
            buzzer_task_handle = NULL;
        }
        
        // Cleanup queue
        if (buzzer_queue != NULL) {
            vQueueDelete(buzzer_queue);
            buzzer_queue = NULL;
        }
        
        // Stop LEDC
        ledc_stop(LEDC_MODE, LEDC_CHANNEL, 0);
        
        // Reset state
        task_running = false;
        module_initialized = false;
        currently_playing = false;
    } else {
        result = ESP_ERR_INVALID_STATE;
    }
    
    return result;
}

bool hwBuzzIsReady(void) {
    return module_initialized && task_running;
}

buzzer_status_t hwBuzzGetStatus(void) {
    buzzer_status_t status;
    
    memset(&status, 0, sizeof(status));
    status.initialized = module_initialized;
    status.task_running = task_running;
    status.currently_playing = currently_playing;
    status.current_pattern = current_pattern;
    status.play_count = play_count;
    
    if (buzzer_queue != NULL) {
        status.queue_size = uxQueueMessagesWaiting(buzzer_queue);
    }
    
    return status;
}

// =============== PLAYBACK FUNCTIONS ===============

esp_err_t hwBuzzPlayInfo(void) {
    return hwBuzzPlayPattern(BUZZ_INFO);
}

esp_err_t hwBuzzPlaySuccess(void) {
    return hwBuzzPlayPattern(BUZZ_SUCCESS);
}

esp_err_t hwBuzzPlayWarning(void) {
    return hwBuzzPlayPattern(BUZZ_WARNING);
}

esp_err_t hwBuzzPlayError(void) {
    return hwBuzzPlayPattern(BUZZ_ERROR);
}

esp_err_t hwBuzzPlayPattern(buzzer_pattern_t pattern) {
    esp_err_t result = ESP_OK;
    BaseType_t queue_result;
    
    if (module_initialized && buzzer_queue != NULL && pattern < BUZZ_PATTERN_COUNT) {
        // Send pattern to queue (non-blocking)
        queue_result = xQueueSend(buzzer_queue, &pattern, 0);
        if (queue_result != pdPASS) {
            result = ESP_ERR_NO_MEM;  // Queue full
        }
    } else if (!module_initialized || buzzer_queue == NULL) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        result = ESP_ERR_INVALID_ARG;
    }
    
    return result;
}

// =============== CONTROL FUNCTIONS ===============

esp_err_t hwBuzzStop(void) {
    esp_err_t result = ESP_OK;
    
    if (module_initialized) {
        buzzer_stop_sound();
    } else {
        result = ESP_ERR_INVALID_STATE;
    }
    
    return result;
}

esp_err_t hwBuzzClearQueue(void) {
    esp_err_t result = ESP_OK;
    
    if (module_initialized && buzzer_queue != NULL) {
        xQueueReset(buzzer_queue);
    } else {
        result = ESP_ERR_INVALID_STATE;
    }
    
    return result;
}

bool hwBuzzIsPlaying(void) {
    return currently_playing;
}

// =============== UTILITY FUNCTIONS ===============

const char* hwBuzzGetPatternName(buzzer_pattern_t pattern) {
    const char* name = "UNKNOWN";
    
    if (pattern >= 0 && pattern < BUZZ_PATTERN_COUNT) {
        name = PATTERN_NAMES[pattern];
    }
    
    return name;
}

// =============== INTERNAL FUNCTIONS ===============

static void buzzer_task(void* parameter) {
    buzzer_pattern_t pattern;
    
    while (1) {
        // Wait for pattern in queue (blocking)
        if (xQueueReceive(buzzer_queue, &pattern, portMAX_DELAY) == pdPASS) {
            if (pattern < BUZZ_PATTERN_COUNT) {
                currently_playing = true;
                current_pattern = pattern;
                play_count++;
                
                // Play the pattern
                play_pattern(SOUND_PATTERNS[pattern]);
                
                currently_playing = false;
            }
        }
    }
}

static esp_err_t configure_ledc(void) {
    esp_err_t result = ESP_OK;
    ledc_timer_config_t ledc_timer;
    ledc_channel_config_t ledc_channel;
    
    // Configure LEDC timer
    memset(&ledc_timer, 0, sizeof(ledc_timer));
    ledc_timer.duty_resolution = LEDC_DUTY_RES;
    ledc_timer.freq_hz = BUZZER_PWM_FREQUENCY;
    ledc_timer.speed_mode = LEDC_MODE;
    ledc_timer.timer_num = LEDC_TIMER;
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;
    
    result = ledc_timer_config(&ledc_timer);
    if (result == ESP_OK) {
        // Configure LEDC channel
        memset(&ledc_channel, 0, sizeof(ledc_channel));
        ledc_channel.channel = LEDC_CHANNEL;
        ledc_channel.duty = 0;
        ledc_channel.gpio_num = BUZZER_PIN;
        ledc_channel.speed_mode = LEDC_MODE;
        ledc_channel.timer_sel = LEDC_TIMER;
        
        result = ledc_channel_config(&ledc_channel);
    }
    
    return result;
}

static void play_pattern(const buzzer_note_t* pattern) {
    int note_index = 0;
    
    while (pattern[note_index].frequency != 0 || pattern[note_index].duration != 0) {
        play_note(pattern[note_index].frequency, pattern[note_index].duration);
        note_index++;
        
        // Small gap between notes
        if (pattern[note_index].frequency != 0 || pattern[note_index].duration != 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    
    // Ensure buzzer is off at end
    buzzer_stop_sound();
}

static void play_note(uint32_t frequency, uint32_t duration) {
    if (frequency == 0) {
        // Silence - stop output and wait
        buzzer_stop_sound();
        vTaskDelay(pdMS_TO_TICKS(duration));
    } else {
        // Set frequency and start tone
        ledc_set_freq(LEDC_MODE, LEDC_TIMER, frequency);
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY_50_PERCENT);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        
        // Play for specified duration
        vTaskDelay(pdMS_TO_TICKS(duration));
    }
}

static void buzzer_stop_sound(void) {
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

