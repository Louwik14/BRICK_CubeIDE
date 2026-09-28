#include "SD/sd_scheduler_runtime.h"

#include <string.h>

#include "Sampler/sample_stream_backend_physical.h"
#include "Storage/sd_access_gate.h"
#include "Storage/audio_recorder_storage.h"
#include "Storage/rec_sd_trace.h"
#include "stm32h7xx_hal.h"

typedef struct
{
    sd_scheduler_provider_t provider;
    sd_access_client_t gate_client;
    uint8_t gate_held;
} sd_scheduler_runtime_provider_t;

static sd_scheduler_t g_sd_scheduler_runtime;
static sd_scheduler_runtime_provider_t g_sd_scheduler_read;
static sd_scheduler_runtime_provider_t g_sd_scheduler_write;
static sd_scheduler_runtime_provider_t g_sd_scheduler_filesystem;
static uint8_t g_sd_scheduler_background_active;
static uint8_t g_sd_scheduler_exclusive_requested;
static uint8_t g_sd_scheduler_exclusive_active;
static uint32_t g_trace_recorder_scheduler_refusal;
static uint32_t g_trace_recorder_scheduler_wait_seen;
static uint8_t g_trace_recorder_scheduler_wait_phase;

static void sd_scheduler_trace_recorder_wait(uint8_t reason)
{
    const audio_recorder_storage_phase_t phase = audio_recorder_storage_phase();
    if ((phase != AUDIO_RECORDER_STORAGE_PREPARING)
            && (phase != AUDIO_RECORDER_STORAGE_FINALIZING))
    {
        g_trace_recorder_scheduler_wait_seen = 0U;
        g_trace_recorder_scheduler_wait_phase = (uint8_t)phase;
        return;
    }
    if (g_trace_recorder_scheduler_wait_phase != (uint8_t)phase)
    {
        g_trace_recorder_scheduler_wait_phase = (uint8_t)phase;
        g_trace_recorder_scheduler_wait_seen = 0U;
    }
    const uint32_t bit = UINT32_C(1) << reason;
    if ((g_trace_recorder_scheduler_wait_seen & bit) != 0U) return;
    g_trace_recorder_scheduler_wait_seen |= bit;
    rec_sd_trace_note_sd(REC_SD_TRACE_SD_SCHED,
        0x100U | (uint32_t)reason | ((uint32_t)phase << 16U),
        (rec_sd_trace_sd_meta_t){
            .requester = SD_ACCESS_CLIENT_SCHEDULED_RECORDER,
            .operation = (phase == AUDIO_RECORDER_STORAGE_PREPARING)
                ? REC_SD_OP_PREPARE : REC_SD_OP_FINALIZE,
            .admission = REC_SD_ADMISSION_DEFERRED,
            .block_result = 0xFFU, .fatfs_result = 0xFFU });
}

static void sd_scheduler_trace_recorder_result(
    const sd_scheduler_runtime_provider_t *wrapper,
    const sd_scheduler_candidate_t *candidate, uint8_t result,
    uint8_t poll_result)
{
    if ((wrapper == 0)
            || (wrapper->gate_client != SD_ACCESS_CLIENT_SCHEDULED_RECORDER))
        return;
    const uint8_t operation = (candidate != 0)
        ? ((candidate->type == SD_SCHEDULER_CLASS_WRITE)
            ? REC_SD_OP_WRITE : REC_SD_OP_FINALIZE) : REC_SD_OP_NONE;
    const uint32_t signature = (uint32_t)operation
        | ((uint32_t)result << 8U)
        | ((uint32_t)sd_scheduler_owner(&g_sd_scheduler_runtime) << 16U)
        | ((uint32_t)sd_access_gate_current_owner() << 24U);
    if ((result == REC_SD_ADMISSION_ACCEPTED)
            || (result == REC_SD_ADMISSION_COMPLETED)) return;
    if ((result == REC_SD_ADMISSION_DEFERRED)
            && (signature == g_trace_recorder_scheduler_refusal)) return;
    if (result == REC_SD_ADMISSION_DEFERRED)
        g_trace_recorder_scheduler_refusal = signature;
    rec_sd_trace_note_sd(REC_SD_TRACE_SD_SCHED,
        (uint32_t)poll_result | ((candidate != 0)
            ? ((uint32_t)candidate->type << 8U) : 0U),
        (rec_sd_trace_sd_meta_t){
            .requester = SD_ACCESS_CLIENT_SCHEDULED_RECORDER,
            .operation = operation, .admission = result,
            .block_result = 0xFFU, .fatfs_result = 0xFFU });
}

