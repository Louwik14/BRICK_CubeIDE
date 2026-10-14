#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "Audio/Publications/audio_boot_diagnostic_reader.h"
#include "ControlRT/live_parameter_audio_publication.h"
#include "sim_engine.h"

static uint32_t g_sim_tick;

uint32_t HAL_GetTick(void) { return g_sim_tick; }
uint32_t sim_engine_hal_tick(void) { return g_sim_tick; }
void sim_engine_set_hal_tick(uint32_t tick) { g_sim_tick = tick; }
uint32_t brick_media_clock_now_tick(void) { return g_sim_tick; }

/* The host has no AUDIO consumer.  Acceptance at this publication boundary is
 * nevertheless required before the real registry installs the canonical
 * value into its CONTROL-owned state. */
void live_parameter_audio_publication_init(void) {}
bool live_parameter_audio_publication_submit_bulk(
    const live_parameter_audio_bulk_t *bulk)
{
    return bulk != NULL && bulk->count <= LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS;
}
bool live_parameter_audio_publication_submit(
    uint32_t capture_tick, const live_parameter_audio_target_t *target)
{
    (void)capture_tick;
    return target != NULL;
}
bool live_parameter_audio_publication_submit_tone_program(
    uint8_t track, track_runtime_type_t type)
{
    (void)type;
    return track < TRACK_COUNT;
}
bool live_parameter_audio_publication_submit_dated(
    uint64_t effective_sample_time, uint16_t parameter_id,
    uint8_t track, uint16_t value16,
    control_audio_param_semantic_t semantic)
{
    (void)effective_sample_time;
    (void)parameter_id;
    (void)value16;
    (void)semantic;
    return track < TRACK_COUNT;
}

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
