#include "param_store.h"
#include "audio_float.h"
#include "stm32h7xx_hal.h"
#include <string.h>

typedef struct {
    float staging[PARAM_COUNT];
    float active[PARAM_COUNT];
    volatile uint32_t last_commit_block;
    volatile uint32_t commit_count;
    volatile uint8_t dirty;
} param_store_t;

static param_store_t g_ps;

void param_store_init(void)
{
    memset(&g_ps, 0, sizeof(g_ps));
    g_ps.last_commit_block = g_audio_block_counter;
}

void param_store_set_staging(param_id_t id, float v)
{
    if (id >= PARAM_COUNT) return;
    g_ps.staging[id] = v;
    g_ps.dirty = 1U;
}

bool param_store_commit_if_block_advanced(void)
{
    if (g_ps.dirty == 0U) return false;

    uint32_t b = g_audio_block_counter;

    // overflow-safe compare
    if ((uint32_t)(b - g_ps.last_commit_block) == 0U)
        return false;

    memcpy(g_ps.active, g_ps.staging, sizeof(g_ps.active));

    __DMB();

    g_ps.last_commit_block = b;
    g_ps.commit_count++;
    g_ps.dirty = 0U;

    return true;
}

float param_store_get_active(param_id_t id)
{
    if (id >= PARAM_COUNT) return 0.0f;
    return g_ps.active[id];
}

uint32_t param_store_get_commit_count(void)
{
    return g_ps.commit_count;
}

uint32_t param_store_get_last_commit_block(void)
{
    return g_ps.last_commit_block;
}
