#include "sim_input.h"

#include <limits.h>
#include <string.h>

#include "buttons_hw.h"
#include "encoders_hw.h"

static uint8_t g_buttons[BTN_COUNT];
static int32_t g_encoder_delta[ENC_COUNT];

void sim_input_reset(void)
{
    memset(g_buttons, 0, sizeof(g_buttons));
    memset(g_encoder_delta, 0, sizeof(g_encoder_delta));
}

int sim_input_set_button(button_id_t button, uint8_t down)
{
    if ((uint32_t)button >= (uint32_t)BTN_COUNT) return 0;
    g_buttons[button] = down ? 1U : 0U;
    return 1;
}

int sim_input_add_encoder(uint8_t encoder, int16_t delta)
{
    if (encoder >= (uint8_t)ENC_COUNT) return 0;
    int64_t next = (int64_t)g_encoder_delta[encoder] + delta;
    if (next > INT16_MAX) next = INT16_MAX;
    if (next < INT16_MIN) next = INT16_MIN;
    g_encoder_delta[encoder] = (int32_t)next;
    return 1;
}

uint8_t sim_input_button_state(button_id_t button)
{
    return ((uint32_t)button < (uint32_t)BTN_COUNT) ? g_buttons[button] : 0U;
}

uint64_t sim_input_button_mask(void)
{
    uint64_t mask = 0U;
    for (uint32_t i = 0U; i < (uint32_t)BTN_COUNT; ++i)
    {
        if (g_buttons[i] != 0U) mask |= UINT64_C(1) << i;
    }
    return mask;
}

void sim_input_release_all(void)
{
    memset(g_buttons, 0, sizeof(g_buttons));
}

void buttons_hw_init(void) { sim_input_reset(); }
void buttons_hw_read(void) {}
uint8_t buttons_hw_get(button_id_t button) { return sim_input_button_state(button); }

void encoders_hw_init(void) { memset(g_encoder_delta, 0, sizeof(g_encoder_delta)); }
void encoders_fast_poll_init(void) {}
void encoders_fast_poll_irq(void) {}
void encoders_hw_read(void) {}

int16_t encoders_hw_get_delta(uint8_t encoder)
{
    if (encoder >= (uint8_t)ENC_COUNT) return 0;
    const int16_t result = (int16_t)g_encoder_delta[encoder];
    g_encoder_delta[encoder] = 0;
    return result;
}

uint8_t encoders_hw_pop_detent_event(encoder_detent_event_t *event)
{
    (void)event;
    return 0U;
}

uint32_t encoders_hw_get_detent_pending_count(void) { return 0U; }
uint32_t encoders_hw_get_detent_overflow_count(void) { return 0U; }

void hall_surface_refresh(void) {}
uint8_t hall_surface_is_pressed(uint8_t lane)
{
    return lane < 16U ? sim_input_button_state((button_id_t)((unsigned)BTN_STEP_1 + lane)) : 0U;
}
uint16_t hall_surface_pressure_u16(uint8_t lane) { return hall_surface_is_pressed(lane) ? UINT16_MAX : 0U; }
uint8_t hall_surface_is_binary(void) { return 1U; }
