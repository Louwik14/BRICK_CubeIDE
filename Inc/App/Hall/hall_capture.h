#ifndef APP_HALL_HALL_CAPTURE_H
#define APP_HALL_HALL_CAPTURE_H

#include <stdint.h>
#include "App/Hall/hall_engine.h"

/* Temporary acquisition trace. Sequence is written last. */
typedef struct {
    uint32_t sequence;
    uint32_t tick_ms;
    uint32_t tim5_tick;
    uint32_t held_before;
    uint32_t held_after;
    uint32_t calibration_generation;
    uint16_t adc1_hall_a;
    uint16_t adc2_hall_b;
    uint16_t adc1_hall_c;
    uint16_t adc1_volume;
    uint16_t adc1_callbacks;
    uint16_t adc2_callbacks;
    uint8_t mux_expected;
    uint8_t mux_odr;
    uint8_t mux_idr;
    uint8_t completing_adc;
    uint32_t adc1_callback_tick;
    uint32_t adc2_callback_tick;
    uint16_t adc1_error;
    uint16_t adc2_error;
    uint16_t adc1_dma_ndtr_before;
    uint16_t adc1_dma_ndtr_after;
    uint32_t mux_generation;
    uint32_t adc1_callback_generation;
    uint32_t adc2_callback_generation;
    uint16_t adc1_callback_ndtr;
    uint16_t adc2_callback_ndtr;
} hall_capture_record_t;

typedef struct {
    uint16_t minimum;
    uint16_t maximum;
    uint16_t press;
    uint16_t release;
} hall_capture_calibration_t;

#define HALL_CAPTURE_CAPACITY 4096U

extern volatile hall_capture_record_t g_hall_capture[HALL_CAPTURE_CAPACITY];
extern volatile hall_capture_calibration_t
    g_hall_capture_calibration[HALL_KEY_COUNT];
extern volatile uint32_t g_hall_capture_sequence;
extern volatile uint32_t g_hall_capture_calibration_generation;
extern volatile uint8_t g_hall_capture_frozen;
extern volatile uint8_t g_hall_capture_release_keys[3];
extern volatile uint32_t g_hall_capture_release_ticks[3];
extern volatile uint32_t g_hall_capture_held_mask;

void hall_capture_note_release(uint8_t key, uint32_t held_ms);

#endif
