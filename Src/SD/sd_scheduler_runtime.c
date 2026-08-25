#include "SD/sd_scheduler_runtime.h"

#include <string.h>

#include "Sampler/sample_stream_backend_physical.h"
#include "Storage/sd_access_gate.h"
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
    if ((wrapper == 0) || (wrapper->provider.start == 0)
            || (wrapper->gate_held != 0U))
    {
        return SD_SCHEDULER_START_ERROR;
    }
    if (sd_access_gate_try_acquire(wrapper->gate_client) == 0U)
    {
        return SD_SCHEDULER_START_BUSY;
    }
    wrapper->gate_held = 1U;
    const sd_scheduler_start_result_t result = wrapper->provider.start(
        wrapper->provider.context, candidate, granted_sector_count);
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
        .poll = (type == SD_SCHEDULER_CLASS_FILESYSTEM)
            ? 0 : sd_scheduler_runtime_poll,
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
    if ((g_sd_scheduler_background_active != 0U)
        || (g_sd_scheduler_exclusive_active != 0U)
        || ((g_sd_scheduler_exclusive_requested != 0U)
            && (sd_scheduler_owner(&g_sd_scheduler_runtime)
                == SD_SCHEDULER_OWNER_IDLE)))
    {
        return;
    }
    sd_scheduler_service(&g_sd_scheduler_runtime,
                         HAL_GetTick() * 1000U,
                         sd_access_media_epoch());
}

sd_scheduler_background_admission_t sd_scheduler_runtime_background_try_begin(
    const sd_scheduler_background_request_t *request)
{
    if ((request == 0)
        || (request->kind > SD_SCHEDULER_BACKGROUND_METADATA)
        || ((request->kind == SD_SCHEDULER_BACKGROUND_DATA)
            && ((request->byte_count == 0U)
                || (request->byte_count
                    > SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES)))
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

void sd_scheduler_runtime_metrics_get(sd_scheduler_metrics_t *metrics)
{
    sd_scheduler_metrics_get(&g_sd_scheduler_runtime, metrics);
}
