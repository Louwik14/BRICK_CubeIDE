#include "Board/board_product.h"
#include "Board/board_audio_format.h"
#include "Board/board_led_transport.h"

static const board_product_capabilities_t g_premium_capabilities = {
    .has_physical_cue = 1U,
    .physical_input_stereo_count = 3U,
    .has_microphone_input = 0U,
    .audio_tdm_slots = BOARD_AUDIO_TDM_SLOTS,
    .has_analog_hall_lanes = 1U,
    .has_step_binary_lanes = 0U,
    .has_macro_pots = 1U,
    .has_separate_hall_keyboard = 0U,
    .has_dynamic_usb_role = 0U,
    .led_count = BOARD_LED_TRANSPORT_COUNT,
};

const board_product_capabilities_t *board_product_capabilities(void)
{
    return &g_premium_capabilities;
}
