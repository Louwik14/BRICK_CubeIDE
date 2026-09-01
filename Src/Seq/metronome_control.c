#include "Seq/metronome_control.h"
#include "IPC/control_audio_command.h"
#include "ControlRT/control_rt_publication.h"

static uint8_t g_metronome_level;

void metronome_control_init(void) { g_metronome_level = 0U; }
uint8_t metronome_control_get_level(void) { return g_metronome_level; }
uint8_t metronome_control_set_level(uint8_t level)
{
    if (level > 127U) level = 127U;
    if (g_metronome_level == level) return 1U;
    if (control_rt_publish_param_now(
                    0U, CONTROL_AUDIO_PARAM_METRONOME_LEVEL,
                    level, 0U) == 0U)
        return 0U;
    g_metronome_level = level;
    return 1U;
}