sd_scheduler_class_t sd_scheduler_runtime_active_class(void)
{
    return g_sd_scheduler_runtime.active_class;
}

uint8_t sd_scheduler_runtime_exclusive_requested(void)
{
    return g_sd_scheduler_exclusive_requested;
}

uint8_t sd_scheduler_runtime_exclusive_active(void)
{
    return g_sd_scheduler_exclusive_active;
}

static uint8_t sd_scheduler_runtime_peek(void *context,
                                         sd_scheduler_candidate_t *candidate)
{
    sd_scheduler_runtime_provider_t *const wrapper = context;
    return ((wrapper != 0) && (wrapper->provider.peek != 0))
        ? wrapper->provider.peek(wrapper->provider.context, candidate) : 0U;
}

static sd_scheduler_start_result_t sd_scheduler_runtime_start(
    void *context,
    const sd_scheduler_candidate_t *candidate,
    uint32_t granted_sector_count)
{
    sd_scheduler_runtime_provider_t *const wrapper = context;
    if ((wrapper == 0) || (wrapper->provider.start == 0))
    {
        return SD_SCHEDULER_START_ERROR;
    }
    if(wrapper->gate_held != 0U)
    {
        if((wrapper->gate_client != SD_ACCESS_CLIENT_SAMPLE_STREAM)
                || (candidate == 0)
                || (candidate->type != SD_SCHEDULER_CLASS_READ))
        {
            return SD_SCHEDULER_START_ERROR;
        }
        return wrapper->provider.start(
            wrapper->provider.context, candidate, granted_sector_count);
    }
    if (sd_access_gate_try_acquire(wrapper->gate_client) == 0U)
    {
        sd_scheduler_trace_recorder_result(wrapper, candidate,
            REC_SD_ADMISSION_DEFERRED, SD_SCHEDULER_START_BUSY);
        return SD_SCHEDULER_START_BUSY;
    }
    wrapper->gate_held = 1U;
    const sd_scheduler_start_result_t result = wrapper->provider.start(
        wrapper->provider.context, candidate, granted_sector_count);
    sd_scheduler_trace_recorder_result(wrapper, candidate,
        (result == SD_SCHEDULER_START_BUSY) ? REC_SD_ADMISSION_DEFERRED
            : (result == SD_SCHEDULER_START_ERROR) ? REC_SD_ADMISSION_ERROR
                : REC_SD_ADMISSION_COMPLETED, (uint8_t)result);
    if (result != SD_SCHEDULER_START_STARTED)
    {
        sd_access_gate_release(wrapper->gate_client);
        wrapper->gate_held = 0U;
    }
    return result;
}

static sd_scheduler_poll_result_t sd_scheduler_runtime_poll(void *context)
{
    sd_scheduler_runtime_provider_t *const wrapper = context;
    if ((wrapper == 0) || (wrapper->provider.poll == 0)
            || (wrapper->gate_held == 0U))
    {
        return SD_SCHEDULER_POLL_ERROR;
    }
    const sd_scheduler_poll_result_t result =
        wrapper->provider.poll(wrapper->provider.context);
    if ((result == SD_SCHEDULER_POLL_ERROR)
            || (result == SD_SCHEDULER_POLL_RECOVERY_ABORT))
        sd_scheduler_trace_recorder_result(wrapper, 0,
            REC_SD_ADMISSION_ERROR, (uint8_t)result);
    if (result != SD_SCHEDULER_POLL_ACTIVE)
    {
        sd_access_gate_release(wrapper->gate_client);
        wrapper->gate_held = 0U;
    }
    return result;
}

