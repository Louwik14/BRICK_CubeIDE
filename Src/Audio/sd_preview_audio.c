#include "Audio/sd_preview_audio.h"

#include "IPC/sd_preview_ring_contract.h"
#include "IPC/storage_io_wakeup.h"
#include "stm32h7xx.h"

static float g_sd_preview_gain;
static uint8_t g_sd_preview_active;

void sd_preview_audio_init(void)
{
    g_sd_preview_ring_layout.read_count = 0U;
    g_sd_preview_gain = 1.0f;
    g_sd_preview_active = 0U;
    __DMB();
}

static uint8_t sd_preview_ring_pop(float *left, float *right)
{
    const uint32_t read_count = g_sd_preview_ring_layout.read_count;
    const uint32_t write_count = g_sd_preview_ring_layout.write_count;
    const uint8_t was_full = ((write_count - read_count)
                              >= SD_PREVIEW_RING_FRAMES) ? 1U : 0U;
    __DMB();
    if ((read_count == write_count) || (left == 0) || (right == 0)) return 0U;
    const uint32_t index = read_count % SD_PREVIEW_RING_FRAMES;
    *left = g_sd_preview_ring[index * 2U];
    *right = g_sd_preview_ring[index * 2U + 1U];
    __DMB();
    g_sd_preview_ring_layout.read_count = read_count + 1U;
    if (was_full != 0U)
        storage_io_owner_wakeup(STORAGE_OWNER_PREVIEW);
    return 1U;
}

uint8_t sd_preview_render_main(float *out_main_l, float *out_main_r,
                               uint32_t frames)
{
    if ((out_main_l == 0) || (out_main_r == 0) || (frames == 0U)
            || (g_sd_preview_active == 0U)) return 0U;
    uint8_t mixed = 0U;
    for (uint32_t i = 0U; i < frames; ++i)
    {
        float left = 0.0f;
        float right = 0.0f;
        if (sd_preview_ring_pop(&left, &right) == 0U) break;
        out_main_l[i] += left * g_sd_preview_gain;
        out_main_r[i] += right * g_sd_preview_gain;
        mixed = 1U;
    }
    return mixed;
}

uint8_t sd_preview_audio_apply_active(uint8_t active)
{
    g_sd_preview_active = (active != 0U) ? 1U : 0U;
    if (g_sd_preview_active == 0U)
    {
        g_sd_preview_ring_layout.read_count = g_sd_preview_ring_layout.write_count;
        __DMB();
    }
    return 1U;
}

uint8_t sd_preview_audio_apply_gain(uint32_t gain_bits)
{
    union { uint32_t u; float f; } decoded = { .u = gain_bits };
    g_sd_preview_gain = decoded.f;
    return 1U;
}
