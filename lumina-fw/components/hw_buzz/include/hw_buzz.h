// hw_buzz.h - Hardware Buzzer Control
#ifndef HW_BUZZ_H
#define HW_BUZZ_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"

// Buzzer pattern types
typedef enum {
    BUZZ_INFO = 0,
    BUZZ_SUCCESS,
    BUZZ_WARNING,
    BUZZ_ERROR,
    BUZZ_PATTERN_COUNT
} buzzer_pattern_t;

// Note structure for sound patterns
typedef struct {
    uint32_t frequency;             // Hz (0 = silence)
    uint32_t duration;              // milliseconds
} buzzer_note_t;

// Buzzer status structure
typedef struct {
    bool initialized;
    bool task_running;
    bool currently_playing;
    buzzer_pattern_t current_pattern;
    uint32_t play_count;
    uint32_t queue_size;            // Number of pending sounds
} buzzer_status_t;

// Core functions
esp_err_t hwBuzzInit(void);
esp_err_t hwBuzzDestroy(void);
bool hwBuzzIsReady(void);
buzzer_status_t hwBuzzGetStatus(void);

// Playback functions
esp_err_t hwBuzzPlayInfo(void);
esp_err_t hwBuzzPlaySuccess(void);
esp_err_t hwBuzzPlayWarning(void);
esp_err_t hwBuzzPlayError(void);
esp_err_t hwBuzzPlayPattern(buzzer_pattern_t pattern);

// Control functions
esp_err_t hwBuzzStop(void);
esp_err_t hwBuzzClearQueue(void);
bool hwBuzzIsPlaying(void);

// Utility functions
const char* hwBuzzGetPatternName(buzzer_pattern_t pattern);

#endif // HW_BUZZ_H
