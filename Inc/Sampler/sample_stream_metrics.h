#pragma once

#include <stdint.h>

typedef struct
{
    uint32_t calls;
    uint32_t total_cycles_lo;
    uint32_t total_cycles_hi;
    uint32_t max_cycles;
} sample_stream_metric_t;

typedef enum
{
    SAMPLE_STREAM_METRIC_SERVICE = 0,
    SAMPLE_STREAM_METRIC_MANAGER,
    SAMPLE_STREAM_METRIC_ROUND_ROBIN,
    SAMPLE_STREAM_METRIC_SD_SCHEDULER,
    SAMPLE_STREAM_METRIC_WORKER,
    SAMPLE_STREAM_METRIC_POLL_COMPLETION,
    SAMPLE_STREAM_METRIC_SPAN_LBA,
    SAMPLE_STREAM_METRIC_DECODE,
    SAMPLE_STREAM_METRIC_COPY,
    SAMPLE_STREAM_METRIC_PUBLISH,
    SAMPLE_STREAM_METRIC_REFILL_TOTAL,
    SAMPLE_STREAM_METRIC_COUNT
} sample_stream_metric_id_t;

typedef struct
{
    volatile uint32_t state;
    uint32_t reserved;
    sample_stream_metric_t metric[SAMPLE_STREAM_METRIC_COUNT];
} sample_stream_metrics_t;

extern volatile sample_stream_metrics_t g_sample_stream_metrics;

uint32_t sample_stream_metrics_begin(void);
void sample_stream_metrics_end(sample_stream_metric_id_t metric,
                               uint32_t start_cycles);
void sample_stream_metrics_refill_begin(uint16_t slot_index);
void sample_stream_metrics_refill_ready(uint16_t slot_index);
