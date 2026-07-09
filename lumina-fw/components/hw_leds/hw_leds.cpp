// hw_leds.cpp - LED hardware implementation with native RMT
#include "hw_leds.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

// WS2812B timing constants (in nanoseconds)
#define WS2812_T0H_NS    (350)    // 0 bit high time
#define WS2812_T0L_NS    (1000)   // 0 bit low time
#define WS2812_T1H_NS    (1000)   // 1 bit high time
#define WS2812_T1L_NS    (350)    // 1 bit low time
#define WS2812_RESET_US  (280)    // Reset time in microseconds

// Internal module variables
static rmt_channel_handle_t rmt_chan = NULL;
static rmt_encoder_handle_t led_encoder = NULL;
static TaskHandle_t led_task_handle = NULL;
static bool module_initialized = false;
static bool task_running = false;
static volatile bool signal_override = false;  // When true, led_task skips rendering

// LED buffer (GRB format for WS2812B)
static uint8_t led_buffer[NUM_LEDS * 3];

// LED status structure
static led_status_t led_status = {
    .initialized = false,
    .task_running = false,
    .enabled = true,
    .brightness = 255,
    .effect = LED_EFFECT_FIX,
    .color = {255, 255, 255},
    .animation_phase = 0.0f,
    .frame_count = 0
};

// WS2812B encoder structure
typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder;
    rmt_encoder_t *copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
} rmt_led_strip_encoder_t;

// WS2812B encoder implementation
static size_t rmt_encode_led_strip(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                   const void *primary_data, size_t data_size,
                                   rmt_encode_state_t *ret_state) {
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_handle_t bytes_encoder = led_encoder->bytes_encoder;
    rmt_encoder_handle_t copy_encoder = led_encoder->copy_encoder;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    bool encoding_complete = false;
    
    if (led_encoder->state == 0) { // send RGB data
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = 1; // switch to next state when current encoding session finished
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state = (rmt_encode_state_t)(state | RMT_ENCODING_MEM_FULL);
            encoding_complete = true;
        }
    }
    
    if ((led_encoder->state == 1) && !encoding_complete) { // send reset code
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, &led_encoder->reset_code,
                                                sizeof(led_encoder->reset_code), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = RMT_ENCODING_RESET; // back to the initial encoding session
            state = (rmt_encode_state_t)(state | RMT_ENCODING_COMPLETE);
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state = (rmt_encode_state_t)(state | RMT_ENCODING_MEM_FULL);
        }
    }
    
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t rmt_del_led_strip_encoder(rmt_encoder_t *encoder) {
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_del_encoder(led_encoder->bytes_encoder);
    rmt_del_encoder(led_encoder->copy_encoder);
    free(led_encoder);
    return ESP_OK;
}

