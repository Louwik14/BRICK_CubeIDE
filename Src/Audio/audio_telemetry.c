#include "Audio/audio_telemetry.h"

#include <string.h>

#include "Seq/seq_model.h"

typedef struct
{
    audio_telemetry_ram_playhead_t ram_playhead[SEQ_TRACK_COUNT];
    uint32_t ram_playhead_generation[SEQ_TRACK_COUNT];
    uint32_t published;
    uint32_t overwritten;
} audio_telemetry_state_t;

static volatile audio_telemetry_state_t g_audio_telemetry;

void audio_telemetry_init(void)
{
    memset((void *)&g_audio_telemetry, 0, sizeof(g_audio_telemetry));
}

void audio_telemetry_publish_ram_playhead_from_audio(uint8_t track,
                                                     const audio_telemetry_ram_playhead_t *snapshot)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if ((snapshot == 0) || (snapshot->active == 0U))
    {
        memset((void *)&g_audio_telemetry.ram_playhead[track],
               0,
               sizeof(g_audio_telemetry.ram_playhead[track]));
    }
    else
    {
        g_audio_telemetry.ram_playhead[track] = *snapshot;
    }

    if (g_audio_telemetry.ram_playhead_generation[track] != 0U)
    {
        g_audio_telemetry.overwritten++;
    }
    g_audio_telemetry.ram_playhead_generation[track]++;
    if (g_audio_telemetry.ram_playhead_generation[track] == 0U)
    {
        g_audio_telemetry.ram_playhead_generation[track] = 1U;
    }
    g_audio_telemetry.published++;
}

uint8_t audio_telemetry_get_ram_playhead(uint8_t track,
                                         uint16_t sample_id,
                                         audio_telemetry_ram_playhead_t *out_snapshot)
{
    if (out_snapshot != 0)
    {
        memset(out_snapshot, 0, sizeof(*out_snapshot));
    }
    if ((track >= SEQ_TRACK_COUNT) || (out_snapshot == 0))
    {
        return 0U;
    }

    const audio_telemetry_ram_playhead_t snapshot = g_audio_telemetry.ram_playhead[track];
    if ((snapshot.active == 0U)
        || (snapshot.sample_id != sample_id)
        || (snapshot.frame_count == 0U))
    {
        return 0U;
    }

    *out_snapshot = snapshot;
    return 1U;
}

void audio_telemetry_diag_snapshot(audio_telemetry_diag_t *out_diag)
{
    if (out_diag == 0)
    {
        return;
    }
    out_diag->ram_playhead_published = g_audio_telemetry.published;
    out_diag->ram_playhead_overwritten = g_audio_telemetry.overwritten;
}