static uint8_t sd_scheduler_runtime_bind_wrapped(
    sd_scheduler_class_t type,
    sd_scheduler_runtime_provider_t *wrapper,
    const sd_scheduler_provider_t *provider,
    sd_access_client_t client)
{
    if ((wrapper == 0) || (provider == 0))
    {
        return 0U;
    }
    memset(wrapper, 0, sizeof(*wrapper));
    wrapper->provider = *provider;
    wrapper->gate_client = client;
    const sd_scheduler_provider_t exposed = {
        .context = wrapper,
        .peek = sd_scheduler_runtime_peek,
        .start = sd_scheduler_runtime_start,
        .poll = (provider->poll != 0) ? sd_scheduler_runtime_poll : 0,
    };
    return sd_scheduler_bind_provider(&g_sd_scheduler_runtime, type, &exposed);
}

void sd_scheduler_runtime_init(void)
{
    sd_scheduler_config_t config;
    sd_scheduler_default_config(&config);
    sd_scheduler_init(&g_sd_scheduler_runtime, &config);
    g_sd_scheduler_background_active = 0U;
    g_sd_scheduler_exclusive_requested = 0U;
    g_sd_scheduler_exclusive_active = 0U;
    g_trace_recorder_scheduler_refusal = UINT32_MAX;
    g_trace_recorder_scheduler_wait_seen = 0U;
    g_trace_recorder_scheduler_wait_phase = AUDIO_RECORDER_STORAGE_IDLE;
    const sd_scheduler_provider_t read_provider =
        sample_stream_backend_physical_read_provider();
    (void)sd_scheduler_runtime_bind_wrapped(
        SD_SCHEDULER_CLASS_READ, &g_sd_scheduler_read, &read_provider,
        SD_ACCESS_CLIENT_SAMPLE_STREAM);
}

uint8_t sd_scheduler_runtime_bind_recorder(
    const sd_scheduler_provider_t *write_provider,
    const sd_scheduler_provider_t *filesystem_provider)
{
    return (uint8_t)(sd_scheduler_runtime_bind_wrapped(
                SD_SCHEDULER_CLASS_WRITE, &g_sd_scheduler_write,
                write_provider, SD_ACCESS_CLIENT_SCHEDULED_RECORDER)
        && sd_scheduler_runtime_bind_wrapped(
                SD_SCHEDULER_CLASS_FILESYSTEM, &g_sd_scheduler_filesystem,
                filesystem_provider, SD_ACCESS_CLIENT_SCHEDULED_RECORDER));
}

void sd_scheduler_runtime_service(void)
{
    const audio_recorder_storage_phase_t recorder_phase =
        audio_recorder_storage_phase();
    if ((recorder_phase != AUDIO_RECORDER_STORAGE_PREPARING)
            && (recorder_phase != AUDIO_RECORDER_STORAGE_FINALIZING))
    {
        g_trace_recorder_scheduler_wait_seen = 0U;
        g_trace_recorder_scheduler_wait_phase = (uint8_t)recorder_phase;
    }
    if ((g_sd_scheduler_background_active != 0U)
        || (g_sd_scheduler_exclusive_active != 0U)
        || ((g_sd_scheduler_exclusive_requested != 0U)
            && (sd_scheduler_owner(&g_sd_scheduler_runtime)
                == SD_SCHEDULER_OWNER_IDLE)))
    {
        sd_scheduler_trace_recorder_wait(
            (g_sd_scheduler_background_active != 0U) ? 1U
                : (g_sd_scheduler_exclusive_active != 0U) ? 2U : 3U);
        return;
    }
    const uint32_t now_us = HAL_GetTick() * 1000U;
    const uint32_t media_epoch = sd_access_media_epoch();
    if((g_sd_scheduler_exclusive_requested == 0U)
            && (sd_scheduler_owner(&g_sd_scheduler_runtime)
                == SD_SCHEDULER_OWNER_READ_DMA))
    {
        (void)sd_scheduler_prepare_read_chain(
            &g_sd_scheduler_runtime, now_us, media_epoch);
    }
    sd_scheduler_service(&g_sd_scheduler_runtime, now_us, media_epoch);
    const sd_scheduler_owner_t owner =
        sd_scheduler_owner(&g_sd_scheduler_runtime);
    if ((owner == SD_SCHEDULER_OWNER_READ_DMA)
            || (owner == SD_SCHEDULER_OWNER_WRITE_DMA)
            || (owner == SD_SCHEDULER_OWNER_RECOVERY_ABORT))
        sd_scheduler_trace_recorder_wait((uint8_t)(4U + owner));
}