static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t *encoder) {
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_reset(led_encoder->bytes_encoder);
    rmt_encoder_reset(led_encoder->copy_encoder);
    led_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

// Create WS2812B encoder
static esp_err_t rmt_new_led_strip_encoder(rmt_encoder_handle_t *ret_encoder) {
    esp_err_t ret = ESP_ERR_NO_MEM;
    rmt_led_strip_encoder_t *led_encoder = NULL;
    rmt_bytes_encoder_config_t bytes_encoder_config;
    rmt_copy_encoder_config_t copy_encoder_config;
    uint32_t reset_ticks;
    bool encoder_created = false;
    bool copy_encoder_created = false;
    
    led_encoder = (rmt_led_strip_encoder_t *)calloc(1, sizeof(rmt_led_strip_encoder_t));
    if (led_encoder != NULL) {
        led_encoder->base.encode = rmt_encode_led_strip;
        led_encoder->base.del = rmt_del_led_strip_encoder;
        led_encoder->base.reset = rmt_led_strip_encoder_reset;
        
        // WS2812 timing configuration
        memset(&bytes_encoder_config, 0, sizeof(bytes_encoder_config));
        bytes_encoder_config.bit0.level0 = 1;
        bytes_encoder_config.bit0.duration0 = 3; // T0H=0.3us
        bytes_encoder_config.bit0.level1 = 0;
        bytes_encoder_config.bit0.duration1 = 9; // T0L=0.9us
        bytes_encoder_config.bit1.level0 = 1;
        bytes_encoder_config.bit1.duration0 = 9; // T1H=0.9us
        bytes_encoder_config.bit1.level1 = 0;
        bytes_encoder_config.bit1.duration1 = 3; // T1L=0.3us
        bytes_encoder_config.flags.msb_first = 1; // WS2812 transfer bit order: G7...G0R7...R0B7...B0
        
        ret = rmt_new_bytes_encoder(&bytes_encoder_config, &led_encoder->bytes_encoder);
        if (ret == ESP_OK) {
            encoder_created = true;
            
            memset(&copy_encoder_config, 0, sizeof(copy_encoder_config));
            ret = rmt_new_copy_encoder(&copy_encoder_config, &led_encoder->copy_encoder);
            if (ret == ESP_OK) {
                copy_encoder_created = true;
                
                reset_ticks = 10 * 50 / 2; // reset code duration defaults to 50us
                led_encoder->reset_code.level0 = 0;
                led_encoder->reset_code.duration0 = reset_ticks;
                led_encoder->reset_code.level1 = 0;
                led_encoder->reset_code.duration1 = reset_ticks;
                
                *ret_encoder = &led_encoder->base;
                ret = ESP_OK;
            }
        }
    }
    
    // Cleanup on error
    if (ret != ESP_OK) {
        if (led_encoder != NULL) {
            if (copy_encoder_created) {
                rmt_del_encoder(led_encoder->copy_encoder);
            }
            if (encoder_created) {
                rmt_del_encoder(led_encoder->bytes_encoder);
            }
            free(led_encoder);
        }
    }
    
    return ret;
}

// Set LED pixel in buffer
static void set_pixel(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= 0 && index < NUM_LEDS) {
        led_buffer[index * 3 + 0] = g; // WS2812B uses GRB order
        led_buffer[index * 3 + 1] = r;
        led_buffer[index * 3 + 2] = b;
    }
}

// Clear all LEDs
static void clear_all(void) {
    memset(led_buffer, 0, sizeof(led_buffer));
}

// Update LED strip
static esp_err_t update_strip(void) {
    rmt_transmit_config_t tx_config;
    memset(&tx_config, 0, sizeof(tx_config));
    tx_config.loop_count = 0; // no transfer loop
    
    return rmt_transmit(rmt_chan, led_encoder, led_buffer, sizeof(led_buffer), &tx_config);
}

// Effect implementations
static void apply_effect_fix(void) {
    led_color_t final_color = hwLedsApplyBrightness(led_status.color, led_status.brightness);
    
    for (int i = 0; i < NUM_LEDS; i++) {
        set_pixel(i, final_color.r, final_color.g, final_color.b);
    }
}

static void apply_effect_breath(void) {
    float breath_factor = (sin(led_status.animation_phase * 2 * M_PI) + 1.0f) / 2.0f;
    uint8_t breath_brightness = (uint8_t)(led_status.brightness * breath_factor);
    
    led_color_t final_color = hwLedsApplyBrightness(led_status.color, breath_brightness);
    
    for (int i = 0; i < NUM_LEDS; i++) {
        set_pixel(i, final_color.r, final_color.g, final_color.b);
    }
    
    led_status.animation_phase += BREATH_SPEED;
    if (led_status.animation_phase >= 1.0f) {
        led_status.animation_phase = 0.0f;
    }
}

