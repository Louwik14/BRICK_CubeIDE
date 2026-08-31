#include "Audio/audio_boot_diagnostic_producer.h"
#include "IPC/audio_boot_diagnostic_layout.h"
#include "stm32h7xx.h"
#include <string.h>

static audio_boot_diag_snapshot_t g_audio_diag;

static void publish(void)
{
    uint32_t sequence = g_audio_boot_diag_layout.sequence;
    if ((sequence & 1U) != 0U) ++sequence;
    g_audio_boot_diag_layout.sequence = sequence + 1U;
    __DMB();
    g_audio_boot_diag_layout.snapshot = g_audio_diag;
    __DMB();
    g_audio_boot_diag_layout.sequence = sequence + 2U;
}

void audio_boot_diag_producer_init(void)
{
    memset(&g_audio_diag, 0, sizeof(g_audio_diag));
    memset(&g_audio_boot_diag_layout, 0, sizeof(g_audio_boot_diag_layout));
    publish();
}

void audio_boot_diag_producer_publish_state(audio_init_state_t state,
                                            board_audio_boot_error_t error)
{
    g_audio_diag.state = state;
    g_audio_diag.error = error;
    publish();
}

void audio_boot_diag_producer_publish_cpu(uint8_t valid, uint32_t avg_permille)
{
    if (avg_permille > UINT16_MAX) avg_permille = UINT16_MAX;
    g_audio_diag.cpu_load_valid = (valid != 0U) ? 1U : 0U;
    g_audio_diag.avg_permille = (uint16_t)avg_permille;
    publish();
}
