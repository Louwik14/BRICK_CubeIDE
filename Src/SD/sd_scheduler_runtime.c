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
    sd_scheduler_service(&g_sd_scheduler_runtime,
                         HAL_GetTick() * 1000U,
                         sd_access_media_epoch());
}

sd_scheduler_owner_t sd_scheduler_runtime_owner(void)
{
    return sd_scheduler_owner(&g_sd_scheduler_runtime);
}

void sd_scheduler_runtime_metrics_get(sd_scheduler_metrics_t *metrics)
{
    sd_scheduler_metrics_get(&g_sd_scheduler_runtime, metrics);
}
