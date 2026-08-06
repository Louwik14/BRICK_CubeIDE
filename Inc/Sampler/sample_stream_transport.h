#pragma once

#include <stdint.h>

#include "Sampler/sample_stream_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_STREAM_TRANSPORT_ABI_VERSION (1U)

typedef struct
{
    uint32_t submitted;
    uint32_t completed;
    uint32_t busy_rejections;
    uint32_t protocol_errors;
    uint32_t next_sequence;
    uint32_t completed_sequence;
} sample_stream_transport_stats_t;

void sample_stream_transport_init(void);
uint8_t sample_stream_transport_submit(const sample_stream_io_command_t *command,
                                       uint32_t *out_sequence);
void sample_stream_transport_worker_poll(void);
uint8_t sample_stream_transport_take_result(uint32_t expected_sequence,
                                            sample_stream_io_result_t *out_result);
void sample_stream_transport_execute_monocore(const sample_stream_io_command_t *command,
                                              sample_stream_io_result_t *out_result);
void sample_stream_transport_get_stats(sample_stream_transport_stats_t *out_stats);

#ifdef __cplusplus
}
#endif
