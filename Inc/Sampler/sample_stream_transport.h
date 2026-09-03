#pragma once

#include <stdint.h>

#include "Sampler/sample_stream_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_STREAM_TRANSPORT_ABI_VERSION (2U)

typedef struct
{
    uint32_t submitted;
    uint32_t completed;
    uint32_t busy_rejections;
    uint32_t protocol_errors;
    uint32_t stale_completions;
    uint32_t payload_bytes;
    uint32_t next_sequence;
    uint32_t completed_sequence;
} sample_stream_transport_stats_t;

void sample_stream_transport_init(void);
uint8_t sample_stream_transport_submit(const sample_stream_io_command_t *command,
                                       uint32_t *out_sequence);
uint8_t sample_stream_transport_can_submit(void);
void sample_stream_transport_worker_poll(void);
uint8_t sample_stream_transport_take_result(uint32_t expected_sequence,
                                            sample_stream_io_result_t *out_result);
uint8_t sample_stream_transport_request_release(sample_audio_key_t key);
void sample_stream_transport_release_map(
    const sample_stream_physical_map_t *map);
void sample_stream_transport_reset_storage_maps(void);
void sample_stream_transport_get_stats(sample_stream_transport_stats_t *out_stats);

#ifdef __cplusplus
}
#endif
