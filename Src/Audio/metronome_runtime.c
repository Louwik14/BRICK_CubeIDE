#include "Audio/metronome_runtime.h"

#include <string.h>

#define METRO_MAX_GAIN                 0.085f
#define METRO_NORMAL_GAIN_TRIM         0.82f
#define METRO_ACCENT_GAIN_TRIM         1.00f
#define METRO_PHASE_START_Q32          0x40000000UL
#define METRO_NORMAL_PHASE_INC_Q32     0x05900000UL
#define METRO_ACCENT_PHASE_INC_Q32     0x08400000UL
#define METRO_NORMAL_ENV_COEFF         0.9899f
#define METRO_ACCENT_ENV_COEFF         0.9924f
#define METRO_NORMAL_FRAMES            384U
#define METRO_ACCENT_FRAMES            512U
#define METRO_NOISE_ENABLED            1U
#define METRO_NORMAL_NOISE_FRAMES      64U
#define METRO_ACCENT_NOISE_FRAMES      96U
#define METRO_NORMAL_NOISE_GAIN        0.20f
#define METRO_ACCENT_NOISE_GAIN        0.28f
#define METRO_TABLE_SCALE              32767.0f
#define METRO_TABLE_INV_SCALE          (1.0f / 32768.0f)

typedef struct
{
    uint8_t active;
    uint8_t level_u7;
    uint8_t pending;
    metronome_click_type_t pending_type;
    uint16_t pending_offset;
    const int16_t *table;
    uint16_t table_len;
    uint16_t table_pos;
    float level_gain;
    float click_gain;
} metronome_runtime_state_t;

static metronome_runtime_state_t g_metronome;
static int16_t g_metronome_normal_table[METRO_NORMAL_FRAMES];
static int16_t g_metronome_accent_table[METRO_ACCENT_FRAMES];
static uint8_t g_metronome_tables_ready;

static float metronome_level_to_gain(uint8_t level)
{
    if (level == 0U)
    {
        return 0.0f;
    }

    if (level > 127U)
    {
        level = 127U;
    }

    const float norm = (float)level * (1.0f / 127.0f);
    return norm * norm * METRO_MAX_GAIN;
}

static float metronome_triangle_q32(uint32_t phase_q32)
{
    const float p = (float)phase_q32 * (1.0f / 4294967296.0f);
    float tri = 0.0f;
    if (p < 0.5f)
    {
        tri = (p * 4.0f) - 1.0f;
    }
    else
    {
        tri = 3.0f - (p * 4.0f);
    }

    const float abs_tri = (tri < 0.0f) ? -tri : tri;
    return tri * (2.0f - abs_tri);
}

static float metronome_noise(uint32_t *lfsr)
{
    uint32_t x = *lfsr;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    if (x == 0U)
    {
        x = 0x6D2B79F5UL;
    }
    *lfsr = x;
    return ((x & 0x1U) != 0U) ? 1.0f : -1.0f;
}

static int16_t metronome_float_to_i16(float sample)
{
    if (sample > 0.98f)
    {
        sample = 0.98f;
    }
    else if (sample < -0.98f)
    {
        sample = -0.98f;
    }

    const float scaled = sample * METRO_TABLE_SCALE;
    return (int16_t)((scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f));
}

static void metronome_build_table(int16_t *table,
                                  uint16_t frames,
                                  uint32_t phase_inc_q32,
                                  uint16_t noise_frames,
                                  float noise_gain,
                                  float env_coeff)
{
    uint32_t phase_q32 = METRO_PHASE_START_Q32;
    uint32_t lfsr = 0x6D2B79F5UL;
    float env = 1.0f;

    for (uint16_t i = 0U; i < frames; ++i)
    {
        float sample = metronome_triangle_q32(phase_q32);
        phase_q32 += phase_inc_q32;

#if (METRO_NOISE_ENABLED != 0U)
        if (i < noise_frames)
        {
            sample += metronome_noise(&lfsr) * noise_gain;
        }
#else
        (void)noise_frames;
        (void)noise_gain;
#endif

        float attack = 1.0f;
        if (i < 4U)
        {
            attack = (float)i * 0.25f;
        }

        table[i] = metronome_float_to_i16(sample * env * attack);
        env *= env_coeff;
    }
}

