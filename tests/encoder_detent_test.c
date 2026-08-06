#include "encoders_hw.h"

#include <assert.h>
#include <stdint.h>

#include "param_store.h"

static uint8_t g_encoder_states[ENC_COUNT];
static uint32_t g_fake_tim5_tick;

uint8_t board_controls_encoder_state(uint8_t encoder)
{
    return (encoder < (uint8_t)ENC_COUNT) ? g_encoder_states[encoder] : 0U;
}

void board_controls_start_encoder_fast_poll_timer(void)
{
}

uint32_t live_clock_capture_tick(void)
{
    return g_fake_tim5_tick;
}

static void set_state(uint8_t encoder, uint8_t state, uint32_t tick)
{
    g_encoder_states[encoder] = state;
    g_fake_tim5_tick = tick;
    encoders_hw_read();
}

static void rotate_positive(uint8_t encoder, uint32_t tick_base)
{
    set_state(encoder, 1U, tick_base + 1U);
    set_state(encoder, 3U, tick_base + 2U);
    set_state(encoder, 2U, tick_base + 3U);
    set_state(encoder, 0U, tick_base + 4U);
}

static void rotate_negative(uint8_t encoder, uint32_t tick_base)
{
    set_state(encoder, 2U, tick_base + 1U);
    set_state(encoder, 3U, tick_base + 2U);
    set_state(encoder, 1U, tick_base + 3U);
    set_state(encoder, 0U, tick_base + 4U);
}

static void test_slow_direction_and_timestamp(void)
{
    encoder_detent_event_t event;

    encoders_hw_init();
    rotate_positive(0U, 100U);
    assert(encoders_hw_get_detent_pending_count() == 1U);
    assert(encoders_hw_pop_detent_event(&event) != 0U);
    assert(event.encoder_id == 0U);
    assert(event.direction == 1);
    assert(event.capture_tick == 104U);
    assert(event.ingress_serial == 1U);
    assert(encoders_hw_get_detent_pending_count() == 0U);

    /* A complete detent is emitted once; stable reads emit nothing. */
    encoders_hw_read();
    assert(encoders_hw_get_detent_pending_count() == 0U);
}

static void test_fast_multiple_encoder_order(void)
{
    encoder_detent_event_t event;

    encoders_hw_init();
    rotate_positive(2U, 200U);
    rotate_negative(1U, 300U);
    rotate_positive(3U, 400U);

    assert(encoders_hw_get_detent_pending_count() == 3U);
    assert(encoders_hw_pop_detent_event(&event) != 0U);
    assert(event.encoder_id == 2U && event.direction == 1 && event.capture_tick == 204U);
    assert(event.ingress_serial == 1U);
    assert(encoders_hw_pop_detent_event(&event) != 0U);
    assert(event.encoder_id == 1U && event.direction == -1 && event.capture_tick == 304U);
    assert(event.ingress_serial == 2U);
    assert(encoders_hw_pop_detent_event(&event) != 0U);
    assert(event.encoder_id == 3U && event.direction == 1 && event.capture_tick == 404U);
    assert(event.ingress_serial == 3U);
    assert(encoders_hw_pop_detent_event(&event) == 0U);
}

static void test_binding_snapshot_and_audio_route(void)
{
    encoder_detent_event_t event;
    encoder_binding_snapshot_t snapshot = { 0 };

    encoders_hw_init();
    snapshot.entry[0] = encoder_binding_pack(PARAM_VCA_ATTACK,
                                             1U,
                                             2U,
                                             0xFFU,
                                             1U,
                                             ENCODER_BINDING_ROUTE_AUDIO,
                                             0U,
                                             1U);
    encoders_set_binding_snapshot(&snapshot);
    rotate_positive(0U, 500U);

    assert(encoders_hw_pop_detent_event(&event) != 0U);
    const uint32_t binding = event.binding.entry[0];
    assert(encoder_binding_parameter(binding) == PARAM_VCA_ATTACK);
    assert(encoder_binding_scope(binding) == 1U);
    assert(encoder_binding_track(binding) == 2U);
    assert(encoder_binding_slot(binding) == 0xFFU);
    assert(encoder_binding_shift_down(binding) == 1U);
    assert(encoder_binding_track_modifier_down(binding) == 0U);
    assert(encoder_binding_route(binding) == ENCODER_BINDING_ROUTE_AUDIO);
    assert(encoder_binding_valid(binding) != 0U);
    assert(encoders_hw_get_delta(0U) == 0);
}

static void test_deterministic_overflow(void)
{
    encoder_detent_event_t event;

    encoders_hw_init();
    for (uint32_t i = 0U; i < ENCODER_DETENT_QUEUE_CAPACITY + 1U; ++i)
    {
        rotate_positive(0U, 1000U + (i * 4U));
    }

    assert(encoders_hw_get_detent_pending_count() == ENCODER_DETENT_QUEUE_CAPACITY);
    assert(encoders_hw_get_detent_overflow_count() == 1U);
    for (uint32_t i = 0U; i < ENCODER_DETENT_QUEUE_CAPACITY; ++i)
    {
        assert(encoders_hw_pop_detent_event(&event) != 0U);
        assert(event.ingress_serial == i + 1U);
        assert(event.direction == 1);
    }
    assert(encoders_hw_pop_detent_event(&event) == 0U);
}

int main(void)
{
    test_slow_direction_and_timestamp();
    test_fast_multiple_encoder_order();
    test_binding_snapshot_and_audio_route();
    test_deterministic_overflow();
    return 0;
}
