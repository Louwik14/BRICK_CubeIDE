#include "Storage/rec_sd_trace.h"

#include "Platform/memory_layout.h"
#include "SD/sd_block_device.h"
#include "SD/sd_scheduler_runtime.h"
#include "Storage/sd_access_gate.h"
#include "sdmmc.h"
#include "stm32h7xx.h"

IRQ_SHARED_D2 volatile rec_sd_trace_entry_t
    g_rec_sd_trace[REC_SD_TRACE_CAPACITY] __attribute__((used, externally_visible));
IRQ_SHARED_D2 volatile uint32_t g_rec_sd_trace_next
    __attribute__((used, externally_visible));

static void rec_sd_trace_write(rec_sd_trace_event_t event, uint32_t states,
                               uint32_t context, uint32_t detail,
                               uint32_t frames, uint32_t session,
                               uint64_t sample, uint32_t sd_owner,
                               uint32_t sd_flags, uint32_t sd_io,
                               uint32_t sd_result, uint32_t sd_hal_error)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint32_t next = g_rec_sd_trace_next;
    volatile rec_sd_trace_entry_t *const entry =
        &g_rec_sd_trace[next % REC_SD_TRACE_CAPACITY];
    entry->sequence = 0U;
    entry->event = (uint32_t)event;
    entry->states = states;
    entry->context = context;
    entry->detail = detail;
    entry->frames = frames;
    entry->session = session;
    entry->sample_lo = (uint32_t)sample;
    entry->sd_owner = sd_owner;
    entry->sd_flags = sd_flags;
    entry->sd_io = sd_io;
    entry->sd_result = sd_result;
    entry->sd_hal_error = sd_hal_error;
    __DMB();
    entry->sequence = next + 1U;
    g_rec_sd_trace_next = next + 1U;
    __set_PRIMASK(primask);
}

void rec_sd_trace_log(rec_sd_trace_event_t event, uint32_t states,
                      uint32_t context, uint32_t detail, uint32_t frames,
                      uint32_t session, uint64_t sample)
{
    rec_sd_trace_write(event, states, context, detail, frames, session,
                       sample, 0U, 0U, 0U, 0U, 0U);
}

void rec_sd_trace_log_sd(rec_sd_trace_event_t event, uint32_t states,
                         uint32_t context, uint32_t detail, uint32_t frames,
                         uint32_t session, uint64_t sample,
                         rec_sd_trace_sd_meta_t meta)
{
    sd_block_device_debug_snapshot_t block;
    sd_block_device_debug_snapshot(&block);
    const uint32_t owner = (uint32_t)sd_access_gate_current_owner()
        | ((uint32_t)meta.requester << 8U)
        | ((uint32_t)sd_scheduler_runtime_owner() << 16U)
        | ((uint32_t)sd_scheduler_runtime_active_class() << 24U);
    const uint32_t flags = (uint32_t)sd_access_gate_held_count()
        | ((uint32_t)(sd_access_gate_streaming_critical_active()
            | (sd_access_gate_recorder_fs_logical_active() << 1U)
            | (sd_scheduler_runtime_background_active() << 2U)
            | (sd_scheduler_runtime_exclusive_requested() << 3U)
            | (sd_scheduler_runtime_exclusive_active() << 4U)) << 8U)
        | ((uint32_t)sd_block_device_async_hardware_state() << 16U)
        | ((uint32_t)block.pending << 24U);
    const uint32_t io = (uint32_t)block.operation
        | ((uint32_t)block.owner_client << 8U)
        | ((uint32_t)block.fault_latched << 16U)
        | ((uint32_t)block.irq_error << 17U)
        | ((uint32_t)sd_access_storage_status() << 24U);
    const uint32_t result = (uint32_t)meta.admission
        | ((uint32_t)meta.block_result << 8U)
        | ((uint32_t)meta.fatfs_result << 16U)
        | ((uint32_t)meta.operation << 24U);
    rec_sd_trace_write(event, states, context, detail, frames, session,
                       sample, owner, flags, io, result,
                       HAL_SD_GetError(&hsd1));
}

void rec_sd_trace_note_sd(rec_sd_trace_event_t event, uint32_t detail,
                          rec_sd_trace_sd_meta_t meta)
{
    rec_sd_trace_log_sd(event,
        REC_SD_TRACE_STATES(0xFFU, 0xFFU, 0xFFU, 0xFFU),
        REC_SD_TRACE_CONTEXT(0xFFU, 0xFFU, 0xFFU, 0xFFU),
        detail, 0U, 0U, 0U, meta);
}
