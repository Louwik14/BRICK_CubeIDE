#include "Platform/superloop_diag.h"

#include "stm32h7xx.h"

volatile uint32_t
    g_superloop_diag_service_count[SUPERLOOP_DIAG_SERVICE_COUNT];
volatile uint32_t
    g_superloop_diag_service_max_cycles[SUPERLOOP_DIAG_SERVICE_COUNT];
volatile uint32_t g_superloop_diag_interval_max_service_id;
volatile uint32_t g_superloop_diag_interval_max_cycles;
volatile uint32_t g_usb_audio_diag_transport_gap_service_id;
volatile uint32_t g_usb_audio_diag_transport_gap_service_cycles;

uint32_t superloop_diag_begin(void)
{
    return DWT->CYCCNT;
}

void superloop_diag_end(superloop_diag_service_id_t service_id,
                        uint32_t started_cycles)
{
    const uint32_t elapsed = DWT->CYCCNT - started_cycles;

    ++g_superloop_diag_service_count[service_id];
    if (elapsed > g_superloop_diag_service_max_cycles[service_id]) {
        g_superloop_diag_service_max_cycles[service_id] = elapsed;
    }
    if (elapsed > g_superloop_diag_interval_max_cycles) {
        g_superloop_diag_interval_max_cycles = elapsed;
        g_superloop_diag_interval_max_service_id = (uint32_t)service_id;
    }
}

void superloop_diag_interval_reset(void)
{
    g_superloop_diag_interval_max_service_id = UINT32_MAX;
    g_superloop_diag_interval_max_cycles = 0U;
}

void superloop_diag_reset(void)
{
    uint32_t i;

    for (i = 0U; i < (uint32_t)SUPERLOOP_DIAG_SERVICE_COUNT; ++i) {
        g_superloop_diag_service_count[i] = 0U;
        g_superloop_diag_service_max_cycles[i] = 0U;
    }
    superloop_diag_interval_reset();
    g_usb_audio_diag_transport_gap_service_id = UINT32_MAX;
    g_usb_audio_diag_transport_gap_service_cycles = 0U;
}
