#include "Seq/metronome_control.h"
#include "IPC/control_audio_command.h"
#include "IPC/control_audio_publication.h"
#include "IPC/live_clock_control.h"

static uint8_t g_metronome_level;

void metronome_control_init(void) { g_metronome_level = 0U; }
uint8_t metronome_control_get_level(void) { return g_metronome_level; }
uint8_t metronome_control_set_level(uint8_t level)
{
    if (level > 127U) level = 127U;
    if (g_metronome_level == level) return 1U;
    uint64_t sample = 0U;
    if ((live_clock_read_audio_sample(&sample) == 0U)
            || (control_audio_publish_param(
                    0U, CONTROL_AUDIO_PARAM_METRONOME_LEVEL,
                    level, 0U, sample) == 0U))
        return 0U;
    g_metronome_level = level;
    return 1U;
}
