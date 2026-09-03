#include "App/Hall/hall_keymap.h"

#define HALL_KEY_INVALID 0xFFU

#define K_WHITE(id, white, chroma) { id, HALL_KEY_KIND_WHITE, white, 0U, chroma, 1U }
#define K_BLACK(id, black, chroma) { id, HALL_KEY_KIND_BLACK, 0U, black, chroma, 1U }

static const hall_key_metadata_t g_key_metadata[HALL_KEY_COUNT] = {
    K_WHITE(0U, 1U, 0U),    /* F1 */
    K_BLACK(1U, 1U, 1U),    /* F#1 */
    K_WHITE(2U, 2U, 2U),    /* G1 */
    K_BLACK(3U, 2U, 3U),    /* G#1 */
    K_WHITE(4U, 3U, 4U),    /* A1 */
    K_BLACK(5U, 3U, 5U),    /* A#1 */
    K_WHITE(6U, 4U, 6U),    /* B1 */
    K_WHITE(7U, 5U, 7U),    /* C2 */
    K_BLACK(8U, 4U, 8U),    /* C#2 */
    K_WHITE(9U, 6U, 9U),    /* D2 */
    K_BLACK(10U, 5U, 10U),  /* D#2 */
    K_WHITE(11U, 7U, 11U),  /* E2 */
    K_WHITE(12U, 8U, 12U),  /* F2 */
    K_BLACK(13U, 6U, 13U),  /* F#2 */
    K_WHITE(14U, 9U, 14U),  /* G2 */
    K_BLACK(15U, 7U, 15U),  /* G#2 */
    K_WHITE(16U, 10U, 16U), /* A2 */
    K_BLACK(17U, 8U, 17U),  /* A#2 */
    K_WHITE(18U, 11U, 18U), /* B2 */
    K_WHITE(19U, 12U, 19U), /* C3 */
    K_BLACK(20U, 9U, 20U),  /* C#3 */
    K_WHITE(21U, 13U, 21U), /* D3 */
    K_BLACK(22U, 10U, 22U), /* D#3 */
    K_WHITE(23U, 14U, 23U), /* E3 */
};

static const uint8_t g_key_from_mux[3U][8U] = {
    { 1U,  3U,  5U,  0U,  2U,  7U,  4U,  6U },
    { 13U, 10U, 8U,  15U, 9U,  14U, 11U, 12U },
    { 23U, 20U, 17U, 22U, 16U, 21U, 18U, 19U },
};

_Static_assert((sizeof(g_key_metadata) / sizeof(g_key_metadata[0])) == 24U,
               "Hall keyboard metadata must define 24 keys");
_Static_assert((sizeof(g_key_from_mux) / sizeof(g_key_from_mux[0])) == 3U,
               "BRICK Hall keyboard must define 3 muxes");
_Static_assert((sizeof(g_key_from_mux[0]) / sizeof(g_key_from_mux[0][0])) == 8U,
               "BRICK Hall keyboard must define 8 channels per mux");

#define KEY_ID_MASK 0x00FFFFFFUL
#define WHITE_KEY_MASK ( \
    (1UL << 0U) | (1UL << 2U) | (1UL << 4U) | (1UL << 6U) | \
    (1UL << 7U) | (1UL << 9U) | (1UL << 11U) | (1UL << 12U) | \
    (1UL << 14U) | (1UL << 16U) | (1UL << 18U) | (1UL << 19U) | \
    (1UL << 21U) | (1UL << 23U))
#define BLACK_KEY_MASK ( \
    (1UL << 1U) | (1UL << 3U) | (1UL << 5U) | (1UL << 8U) | \
    (1UL << 10U) | (1UL << 13U) | (1UL << 15U) | (1UL << 17U) | \
    (1UL << 20U) | (1UL << 22U))
#define MUX_KEY_MASK ( \
    (1UL << 1U) | (1UL << 3U) | (1UL << 5U) | (1UL << 0U) | \
    (1UL << 2U) | (1UL << 7U) | (1UL << 4U) | (1UL << 6U) | \
    (1UL << 13U) | (1UL << 10U) | (1UL << 8U) | (1UL << 15U) | \
    (1UL << 9U) | (1UL << 14U) | (1UL << 11U) | (1UL << 12U) | \
    (1UL << 23U) | (1UL << 20U) | (1UL << 17U) | (1UL << 22U) | \
    (1UL << 16U) | (1UL << 21U) | (1UL << 18U) | (1UL << 19U))

_Static_assert(MUX_KEY_MASK == KEY_ID_MASK,
               "Hall keyboard mux table must contain 24 unique logical keys");
_Static_assert((WHITE_KEY_MASK | BLACK_KEY_MASK) == KEY_ID_MASK,
               "Hall keyboard metadata must cover 24 chromatic keys");
_Static_assert((WHITE_KEY_MASK & BLACK_KEY_MASK) == 0U,
               "Low-cost Hall keyboard white/black key sets must not overlap");

uint8_t hall_keymap_key_for_mux_channel(uint8_t mux_index, uint8_t channel, uint8_t *out_key)
{
    if ((out_key == 0) || (channel >= 8U))
    {
        return 0U;
    }

    if (mux_index >= 3U)
    {
        return 0U;
    }

    *out_key = g_key_from_mux[mux_index][channel];
    return (*out_key < HALL_KEY_COUNT) ? 1U : 0U;
}

uint8_t hall_keymap_metadata(uint8_t key, hall_key_metadata_t *out_meta)
{
    if ((out_meta == 0) || (key >= HALL_KEY_COUNT))
    {
        return 0U;
    }

    *out_meta = g_key_metadata[key];
    return out_meta->valid;
}