static void apply_effect_halo(void) {
    clear_all();
    
    // Calculate continuous center position (can be between LEDs)
    float center_pos = led_status.animation_phase * NUM_LEDS;
    
    // Ultra-smooth halo effect with true continuous movement
    for (int i = 0; i < NUM_LEDS; i++) {
        // Calculate precise distance from the continuous center position
        float distance = fabsf((float)i - center_pos);
        
        // Handle wraparound for circular LED strip
        if (distance > NUM_LEDS / 2.0f) {
            distance = NUM_LEDS - distance;
        }
        
        // Calculate smooth brightness based on continuous distance using header constants
        float brightness_factor = 0.0f;
        
        if (distance <= HALO_CENTER_RADIUS) {
            // Center zone - configurable peak brightness
            brightness_factor = HALO_CENTER_BRIGHTNESS - (distance * (HALO_CENTER_BRIGHTNESS - HALO_RING1_BRIGHTNESS) / HALO_CENTER_RADIUS);
        } else if (distance <= HALO_RING1_RADIUS) {
            // First ring - smooth transition
            float ring_pos = (distance - HALO_CENTER_RADIUS) / (HALO_RING1_RADIUS - HALO_CENTER_RADIUS);
            brightness_factor = HALO_RING1_BRIGHTNESS - (ring_pos * (HALO_RING1_BRIGHTNESS - HALO_RING2_BRIGHTNESS));
        } else if (distance <= HALO_RING2_RADIUS) {
            // Second ring - smooth transition
            float ring_pos = (distance - HALO_RING1_RADIUS) / (HALO_RING2_RADIUS - HALO_RING1_RADIUS);
            brightness_factor = HALO_RING2_BRIGHTNESS - (ring_pos * (HALO_RING2_BRIGHTNESS - HALO_RING3_BRIGHTNESS));
        } else if (distance <= HALO_RING3_RADIUS) {
            // Third ring - smooth transition
            float ring_pos = (distance - HALO_RING2_RADIUS) / (HALO_RING3_RADIUS - HALO_RING2_RADIUS);
            brightness_factor = HALO_RING3_BRIGHTNESS - (ring_pos * (HALO_RING3_BRIGHTNESS - HALO_RING4_BRIGHTNESS));
        } else if (distance <= HALO_RING4_RADIUS) {
            // Fourth ring - smooth transition
            float ring_pos = (distance - HALO_RING3_RADIUS) / (HALO_RING4_RADIUS - HALO_RING3_RADIUS);
            brightness_factor = HALO_RING4_BRIGHTNESS - (ring_pos * (HALO_RING4_BRIGHTNESS - HALO_RING5_BRIGHTNESS));
        } else if (distance <= HALO_RING5_RADIUS) {
            // Fifth ring - fade to zero
            float ring_pos = (distance - HALO_RING4_RADIUS) / (HALO_RING5_RADIUS - HALO_RING4_RADIUS);
            brightness_factor = HALO_RING5_BRIGHTNESS - (ring_pos * HALO_RING5_BRIGHTNESS);
        }
        // Beyond outer ring = completely off
        
        if (brightness_factor > 0.005f) { // Only light LEDs with meaningful brightness
            uint8_t fade_brightness = (uint8_t)(led_status.brightness * brightness_factor);
            if (fade_brightness > 0) {
                led_color_t faded_color = hwLedsApplyBrightness(led_status.color, fade_brightness);
                set_pixel(i, faded_color.r, faded_color.g, faded_color.b);
            }
        }
    }
    
    // Continuous smooth movement using configurable speed
    led_status.animation_phase += HALO_SPEED;
    if (led_status.animation_phase >= 1.0f) {
        led_status.animation_phase = 0.0f;
    }
}

static void apply_effect_bounce(void) {
    clear_all();
    
    int half_leds = NUM_LEDS / 2;
    
    // Bounce: 0→1→0 using sine wave for smooth motion
    // animation_phase goes from 0.0 to 1.0
    float bounce_pos = sinf(led_status.animation_phase * M_PI); // 0→1→0
    
    // Calculate LED positions for both halves (synchronized and mirrored)
    // Left half: bounces from 0 towards half_leds-1
    int left_led_index = (int)(bounce_pos * (half_leds - 1));
    
    // Right half: bounces from half_leds towards NUM_LEDS-1 (mirrored)
    int right_led_index = half_leds + (int)(bounce_pos * (half_leds - 1));
    
    // Draw light source in LEFT half with halo
    for (int i = 0; i < half_leds; i++) {
        int distance = abs(i - left_led_index);
        float brightness_factor = 0.0f;
        
        if (distance == 0) {
            brightness_factor = 1.0f; // Center LED at full brightness
        } else if (distance == 1) {
            brightness_factor = 0.5f; // Adjacent LEDs at 50%
        } else if (distance == 2) {
            brightness_factor = 0.2f; // Next LEDs at 20%
        }
        
        if (brightness_factor > 0.0f) {
            uint8_t fade_brightness = (uint8_t)(led_status.brightness * brightness_factor);
            led_color_t faded_color = hwLedsApplyBrightness(led_status.color, fade_brightness);
            set_pixel(i, faded_color.r, faded_color.g, faded_color.b);
        }
    }
    
    // Draw light source in RIGHT half with halo (synchronized)
    for (int i = half_leds; i < NUM_LEDS; i++) {
        int distance = abs(i - right_led_index);
        float brightness_factor = 0.0f;
        
        if (distance == 0) {
            brightness_factor = 1.0f; // Center LED at full brightness
        } else if (distance == 1) {
            brightness_factor = 0.5f; // Adjacent LEDs at 50%
        } else if (distance == 2) {
            brightness_factor = 0.2f; // Next LEDs at 20%
        }
        
        if (brightness_factor > 0.0f) {
            uint8_t fade_brightness = (uint8_t)(led_status.brightness * brightness_factor);
            led_color_t faded_color = hwLedsApplyBrightness(led_status.color, fade_brightness);
            set_pixel(i, faded_color.r, faded_color.g, faded_color.b);
        }
    }
    
    // Advance animation
    led_status.animation_phase += BOUNCE_SPEED;
    if (led_status.animation_phase >= 1.0f) {
        led_status.animation_phase = 0.0f;
    }
}

