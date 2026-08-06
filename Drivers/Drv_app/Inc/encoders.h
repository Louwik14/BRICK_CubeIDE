#ifndef ENCODERS_H
#define ENCODERS_H

#include <stdint.h>

#include "Core/brick_build_config.h"

typedef enum
{
    ENC_PAGE = 0,
    ENC_PARAM_A,
    ENC_PARAM_B,
    ENC_PARAM_C,
    ENC_COUNT
} encoder_id_t;

/* Fixed-width, pointer-free ABI for validated encoder detents. */
#define ENCODER_DETENT_QUEUE_CAPACITY 32U

_Static_assert((ENCODER_DETENT_QUEUE_CAPACITY
                & (ENCODER_DETENT_QUEUE_CAPACITY - 1U)) == 0U,
               "encoder detent queue capacity must be a power of two");

typedef struct
{
    uint32_t capture_tick;
    uint32_t ingress_serial;
    int8_t direction;
    uint8_t encoder_id;
    uint16_t reserved;
} encoder_detent_event_t;

_Static_assert(sizeof(encoder_detent_event_t) == 12U,
               "encoder detent event ABI must remain fixed-width");

void encoders_init(void);
void encoders_update(uint32_t dt_ms);

int16_t encoder_get_delta(uint8_t encoder);
int16_t encoder_consume_delta(uint8_t encoder);
void encoder_reset_delta(uint8_t encoder);

/* The timer-poll IRQ is the single producer and the caller the single
 * consumer. Saturation drops the newest event and increments the counter. */
uint8_t encoder_detent_event_pop(encoder_detent_event_t *out_event);
uint32_t encoder_detent_event_pending_count(void);
uint32_t encoder_detent_event_overflow_count(void);

#if BRICK_TEST_BUILD
uint8_t encoder_test_inject_delta(uint8_t encoder, int16_t delta);
#endif

#endif
