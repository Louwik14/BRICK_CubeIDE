#include "Param/param_registry.h"

#include <stddef.h>

#include "Audio/control_audio_command.h"
#include "Audio/control_audio_fifo.h"
#include "Core/control_audio_publication.h"
#include "Core/control_music_output.h"
#include "Core/live_clock.h"
#include "Core/track_runtime.h"
#include "Seq/seq_runtime.h"
#include "UI/ui_track_catalog.h"

static volatile uint8_t g_global_depth;
static volatile uint8_t g_track_depth[SEQ_LANE_CAPACITY];

static uint8_t publish_transition(uint8_t track, uint8_t global, uint32_t active)
{
    uint64_t sample = 0U;
    if (live_clock_read_audio_sample(&sample) == 0U) return 0U;
    sample = control_music_output_first_unpublished_sample(sample);
    return control_audio_publish_param(global != 0U ? 0U : track,
        global != 0U ? CONTROL_AUDIO_PARAM_TRANSITION_GLOBAL
                     : CONTROL_AUDIO_PARAM_TRANSITION_TRACK,
        active, 0U, sample);
}

static uint8_t begin_global(void)
{
    if ((g_global_depth == 255U)
            || ((g_global_depth == 0U) && (control_audio_publication_free()
                < (CONTROL_AUDIO_FIFO_CONTRACT_BURST + 2U)))
            || (publish_transition(0U, 1U, 1U) == 0U)) return 0U;
    ++g_global_depth;
    return 1U;
}

static uint8_t end_global(void)
{
    if (g_global_depth == 0U) return 0U;
    const uint8_t next = (uint8_t)(g_global_depth - 1U);
    if (publish_transition(0U, 1U, (uint32_t)(next != 0U)) == 0U) return 0U;
    g_global_depth = next;
    return 1U;
}

static uint8_t begin_track(uint8_t track)
{
    if ((track >= SEQ_LANE_CAPACITY) || (g_track_depth[track] == 255U)
            || ((g_track_depth[track] == 0U) && (control_audio_publication_free()
                < (CONTROL_AUDIO_FIFO_CONTRACT_BURST + 2U)))
            || (publish_transition(track, 0U, 1U) == 0U)) return 0U;
    ++g_track_depth[track];
    return 1U;
}

static uint8_t end_track(uint8_t track)
{
    if ((track >= SEQ_LANE_CAPACITY) || (g_track_depth[track] == 0U)) return 0U;
    const uint8_t next = (uint8_t)(g_track_depth[track] - 1U);
    if (publish_transition(track, 0U, (uint32_t)(next != 0U)) == 0U) return 0U;
    g_track_depth[track] = next;
    return 1U;
}

uint8_t param_registry_track_structure_transition_is_active(void)
{
    if (g_global_depth != 0U) return 1U;
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
        if (g_track_depth[track] != 0U) return 1U;
    return 0U;
}

uint8_t param_registry_track_structure_transition_is_global_active(void)
{
    return (uint8_t)(g_global_depth != 0U);
}

uint8_t param_registry_track_structure_transition_is_track_active(uint8_t track)
{
    return (uint8_t)((track < SEQ_LANE_CAPACITY) && (g_track_depth[track] != 0U));
}

static uint8_t run_tail(const param_registry_track_transition_pipeline_cmd_t *cmd)
{
    if ((cmd->seq_runtime_sync_fn != NULL)
            && (cmd->seq_runtime_sync_fn(cmd->ctx) == 0U)) return 0U;
    if ((cmd->ui_sync_fn != NULL) && (cmd->ui_sync_fn(cmd->ctx) == 0U)) return 0U;
    if ((cmd->resume_fn != NULL) && (cmd->resume_fn(cmd->ctx) == 0U)) return 0U;
    return 1U;
}

uint8_t param_registry_run_track_transition_pipeline(
    const param_registry_track_transition_pipeline_cmd_t *cmd)
{
    if ((cmd == NULL) || (cmd->mutate_fn == NULL) || (begin_global() == 0U))
        return 0U;
    uint8_t ok = (uint8_t)(((cmd->prepare_fn == NULL)
            || (cmd->prepare_fn(cmd->ctx) != 0U))
        && (cmd->mutate_fn(cmd->ctx) != 0U));
    if (ok != 0U) track_runtime_rebuild_all();
    if (end_global() == 0U) ok = 0U;
    return (ok != 0U) ? run_tail(cmd) : 0U;
}

uint8_t param_registry_run_track_transition_pipeline_for_track(
    const param_registry_track_transition_pipeline_cmd_t *cmd, uint8_t track)
{
    if ((cmd == NULL) || (cmd->mutate_fn == NULL)
            || (begin_track(track) == 0U)) return 0U;
    uint8_t ok = (uint8_t)(((cmd->prepare_fn == NULL)
            || (cmd->prepare_fn(cmd->ctx) != 0U))
        && (cmd->mutate_fn(cmd->ctx) != 0U));
    if (ok != 0U) track_runtime_rebuild_track(track);
    if (end_track(track) == 0U) ok = 0U;
    return (ok != 0U) ? run_tail(cmd) : 0U;
}

static uint8_t mutate_legacy(void *opaque)
{
    const param_registry_track_structure_transition_cmd_t *const cmd =
        (const param_registry_track_structure_transition_cmd_t *)opaque;
    if ((cmd == NULL) || (cmd->mutation_fn == NULL)) return 0U;
    cmd->mutation_fn(cmd->mutation_ctx);
    return 1U;
}

void param_registry_apply_track_structure_transition(
    const param_registry_track_structure_transition_cmd_t *cmd)
{
    const param_registry_track_transition_pipeline_cmd_t pipeline = {
        .mutate_fn = mutate_legacy, .ctx = (void *)cmd
    };
    (void)param_registry_run_track_transition_pipeline(&pipeline);
}