// =============== DEMO EFFECT ===============
// Number of showcase stages and how long each one lasts (in frames).
// led_task runs every LED_EFFECT_INTERVAL_MS (20ms => ~50 FPS),
// so 250 frames is about 5 seconds per stage.
#define DEMO_STAGE_COUNT  4
#define DEMO_STAGE_FRAMES 250

// HSV (h,s,v all in [0,1]) -> RGB led_color_t. Used by the demo effects.
static led_color_t demo_hsv(float h, float s, float v) {
    float r = 0.0f, g = 0.0f, b = 0.0f;
    float i = floorf(h * 6.0f);
    float f = h * 6.0f - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);

    switch (((int)i) % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        case 5: r = v; g = p; b = q; break;
    }

    return hwLedsCreateColor((uint8_t)(r * 255.0f), (uint8_t)(g * 255.0f), (uint8_t)(b * 255.0f));
}

// Demo mode: cycles through several showcase light effects.
static void apply_effect_demo(void) {
    static uint32_t demo_frame = 0;
    static float sparkle[NUM_LEDS] = {0};   // per-LED red sparkle intensity (stage 1)
    static float trail[NUM_LEDS] = {0};     // per-LED comet trail intensity (stage 3)

    int stage = (demo_frame / DEMO_STAGE_FRAMES) % DEMO_STAGE_COUNT;
    uint32_t local = demo_frame % DEMO_STAGE_FRAMES;
    uint8_t br = led_status.brightness;

    clear_all();

    switch (stage) {
        case 0: {
            // Stage 0: one red LED travelling around a full ring of green.
            int red_pos = (local / 15) % NUM_LEDS;  // advance ~every 300ms
            led_color_t green = hwLedsApplyBrightness(hwLedsCreateColor(0, 255, 0), br);
            led_color_t red   = hwLedsApplyBrightness(hwLedsCreateColor(255, 0, 0), br);
            for (int i = 0; i < NUM_LEDS; i++) {
                set_pixel(i, green.r, green.g, green.b);
            }
            set_pixel(red_pos, red.r, red.g, red.b);
            break;
        }

        case 1: {
            // Stage 1: random red sparkles flashing over a white background.
            for (int i = 0; i < NUM_LEDS; i++) {
                if ((rand() % 100) < 8) {
                    sparkle[i] = 1.0f;          // ignite a new sparkle
                }
                sparkle[i] *= 0.85f;            // fade existing sparkles

                float s = sparkle[i];
                // Blend white -> red as the sparkle intensity rises.
                uint8_t r = 255;
                uint8_t g = (uint8_t)(255.0f * (1.0f - s));
                uint8_t b = (uint8_t)(255.0f * (1.0f - s));
                led_color_t c = hwLedsApplyBrightness(hwLedsCreateColor(r, g, b), br);
                set_pixel(i, c.r, c.g, c.b);
            }
            break;
        }

        case 2: {
            // Stage 2: rotating rainbow wheel.
            float offset = (float)local / DEMO_STAGE_FRAMES;  // 0..1 across the stage
            for (int i = 0; i < NUM_LEDS; i++) {
                float h = fmodf((float)i / NUM_LEDS + offset, 1.0f);
                led_color_t c = hwLedsApplyBrightness(demo_hsv(h, 1.0f, 1.0f), br);
                set_pixel(i, c.r, c.g, c.b);
            }
            break;
        }

        case 3: {
            // Stage 3: cyan comet chasing around the ring with a fading trail.
            for (int i = 0; i < NUM_LEDS; i++) {
                trail[i] *= 0.6f;              // fade the trail every frame
            }
            int head = (local / 4) % NUM_LEDS; // advance ~every 80ms
            trail[head] = 1.0f;
            for (int i = 0; i < NUM_LEDS; i++) {
                float intensity = trail[i];
                if (intensity > 0.01f) {
                    led_color_t c = hwLedsApplyBrightness(demo_hsv(0.55f, 1.0f, intensity), br);
                    set_pixel(i, c.r, c.g, c.b);
                }
            }
            break;
        }
    }

    demo_frame++;
}

