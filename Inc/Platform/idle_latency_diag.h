#pragma once

#include <stdint.h>

#include "stm32h7xx.h"

typedef enum
{
    IDLE_LATENCY_SERVICE_USB = 0,
    IDLE_LATENCY_SERVICE_ENGINE_TICK,
    IDLE_LATENCY_SERVICE_CONTROL,
    IDLE_LATENCY_SERVICE_STREAM,
    IDLE_LATENCY_SERVICE_AUDIO_BG_LOCAL,
    IDLE_LATENCY_SERVICE_SEQ,
    IDLE_LATENCY_SERVICE_STORAGE,
    IDLE_LATENCY_SERVICE_HALL_MIDI,
    IDLE_LATENCY_SERVICE_UI,
    IDLE_LATENCY_SERVICE_RENDER,
    IDLE_LATENCY_SERVICE_DISPLAY_FLUSH,
    IDLE_LATENCY_SERVICE_COUNT
} idle_latency_service_t;

typedef enum
{
    IDLE_LATENCY_STORAGE_RECORDER = 0,
    IDLE_LATENCY_STORAGE_PROJECT_SAVE,
    IDLE_LATENCY_STORAGE_PROJECT_LOAD,
    IDLE_LATENCY_STORAGE_PATCH,
    IDLE_LATENCY_STORAGE_MULTI_PRIORITY,
    IDLE_LATENCY_STORAGE_MULTI_RETIRE,
    IDLE_LATENCY_STORAGE_RAM_RETIRE,
    IDLE_LATENCY_STORAGE_WAVETABLE_RETIRE,
    IDLE_LATENCY_STORAGE_RAM_LOADER,
    IDLE_LATENCY_STORAGE_WAVETABLE_LOADER,
    IDLE_LATENCY_STORAGE_PROJECT_ASSET,
    IDLE_LATENCY_STORAGE_RAM_WAVEFORM,
    IDLE_LATENCY_STORAGE_MULTI,
    IDLE_LATENCY_STORAGE_PATTERN,
    IDLE_LATENCY_STORAGE_WAVEFORM_CACHE,
    IDLE_LATENCY_STORAGE_PREVIEW,
    IDLE_LATENCY_STORAGE_COUNT
} idle_latency_storage_service_t;

typedef struct
{
    volatile uint32_t call_count[IDLE_LATENCY_SERVICE_COUNT];
    volatile uint32_t last_cycles[IDLE_LATENCY_SERVICE_COUNT];
    volatile uint32_t max_cycles[IDLE_LATENCY_SERVICE_COUNT];
    volatile uint32_t slow_count[IDLE_LATENCY_SERVICE_COUNT];
    volatile uint32_t last_slow_service;
    volatile uint32_t last_slow_cycles;
    volatile uint32_t worst_service;
    volatile uint32_t worst_cycles;
    volatile uint32_t threshold_cycles;
    volatile uint32_t core_clock_hz;
    volatile uint32_t usb_attention_count;
    volatile uint32_t usb_int_low_count;
    volatile uint32_t usb_periodic_poll_count;
    volatile uint32_t usb_refresh_ok_count;
    volatile uint32_t usb_refresh_error_count;
    volatile uint32_t usb_retry_count;
    volatile uint32_t usb_attach_count;
    volatile uint32_t usb_detach_count;
    volatile uint32_t usb_device_start_count;
    volatile uint32_t usb_device_stop_count;
    volatile uint32_t usb_drp_restart_count;
    volatile uint32_t usb_watchdog_recovery_count;
    volatile uint32_t storage_call_count[IDLE_LATENCY_STORAGE_COUNT];
    volatile uint32_t storage_last_cycles[IDLE_LATENCY_STORAGE_COUNT];
    volatile uint32_t storage_max_cycles[IDLE_LATENCY_STORAGE_COUNT];
    volatile uint32_t storage_slow_count[IDLE_LATENCY_STORAGE_COUNT];
} idle_latency_diag_t;

extern volatile idle_latency_diag_t g_idle_latency_diag;

static inline uint32_t idle_latency_diag_begin(void)
{
    return DWT->CYCCNT;
}

static inline void idle_latency_storage_diag_end(
    idle_latency_storage_service_t service,
    uint32_t started)
{
    const uint32_t elapsed = DWT->CYCCNT - started;
    volatile idle_latency_diag_t *const diag = &g_idle_latency_diag;
    diag->storage_call_count[service]++;
    diag->storage_last_cycles[service] = elapsed;
    if (elapsed > diag->storage_max_cycles[service])
        diag->storage_max_cycles[service] = elapsed;
    if (elapsed >= diag->threshold_cycles)
        diag->storage_slow_count[service]++;
}

static inline void idle_latency_diag_end(idle_latency_service_t service,
                                         uint32_t started)
{
    const uint32_t elapsed = DWT->CYCCNT - started;
    volatile idle_latency_diag_t *const diag = &g_idle_latency_diag;
    diag->call_count[service]++;
    diag->last_cycles[service] = elapsed;
    if (elapsed > diag->max_cycles[service])
        diag->max_cycles[service] = elapsed;
    if (elapsed > diag->worst_cycles)
    {
        diag->worst_cycles = elapsed;
        diag->worst_service = (uint32_t)service;
    }
    if (elapsed >= diag->threshold_cycles)
    {
        diag->slow_count[service]++;
        diag->last_slow_cycles = elapsed;
        /* Written last so a GDB watchpoint sees a coherent capture. */
        diag->last_slow_service = (uint32_t)service;
    }
}
