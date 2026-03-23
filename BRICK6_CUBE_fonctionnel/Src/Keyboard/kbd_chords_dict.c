#include "Keyboard/kbd_chords_dict.h"

#include <stddef.h>

typedef struct
{
    uint8_t mask;
    const uint8_t *intervals;
    uint8_t count;
} kbd_chord_component_t;

static const uint8_t TRIAD_MAJOR[] = {0U, 4U, 7U};
static const uint8_t TRIAD_MINOR[] = {0U, 3U, 7U};
static const uint8_t TRIAD_SUS4[] = {0U, 5U, 7U};
static const uint8_t TRIAD_DIM[] = {0U, 3U, 6U};

static const kbd_chord_component_t g_kbd_chord_bases[] = {
    { KBD_CH_BASE_MAJOR, TRIAD_MAJOR, (uint8_t)(sizeof(TRIAD_MAJOR) / sizeof(TRIAD_MAJOR[0])) },
    { KBD_CH_BASE_MINOR, TRIAD_MINOR, (uint8_t)(sizeof(TRIAD_MINOR) / sizeof(TRIAD_MINOR[0])) },
    { KBD_CH_BASE_SUS4, TRIAD_SUS4, (uint8_t)(sizeof(TRIAD_SUS4) / sizeof(TRIAD_SUS4[0])) },
    { KBD_CH_BASE_DIM, TRIAD_DIM, (uint8_t)(sizeof(TRIAD_DIM) / sizeof(TRIAD_DIM[0])) },
};

static const uint8_t EXT_7TH[] = {10U};
static const uint8_t EXT_MAJ7[] = {11U};
static const uint8_t EXT_6TH[] = {9U};
static const uint8_t EXT_9TH[] = {14U};

static const kbd_chord_component_t g_kbd_chord_exts[] = {
    { KBD_CH_EXT_7, EXT_7TH, (uint8_t)(sizeof(EXT_7TH) / sizeof(EXT_7TH[0])) },
    { KBD_CH_EXT_MAJ7, EXT_MAJ7, (uint8_t)(sizeof(EXT_MAJ7) / sizeof(EXT_MAJ7[0])) },
    { KBD_CH_EXT_6, EXT_6TH, (uint8_t)(sizeof(EXT_6TH) / sizeof(EXT_6TH[0])) },
    { KBD_CH_EXT_9, EXT_9TH, (uint8_t)(sizeof(EXT_9TH) / sizeof(EXT_9TH[0])) },
};

static void kbd_add_unique(uint8_t *buf, uint8_t *count, uint8_t value, uint8_t max_count)
{
    if ((*count >= max_count) || (buf == NULL) || (count == NULL))
    {
        return;
    }

    for (uint8_t i = 0U; i < *count; ++i)
    {
        if (buf[i] == value)
        {
            return;
        }
    }

    buf[*count] = value;
    (*count)++;
}

static void kbd_add_all(const uint8_t *src,
                        uint8_t src_count,
                        uint8_t *dst,
                        uint8_t *dst_count,
                        uint8_t max_count)
{
    if ((src == NULL) || (dst == NULL) || (dst_count == NULL))
    {
        return;
    }

    for (uint8_t i = 0U; i < src_count; ++i)
    {
        kbd_add_unique(dst, dst_count, src[i], max_count);
    }
}

static void kbd_isort_u8(uint8_t *values, uint8_t count)
{
    if (values == NULL)
    {
        return;
    }

    for (uint8_t i = 1U; i < count; ++i)
    {
        uint8_t key = values[i];
        uint8_t j = i;
        while ((j > 0U) && (values[j - 1U] > key))
        {
            values[j] = values[j - 1U];
            --j;
        }
        values[j] = key;
    }
}

static void kbd_add_components(uint8_t chord_mask,
                               const kbd_chord_component_t *components,
                               size_t component_count,
                               uint8_t *intervals,
                               uint8_t *count)
{
    for (size_t i = 0U; i < component_count; ++i)
    {
        const kbd_chord_component_t *component = &components[i];
        if ((chord_mask & component->mask) != 0U)
        {
            kbd_add_all(component->intervals, component->count, intervals, count, 12U);
        }
    }
}

bool kbd_chords_dict_build(uint8_t chord_mask, uint8_t *intervals, uint8_t *count)
{
    if ((intervals == NULL) || (count == NULL))
    {
        return false;
    }

    *count = 0U;

    const uint8_t bases = (uint8_t)(chord_mask & KBD_CH_MASK_BASES);
    const uint8_t exts = (uint8_t)(chord_mask & KBD_CH_MASK_EXTS);

    if (bases == 0U)
    {
        return false;
    }

    kbd_add_components(bases,
                       g_kbd_chord_bases,
                       sizeof(g_kbd_chord_bases) / sizeof(g_kbd_chord_bases[0]),
                       intervals,
                       count);

    kbd_add_components(exts,
                       g_kbd_chord_exts,
                       sizeof(g_kbd_chord_exts) / sizeof(g_kbd_chord_exts[0]),
                       intervals,
                       count);

    kbd_isort_u8(intervals, *count);
    return true;
}

static const int8_t g_kbd_scale_offsets[KBD_SCALE_COUNT][KBD_SCALE_SLOT_COUNT] = {
    [KBD_SCALE_ID_MAJOR] = {0, 2, 4, 5, 7, 9, 11, 12},
    [KBD_SCALE_ID_NAT_MINOR] = {0, 2, 3, 5, 7, 8, 10, 12},
    [KBD_SCALE_ID_DORIAN] = {0, 2, 3, 5, 7, 9, 10, 12},
    [KBD_SCALE_ID_MIXOLYDIAN] = {0, 2, 4, 5, 7, 9, 10, 12},
    [KBD_SCALE_ID_PENT_MAJOR] = {0, 2, 4, 7, 9, 12, 14, 16},
    [KBD_SCALE_ID_PENT_MINOR] = {0, 3, 5, 7, 10, 12, 15, 17},
    [KBD_SCALE_ID_CHROMATIC] = {0, 1, 2, 3, 4, 5, 6, 12},
};

int8_t kbd_scale_slot_semitone_offset(uint8_t scale_id, uint8_t slot)
{
    const uint8_t safe_scale = (scale_id < KBD_SCALE_COUNT) ? scale_id : KBD_SCALE_ID_MAJOR;
    const uint8_t safe_slot = (uint8_t)(slot % KBD_SCALE_SLOT_COUNT);
    return g_kbd_scale_offsets[safe_scale][safe_slot];
}
