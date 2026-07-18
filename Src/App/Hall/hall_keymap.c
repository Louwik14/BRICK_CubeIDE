#include "App/Hall/hall_keymap.h"

#include "Board/board_product.h"

#define HALL_KEY_INVALID 0xFFU

#define K_WHITE(id, white, chroma) { id, HALL_KEY_KIND_WHITE, white, 0U, chroma, 1U }
#define K_BLACK(id, black, chroma) { id, HALL_KEY_KIND_BLACK, 0U, black, chroma, 1U }

static const hall_key_metadata_t g_lowcost_key_metadata[HALL_KEY_COUNT] = {
    K_WHITE(0U, 1U, 0U),   /* B1 */
    K_WHITE(1U, 2U, 2U),   /* B2 */
    K_WHITE(2U, 3U, 4U),   /* B3 */
    K_WHITE(3U, 4U, 5U),   /* B4 */
    K_WHITE(4U, 5U, 7U),   /* B5 */
    K_WHITE(5U, 6U, 9U),   /* B6 */
    K_WHITE(6U, 7U, 11U),  /* B7 */
    K_WHITE(7U, 8U, 12U),  /* B8 */
    K_WHITE(8U, 9U, 14U),  /* B9 */
    K_WHITE(9U, 10U, 16U), /* B10 */
    K_WHITE(10U, 11U, 17U),/* B11 */
    K_WHITE(11U, 12U, 19U),/* B12 */
    K_WHITE(12U, 13U, 21U),/* B13 */
    K_WHITE(13U, 14U, 23U),/* B14 */
    K_BLACK(14U, 1U, 1U),  /* N1 */
    K_BLACK(15U, 2U, 3U),  /* N2 */
    K_BLACK(16U, 3U, 6U),  /* N3 */
    K_BLACK(17U, 4U, 8U),  /* N4 */
    K_BLACK(18U, 5U, 10U), /* N5 */
    K_BLACK(19U, 6U, 13U), /* N6 */
    K_BLACK(20U, 7U, 15U), /* N7 */
    K_BLACK(21U, 8U, 18U), /* N8 */
    K_BLACK(22U, 9U, 20U), /* N9 */
    K_BLACK(23U, 10U, 22U),/* N10 */
};

static const uint8_t g_premium_key_from_mux[2U][8U] = {
    { 5U,  6U,  7U,  4U, 0U,  3U,  1U,  2U },
    { 13U, 14U, 15U, 12U, 8U, 11U, 9U, 10U },
};

static const uint8_t g_lowcost_key_from_mux[3U][8U] = {
    { 14U, 15U, 16U, 0U, 1U, 4U, 2U, 3U },
    { 19U, 18U, 17U, 20U, 5U, 8U, 6U, 7U },
    { 13U, 22U, 21U, 23U, 9U, 12U, 10U, 11U },
};

_Static_assert((sizeof(g_lowcost_key_metadata) / sizeof(g_lowcost_key_metadata[0])) == 24U,
               "Low-cost Hall keyboard metadata must define 24 keys");
_Static_assert((sizeof(g_lowcost_key_from_mux) / sizeof(g_lowcost_key_from_mux[0])) == 3U,
               "Low-cost Hall keyboard must define 3 muxes");
_Static_assert((sizeof(g_lowcost_key_from_mux[0]) / sizeof(g_lowcost_key_from_mux[0][0])) == 8U,
               "Low-cost Hall keyboard must define 8 channels per mux");

#define LOWCOST_KEY_ID_MASK 0x00FFFFFFUL
#define LOWCOST_B_MASK 0x00003FFFUL
#define LOWCOST_N_MASK 0x000003FFUL
#define LOWCOST_MUX_KEY_MASK ( \
    (1UL << 14U) | (1UL << 15U) | (1UL << 16U) | (1UL << 0U) | \
    (1UL << 1U) | (1UL << 4U) | (1UL << 2U) | (1UL << 3U) | \
    (1UL << 19U) | (1UL << 18U) | (1UL << 17U) | (1UL << 20U) | \
    (1UL << 5U) | (1UL << 8U) | (1UL << 6U) | (1UL << 7U) | \
    (1UL << 13U) | (1UL << 22U) | (1UL << 21U) | (1UL << 23U) | \
    (1UL << 9U) | (1UL << 12U) | (1UL << 10U) | (1UL << 11U))
#define LOWCOST_B_PRESENT_MASK ( \
    (1UL << 0U) | (1UL << 1U) | (1UL << 2U) | (1UL << 3U) | \
    (1UL << 4U) | (1UL << 5U) | (1UL << 6U) | (1UL << 7U) | \
    (1UL << 8U) | (1UL << 9U) | (1UL << 10U) | (1UL << 11U) | \
    (1UL << 12U) | (1UL << 13U))
#define LOWCOST_N_PRESENT_MASK ( \
    (1UL << 0U) | (1UL << 1U) | (1UL << 2U) | (1UL << 3U) | \
    (1UL << 4U) | (1UL << 5U) | (1UL << 6U) | (1UL << 7U) | \
    (1UL << 8U) | (1UL << 9U))

_Static_assert(LOWCOST_MUX_KEY_MASK == LOWCOST_KEY_ID_MASK,
               "Low-cost Hall keyboard mux table must contain 24 unique logical keys");
_Static_assert(LOWCOST_B_PRESENT_MASK == LOWCOST_B_MASK,
               "Low-cost Hall keyboard metadata must contain B1..B14 exactly once");
_Static_assert(LOWCOST_N_PRESENT_MASK == LOWCOST_N_MASK,
               "Low-cost Hall keyboard metadata must contain N1..N10 exactly once");

static uint8_t hall_keymap_is_lowcost(void)
{
    const board_product_capabilities_t *caps = board_product_capabilities();
    return ((caps != 0) && (caps->has_separate_hall_keyboard != 0U)) ? 1U : 0U;
}

uint8_t hall_keymap_key_for_mux_channel(uint8_t mux_index, uint8_t channel, uint8_t *out_key)
{
    if ((out_key == 0) || (channel >= 8U))
    {
        return 0U;
    }

    if (hall_keymap_is_lowcost() != 0U)
    {
        if (mux_index >= 3U)
        {
            return 0U;
        }
        *out_key = g_lowcost_key_from_mux[mux_index][channel];
        return (*out_key < HALL_KEY_COUNT) ? 1U : 0U;
    }

    if (mux_index >= 2U)
    {
        return 0U;
    }

    *out_key = g_premium_key_from_mux[mux_index][channel];
    return (*out_key < HALL_UI_LANE_COUNT) ? 1U : 0U;
}

uint8_t hall_keymap_metadata(uint8_t key, hall_key_metadata_t *out_meta)
{
    if ((out_meta == 0) || (key >= HALL_KEY_COUNT))
    {
        return 0U;
    }

    if (hall_keymap_is_lowcost() == 0U)
    {
        out_meta->logical_key_id = key;
        out_meta->kind = HALL_KEY_KIND_WHITE;
        out_meta->white_index = (uint8_t)(key + 1U);
        out_meta->black_index = 0U;
        out_meta->chromatic_position = key;
        out_meta->valid = (key < HALL_UI_LANE_COUNT) ? 1U : 0U;
        return out_meta->valid;
    }

    *out_meta = g_lowcost_key_metadata[key];
    return out_meta->valid;
}