// "Magia" effect: random colored sparkles igniting and fading over a faint
// cool base glow, for a magical twinkling look. Meant to be shown briefly as
// a flourish (see MAGIA_DURATION_MS in the application).
static void apply_effect_magia(void) {
    static float spark[NUM_LEDS] = {0};   // per-LED sparkle intensity
    static float hue[NUM_LEDS]   = {0};   // per-LED sparkle hue (0..1)
    uint8_t br = led_status.brightness;

    clear_all();

    for (int i = 0; i < NUM_LEDS; i++) {
        // Randomly ignite a new sparkle with a random magic hue.
        if ((rand() % 100) < 12) {
            spark[i] = 1.0f;
            hue[i] = (float)(rand() % 1000) / 1000.0f;
        }
        spark[i] *= 0.80f;  // fade existing sparkles

        // Faint cool base + bright colored sparkle on top.
        led_color_t spk = demo_hsv(hue[i], 1.0f, spark[i]);
        int r = 0          + spk.r;
        int g = 4          + spk.g;   // subtle blue/teal base glow
        int b = 12         + spk.b;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;

        led_color_t c = hwLedsApplyBrightness(
            hwLedsCreateColor((uint8_t)r, (uint8_t)g, (uint8_t)b), br);
        set_pixel(i, c.r, c.g, c.b);
    }
}

// LED task
static void led_task(void *arg) {
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(LED_EFFECT_INTERVAL_MS);
    
    while (task_running) {
        if (signal_override) {
            // Signal level mode active - skip effect rendering
            vTaskDelayUntil(&last_wake_time, frequency);
            continue;
        }
        if (module_initialized && led_status.enabled) {
            switch (led_status.effect) {
                case LED_EFFECT_FIX:
                    apply_effect_fix();
                    break;
                case LED_EFFECT_BREATH:
                    apply_effect_breath();
                    break;
                case LED_EFFECT_HALO:
                    apply_effect_halo();
                    break;
                case LED_EFFECT_BOUNCE:
                    apply_effect_bounce();
                    break;
                case LED_EFFECT_DEMO:
                    apply_effect_demo();
                    break;
                case LED_EFFECT_MAGIA:
                    apply_effect_magia();
                    break;
            }
            
            esp_err_t ret = update_strip();
            if (ret != ESP_OK) {
                // Silent error - let main handle monitoring
            }
            
            led_status.frame_count++;
        } else if (led_status.initialized && !led_status.enabled) {
            clear_all();
            update_strip();
        }
        
        vTaskDelayUntil(&last_wake_time, frequency);
    }
    
    vTaskDelete(NULL);
}

// =============== CORE FUNCTIONS ===============

