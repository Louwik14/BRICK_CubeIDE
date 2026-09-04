#include "encoders_hw.h"

#include "Board/board_controls.h"
#include "IPC/live_clock_control.h"

#include "cmsis_gcc.h"

#include <limits.h>

static uint8_t enc_prev_state[ENC_COUNT];
static int8_t enc_transition_residual[ENC_COUNT];

/* SPSC queue: the fast-poll IRQ publishes and one consumer pops. */
static encoder_detent_event_t enc_detent_queue[ENCODER_DETENT_QUEUE_CAPACITY]
    __attribute__((aligned(32)));
static volatile uint32_t enc_detent_head;
static volatile uint32_t enc_detent_tail;
static volatile uint32_t enc_detent_overflow_count;
static uint32_t enc_detent_ingress_serial;
static encoder_binding_snapshot_t enc_binding_buffers[2];
static volatile uint8_t enc_binding_active;

static const int8_t quad_table[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0,
};

static uint8_t enc_read_state(uint8_t encoder)
{
    return board_controls_encoder_state(encoder);
}

static uint32_t encoders_hw_next_ingress_serial(void)
{
    uint32_t serial = enc_detent_ingress_serial + 1U;
    if (serial == 0U)
    {
        serial = 1U;
    }

    enc_detent_ingress_serial = serial;
    return serial;
}

void encoders_set_binding_snapshot(const encoder_binding_snapshot_t *snapshot)
{
    if (snapshot == 0)
    {
        return;
    }

    const uint8_t active = (uint8_t)(enc_binding_active & 1U);
    const uint8_t next = (uint8_t)(active ^ 1U);
    enc_binding_buffers[next] = *snapshot;
    __DMB();
    enc_binding_active = next;
}

static void encoders_hw_read_binding_snapshot(encoder_binding_snapshot_t *out_snapshot)
{
    const uint8_t active = (uint8_t)(enc_binding_active & 1U);
    __DMB();
    *out_snapshot = enc_binding_buffers[active];
}

static void encoders_hw_publish_detent(uint8_t encoder, int8_t direction)
{
    const uint32_t capture_tick = live_clock_capture_tick();
    const uint32_t ingress_serial = encoders_hw_next_ingress_serial();
    encoder_binding_snapshot_t binding;
    encoders_hw_read_binding_snapshot(&binding);
    const uint32_t head = enc_detent_head;
    const uint32_t tail = enc_detent_tail;
    if ((uint32_t)(head - tail) >= ENCODER_DETENT_QUEUE_CAPACITY)
    {
        /* Drop newest: accepted events retain their original order. */
        enc_detent_overflow_count++;
        return;
    }

    encoder_detent_event_t *const event =
        &enc_detent_queue[head & (ENCODER_DETENT_QUEUE_CAPACITY - 1U)];
    event->capture_tick = capture_tick;
    event->ingress_serial = ingress_serial;
    event->binding = binding;
    event->direction = direction;
    event->encoder_id = encoder;
    event->reserved = 0U;
    __DMB();
    enc_detent_head = head + 1U;
}

static void encoders_hw_accumulate_transition(uint8_t encoder, int8_t transition)
{
    const int16_t total = (int16_t)enc_transition_residual[encoder] + (int16_t)transition;
    const int16_t increment = total / (int16_t)BOARD_CONTROLS_ENCODER_TRANSITIONS_PER_INCREMENT;
    const int16_t residual = total % (int16_t)BOARD_CONTROLS_ENCODER_TRANSITIONS_PER_INCREMENT;

    enc_transition_residual[encoder] = (int8_t)residual;

    if (increment == 0)
    {
        return;
    }

    const int8_t direction = (increment > 0) ? 1 : -1;
    int16_t detents = (increment > 0) ? increment : (int16_t)-increment;
    while (detents > 0)
    {
        encoders_hw_publish_detent(encoder, direction);
        detents--;
    }
}

void encoders_hw_init(void)
{
    enc_detent_head = 0U;
    enc_detent_tail = 0U;
    enc_detent_overflow_count = 0U;
    enc_detent_ingress_serial = 0U;
    enc_binding_buffers[0] = (encoder_binding_snapshot_t){ 0 };
    enc_binding_buffers[1] = (encoder_binding_snapshot_t){ 0 };
    enc_binding_active = 0U;

    for (uint8_t i = 0U; i < (uint8_t)ENC_COUNT; i++)
    {
        enc_prev_state[i] = enc_read_state(i);
        enc_transition_residual[i] = 0;
    }
}

void encoders_fast_poll_init(void)
{
    board_controls_start_encoder_fast_poll_timer();
}

void encoders_fast_poll_irq(void)
{
    encoders_hw_read();
}

void encoders_hw_read(void)
{
    for (uint8_t i = 0U; i < (uint8_t)ENC_COUNT; i++)
    {
        const uint8_t prev = enc_prev_state[i];
        const uint8_t now = enc_read_state(i);

        if (now == prev)
        {
            continue;
        }

        const uint8_t idx = (uint8_t)((prev << 2) | now);
        const int8_t step = (int8_t)(quad_table[idx] * BOARD_CONTROLS_ENCODER_DIRECTION);

        enc_prev_state[i] = now;

        if (step == 0)
        {
            continue;
        }

        encoders_hw_accumulate_transition(i, step);
    }
}

void encoders_hw_discard_pending(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    enc_detent_tail = enc_detent_head;
    for (uint8_t i = 0U; i < (uint8_t)ENC_COUNT; ++i)
    {
        enc_transition_residual[i] = 0;
    }
    __set_PRIMASK(primask);
}

uint8_t encoders_hw_pop_detent_event(encoder_detent_event_t *out_event)
{
    if (out_event == 0)
    {
        return 0U;
    }

    const uint32_t tail = enc_detent_tail;
    if (tail == enc_detent_head)
    {
        return 0U;
    }

    *out_event = enc_detent_queue[tail & (ENCODER_DETENT_QUEUE_CAPACITY - 1U)];
    __DMB();
    enc_detent_tail = tail + 1U;
    return 1U;
}

uint32_t encoders_hw_get_detent_pending_count(void)
{
    const uint32_t pending = enc_detent_head - enc_detent_tail;
    return (pending <= ENCODER_DETENT_QUEUE_CAPACITY)
        ? pending
        : ENCODER_DETENT_QUEUE_CAPACITY;
}

uint32_t encoders_hw_get_detent_overflow_count(void)
{
    return enc_detent_overflow_count;
}
