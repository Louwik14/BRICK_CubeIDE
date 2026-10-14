#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "Audio/Publications/audio_boot_diagnostic_reader.h"
#include "sim_engine.h"

static uint32_t g_sim_tick;

uint32_t HAL_GetTick(void) { return g_sim_tick; }
uint32_t sim_engine_hal_tick(void) { return g_sim_tick; }
void sim_engine_set_hal_tick(uint32_t tick) { g_sim_tick = tick; }
uint32_t brick_media_clock_now_tick(void) { return g_sim_tick; }

void audio_boot_diag_read(audio_boot_diag_snapshot_t *out_diag)
{
    if (out_diag != NULL)
    {
        memset(out_diag, 0, sizeof(*out_diag));
        out_diag->state = AUDIO_INIT_READY;
    }
}

uint8_t ui_boot_loading_is_active(void) { return 0U; }
void ui_boot_loading_render(void) {}
void ui_boot_loading_note_frame_rendered(void) {}