esp_err_t hwLedsInit(void) {
    esp_err_t ret = ESP_OK;
    BaseType_t task_ret;
    bool rmt_channel_created = false;
    bool encoder_created = false;
    bool rmt_enabled = false;
    
    if (module_initialized) {
        // Already initialized - silent return
    } else {
        // Create RMT TX channel
        rmt_tx_channel_config_t tx_chan_config;
        memset(&tx_chan_config, 0, sizeof(tx_chan_config));
        tx_chan_config.clk_src = RMT_CLK_SRC_DEFAULT;
        tx_chan_config.gpio_num = LED_PIN;
        tx_chan_config.mem_block_symbols = 64;
        tx_chan_config.resolution_hz = 10 * 1000 * 1000; // 10MHz resolution
        tx_chan_config.trans_queue_depth = 4;
        tx_chan_config.flags.invert_out = false;
        tx_chan_config.flags.with_dma = false;
        
        ret = rmt_new_tx_channel(&tx_chan_config, &rmt_chan);
        if (ret == ESP_OK) {
            rmt_channel_created = true;
            
            // Create LED encoder
            ret = rmt_new_led_strip_encoder(&led_encoder);
            if (ret == ESP_OK) {
                encoder_created = true;
                
                // Enable RMT channel
                ret = rmt_enable(rmt_chan);
                if (ret == ESP_OK) {
                    rmt_enabled = true;
                    
                    // Set default yellow halo effect
                    led_status.effect = LED_EFFECT_HALO;
                    led_status.color.r = 255;  // Red
                    led_status.color.g = 200;  // Green (less green for more yellow)
                    led_status.color.b = 0;    // Blue
                    led_status.brightness = 100; // Moderate brightness
                    
                    // Clear LEDs initially, then effect will start
                    clear_all();
                    update_strip();
                    
                    // Create LED task
                    task_running = true;
                    task_ret = xTaskCreate(
                        led_task,
                        "led_task",
                        TASK_STACK_SIZE_MEDIUM,
                        NULL,
                        TASK_PRIORITY_HIGH,  // Higher priority to prevent glitches
                        &led_task_handle
                    );
                    
                    if (task_ret == pdPASS) {
                        led_status.initialized = true;
                        led_status.task_running = true;
                        module_initialized = true;
                    } else {
                        ret = ESP_FAIL;
                    }
                }
            }
        }
        
        // Cleanup on error
        if (ret != ESP_OK) {
            if (rmt_enabled) {
                rmt_disable(rmt_chan);
            }
            if (encoder_created) {
                rmt_del_encoder(led_encoder);
            }
            if (rmt_channel_created) {
                rmt_del_channel(rmt_chan);
            }
        }
    }
    
    return ret;
}

esp_err_t hwLedsDestroy(void) {
    esp_err_t ret = ESP_OK;
    
    if (module_initialized) {
        task_running = false;
        if (led_task_handle != NULL) {
            vTaskDelete(led_task_handle);
            led_task_handle = NULL;
        }
        
        if (rmt_chan) {
            clear_all();
            update_strip();
            rmt_disable(rmt_chan);
            rmt_del_encoder(led_encoder);
            rmt_del_channel(rmt_chan);
            rmt_chan = NULL;
            led_encoder = NULL;
        }
        
        led_status.initialized = false;
        led_status.task_running = false;
        module_initialized = false;
    }
    
    return ret;
}

bool hwLedsIsReady(void) {
    return module_initialized && led_status.task_running;
}

led_status_t hwLedsGetStatus(void) {
    return led_status;
}

// =============== COLOR FUNCTIONS ===============

void hwLedsSetColor(uint8_t r, uint8_t g, uint8_t b) {
    led_status.color.r = r;
    led_status.color.g = g;
    led_status.color.b = b;
}

void hwLedsSetColorStruct(led_color_t color) {
    hwLedsSetColor(color.r, color.g, color.b);
}

led_color_t hwLedsGetColor(void) {
    return led_status.color;
}

// =============== BRIGHTNESS FUNCTIONS ===============

void hwLedsSetBrightness(uint8_t brightness) {
    led_status.brightness = brightness;
}

uint8_t hwLedsGetBrightness(void) {
    return led_status.brightness;
}

// =============== EFFECT FUNCTIONS ===============

void hwLedsSetEffect(led_effect_t effect) {
    led_status.effect = effect;
    led_status.animation_phase = 0.0f; // Reset animation
}