sd_scheduler_background_admission_t sd_scheduler_runtime_background_try_begin(
    const sd_scheduler_background_request_t *request)
{
    if ((request == 0)
        || (request->kind > SD_SCHEDULER_BACKGROUND_METADATA)
        || ((request->kind == SD_SCHEDULER_BACKGROUND_DATA)
            && ((request->byte_count == 0U)
                || (request->byte_count
                    > SD_SCHEDULER_BULK_COPY_MAX_DATA_BYTES)))
        || ((request->kind == SD_SCHEDULER_BACKGROUND_METADATA)
            && (request->byte_count != 0U))
        || (request->media_epoch != sd_access_media_epoch()))
    {
        return SD_SCHEDULER_BACKGROUND_INVALID;
    }
    if ((g_sd_scheduler_background_active != 0U)
        || (g_sd_scheduler_exclusive_requested != 0U)
        || (g_sd_scheduler_exclusive_active != 0U)
        || (sd_scheduler_background_can_start(
                &g_sd_scheduler_runtime, request->media_epoch) == 0U))
    {
        return SD_SCHEDULER_BACKGROUND_NOT_NOW;
    }
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_BACKGROUND) == 0U)
    {
        return SD_SCHEDULER_BACKGROUND_NOT_NOW;
    }
    g_sd_scheduler_background_active = 1U;
    return SD_SCHEDULER_BACKGROUND_GO;
}

void sd_scheduler_runtime_background_end(void)
{
    if (g_sd_scheduler_background_active == 0U)
    {
        return;
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_BACKGROUND);
    g_sd_scheduler_background_active = 0U;
}

uint8_t sd_scheduler_runtime_background_active(void)
{
    return g_sd_scheduler_background_active;
}

void sd_scheduler_runtime_exclusive_request(void)
{
    g_sd_scheduler_exclusive_requested = 1U;
}

uint8_t sd_scheduler_runtime_exclusive_try_begin(void)
{
    if ((g_sd_scheduler_exclusive_requested == 0U)
        || (g_sd_scheduler_exclusive_active != 0U)
        || (g_sd_scheduler_background_active != 0U)
        || (sd_scheduler_owner(&g_sd_scheduler_runtime)
            != SD_SCHEDULER_OWNER_IDLE)
        || (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U))
    {
        return 0U;
    }
    g_sd_scheduler_exclusive_active = 1U;
    return 1U;
}

void sd_scheduler_runtime_exclusive_end(void)
{
    if (g_sd_scheduler_exclusive_active != 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    }
    g_sd_scheduler_exclusive_active = 0U;
    g_sd_scheduler_exclusive_requested = 0U;
}

sd_scheduler_owner_t sd_scheduler_runtime_owner(void)
{
    if (g_sd_scheduler_background_active != 0U)
    {
        return SD_SCHEDULER_OWNER_BACKGROUND;
    }
    if (g_sd_scheduler_exclusive_active != 0U)
    {
        return SD_SCHEDULER_OWNER_FILESYSTEM;
    }
    return sd_scheduler_owner(&g_sd_scheduler_runtime);
}
