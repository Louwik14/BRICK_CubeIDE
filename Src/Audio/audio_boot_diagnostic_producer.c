#include "Audio/audio_boot_diagnostic_producer.h"
#include "IPC/audio_boot_diagnostic_layout.h"
#include "stm32h7xx.h"
#include <string.h>

static audio_boot_diag_snapshot_t g_audio_diag;

static uint32_t audio_boot_diag_lock(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void audio_boot_diag_unlock(uint32_t primask)
{
    __set_PRIMASK(primask);
}

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
    const uint32_t primask = audio_boot_diag_lock();
    memset(&g_audio_diag, 0, sizeof(g_audio_diag));
    memset(&g_audio_boot_diag_layout, 0, sizeof(g_audio_boot_diag_layout));
    publish();
    audio_boot_diag_unlock(primask);
}

void audio_boot_diag_producer_publish_state(audio_init_state_t state,
                                            board_audio_boot_error_t error)
{
    const uint32_t primask = audio_boot_diag_lock();
    g_audio_diag.state = (uint8_t)state;
    g_audio_diag.error = (uint8_t)error;
    publish();
    audio_boot_diag_unlock(primask);
}

void audio_boot_diag_producer_publish_cpu(uint8_t valid, uint32_t avg_permille)
{
    const uint32_t primask = audio_boot_diag_lock();
    if (avg_permille > UINT16_MAX) avg_permille = UINT16_MAX;
    g_audio_diag.cpu_load_valid = (valid != 0U) ? 1U : 0U;
    g_audio_diag.avg_permille = (uint16_t)avg_permille;
    publish();
    audio_boot_diag_unlock(primask);
}