bool hwLedsSetEffectByName(const char* effect_name) {
    bool result = false;
    
    if (strcmp(effect_name, "fix") == 0) {
        hwLedsSetEffect(LED_EFFECT_FIX);
        result = true;
    } else if (strcmp(effect_name, "breath") == 0) {
        hwLedsSetEffect(LED_EFFECT_BREATH);
        result = true;
    } else if (strcmp(effect_name, "halo") == 0) {
        hwLedsSetEffect(LED_EFFECT_HALO);
        result = true;
    } else if (strcmp(effect_name, "bounce") == 0) {
        hwLedsSetEffect(LED_EFFECT_BOUNCE);
        result = true;
    } else if (strcmp(effect_name, "demo") == 0) {
        hwLedsSetEffect(LED_EFFECT_DEMO);
        result = true;
    } else if (strcmp(effect_name, "magia") == 0) {
        hwLedsSetEffect(LED_EFFECT_MAGIA);
        result = true;
    } else {
        result = false;
    }
    
    return result;
}

led_effect_t hwLedsGetEffect(void) {
    return led_status.effect;
}

const char* hwLedsGetEffectName(led_effect_t effect) {
    const char* result;
    
    switch (effect) {
        case LED_EFFECT_FIX: 
            result = "fix";
            break;
        case LED_EFFECT_BREATH: 
            result = "breath";
            break;
        case LED_EFFECT_HALO: 
            result = "halo";
            break;
        case LED_EFFECT_BOUNCE:
            result = "bounce";
            break;
        case LED_EFFECT_DEMO:
            result = "demo";
            break;
        case LED_EFFECT_MAGIA:
            result = "magia";
            break;
        default:
            result = "unknown";
            break;
    }
    
    return result;
}

// =============== CONTROL FUNCTIONS ===============

void hwLedsSetEnabled(bool enabled) {
    led_status.enabled = enabled;
}

void hwLedsToggle(void) {
    hwLedsSetEnabled(!led_status.enabled);
}

void hwLedsOff(void) {
    hwLedsSetEnabled(false);
}

void hwLedsBlink(uint8_t r, uint8_t g, uint8_t b, int times) {
    if (module_initialized) {
        // Save current state
        led_color_t original_color = led_status.color;
        bool original_enabled = led_status.enabled;
        led_effect_t original_effect = led_status.effect;
        
        // Set blink color and effect
        hwLedsSetColor(r, g, b);
        hwLedsSetEffect(LED_EFFECT_FIX);
        
        // Blink
        for (int i = 0; i < times; i++) {
            hwLedsSetEnabled(true);
            vTaskDelay(pdMS_TO_TICKS(200));
            
            hwLedsSetEnabled(false);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        
        // Restore original state
        led_status.color = original_color;
        led_status.enabled = original_enabled;
        led_status.effect = original_effect;
    }
}

// =============== UTILITY FUNCTIONS ===============

led_color_t hwLedsCreateColor(uint8_t r, uint8_t g, uint8_t b) {
    led_color_t color = {r, g, b};
    return color;
}

led_color_t hwLedsApplyBrightness(led_color_t color, uint8_t brightness) {
    led_color_t result;
    result.r = (color.r * brightness) / 255;
    result.g = (color.g * brightness) / 255;
    result.b = (color.b * brightness) / 255;
    return result;
}

// Show signal level on LED ring: leds_count LEDs lit (1-NUM_LEDS), rest off
// Directly writes to buffer and updates strip, bypassing the effect engine
void hwLedsShowSignalLevel(int leds_count, uint8_t r, uint8_t g, uint8_t b) {
    if (!module_initialized) return;
    
    signal_override = true;  // Pause the effect engine
    
    if (leds_count < 0) leds_count = 0;
    if (leds_count > NUM_LEDS) leds_count = NUM_LEDS;
    
    // Apply current brightness
    led_color_t color = {r, g, b};
    uint8_t brightness = led_status.brightness > 0 ? led_status.brightness : 50;  // Min 50% for visibility
    led_color_t final_color = hwLedsApplyBrightness(color, brightness);
    
    clear_all();
    for (int i = 0; i < leds_count; i++) {
        set_pixel(i, final_color.r, final_color.g, final_color.b);
    }
    update_strip();
}

// Resume normal effect engine after signal level mode
void hwLedsReleaseSignalOverride(void) {
    signal_override = false;
}