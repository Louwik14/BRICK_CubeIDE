#include "Storage/brick6_stream_service_task.h"

#include "Sampler/multi_sample_loader.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_stream_transport.h"
#include "Storage/sd_access_gate.h"
#include "Platform/memory_layout.h"
#include "SD/sd_scheduler_runtime.h"
#include "stm32h7xx.h"

static uint8_t brick6_stream_service_task_update_gate(void)
{
    const uint8_t pending = sample_stream_manager_has_pending_sd_work();
    sd_access_gate_set_streaming_critical(pending);
    return pending;
}

void brick6_stream_service_task_init(void)
{
    brick6_stream_service_task_update_gate();
}

void brick6_stream_service_task_poll(void)
{
    /* H743 local worker adapter. On H747 this whole service belongs to M4. */
    sd_scheduler_runtime_service();
    sample_stream_transport_worker_poll();
    const uint8_t pending = brick6_stream_service_task_update_gate();
    if (pending == 0U)
    {
        return;
    }

    if ((sample_stream_manager_io_in_flight() == 0U)
        && (multi_sample_load_is_active() != 0U))
    {
        return;
    }

    sample_cache_service(BRICK6_STREAM_SERVICE_BYTE_BUDGET);
    sd_scheduler_runtime_service();
    sample_stream_transport_worker_poll();
    brick6_stream_service_task_update_gate();
}