static void metronome_build_tables(void)
{
    if (g_metronome_tables_ready != 0U)
    {
        return;
    }

    metronome_build_table(g_metronome_normal_table,
                          METRO_NORMAL_FRAMES,
                          METRO_NORMAL_PHASE_INC_Q32,
                          METRO_NORMAL_NOISE_FRAMES,
                          METRO_NORMAL_NOISE_GAIN,
                          METRO_NORMAL_ENV_COEFF);
    metronome_build_table(g_metronome_accent_table,
                          METRO_ACCENT_FRAMES,
                          METRO_ACCENT_PHASE_INC_Q32,
                          METRO_ACCENT_NOISE_FRAMES,
                          METRO_ACCENT_NOISE_GAIN,
                          METRO_ACCENT_ENV_COEFF);
    g_metronome_tables_ready = 1U;
}

static void metronome_start_click(metronome_click_type_t type)
{
    if (g_metronome.level_u7 == 0U)
    {
        return;
    }

    metronome_build_tables();

    const uint8_t accent = (type == METRONOME_CLICK_ACCENT) ? 1U : 0U;
    g_metronome.active = 1U;
    g_metronome.table = (accent != 0U) ? g_metronome_accent_table : g_metronome_normal_table;
    g_metronome.table_len = (accent != 0U) ? METRO_ACCENT_FRAMES : METRO_NORMAL_FRAMES;
    g_metronome.table_pos = 0U;
    g_metronome.click_gain = g_metronome.level_gain
                              * ((accent != 0U) ? METRO_ACCENT_GAIN_TRIM : METRO_NORMAL_GAIN_TRIM)
                              * METRO_TABLE_INV_SCALE;
}

void metronome_runtime_init(void)
{
    memset(&g_metronome, 0, sizeof(g_metronome));
    metronome_build_tables();
}

void metronome_runtime_set_level_u7(uint8_t level)
{
    if (level > 127U)
    {
        level = 127U;
    }

    g_metronome.level_u7 = level;
    g_metronome.level_gain = metronome_level_to_gain(level);
    if (level == 0U)
    {
        metronome_runtime_stop();
    }
}

void metronome_runtime_trigger_at(uint16_t offset, metronome_click_type_t type)
{
    if (g_metronome.level_u7 == 0U)
    {
        return;
    }

    if (offset == 0U)
    {
        metronome_start_click(type);
        g_metronome.pending = 0U;
        return;
    }

    if ((g_metronome.pending == 0U)
            || (type == METRONOME_CLICK_ACCENT)
            || (offset <= g_metronome.pending_offset))
    {
        g_metronome.pending = 1U;
        g_metronome.pending_type = type;
        g_metronome.pending_offset = offset;
    }
}

void metronome_runtime_render_main_monitor(float *main_l, float *main_r, uint32_t frames)
{
    if ((main_l == 0) || (main_r == 0) || (frames == 0U))
    {
        return;
    }

    if ((g_metronome.active == 0U) && (g_metronome.pending == 0U))
    {
        return;
    }

    uint32_t pos = 0U;
    while (pos < frames)
    {
        if (g_metronome.active == 0U)
        {
            if (g_metronome.pending == 0U)
            {
                break;
            }

            const uint32_t remaining = frames - pos;
            if (g_metronome.pending_offset >= remaining)
            {
                g_metronome.pending_offset = (uint16_t)(g_metronome.pending_offset - remaining);
                break;
            }

            pos += g_metronome.pending_offset;
            g_metronome.pending_offset = 0U;
            const metronome_click_type_t type = g_metronome.pending_type;
            g_metronome.pending = 0U;
            metronome_start_click(type);
        }

        if ((g_metronome.table == 0) || (g_metronome.table_pos >= g_metronome.table_len))
        {
            g_metronome.active = 0U;
            continue;
        }

        uint32_t run = frames - pos;
        const uint32_t table_remaining = (uint32_t)g_metronome.table_len - (uint32_t)g_metronome.table_pos;
        if (run > table_remaining)
        {
            run = table_remaining;
        }

        for (uint32_t i = 0U; i < run; ++i)
        {
            const float sample = (float)g_metronome.table[g_metronome.table_pos++] * g_metronome.click_gain;
            main_l[pos] += sample;
            main_r[pos] += sample;
            pos++;
        }

        if (g_metronome.table_pos >= g_metronome.table_len)
        {
            g_metronome.active = 0U;
        }
    }
}

void metronome_runtime_stop(void)
{
    g_metronome.active = 0U;
    g_metronome.pending = 0U;
    g_metronome.table = 0;
    g_metronome.table_len = 0U;
    g_metronome.table_pos = 0U;
}
