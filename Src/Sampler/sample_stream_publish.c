#include "Sampler/sample_stream_publish.h"
#include "Sampler/sample_stream_metrics.h"

uint8_t sample_stream_publish_result(const sample_stream_io_result_t *result)
{
    const uint32_t metric_start = sample_stream_metrics_begin();
    if (result == 0)
    {
        sample_stream_metrics_end(SAMPLE_STREAM_METRIC_PUBLISH, metric_start);
        return 0U;
    }

    const sample_page_finish_result_t finish =
        (result->load_result == SAMPLE_PAGE_LOAD_OK)
            ? SAMPLE_PAGE_FINISH_READY
            : SAMPLE_PAGE_FINISH_ERROR;
    const uint8_t published =
        sample_page_cache_finish_loading(&result->token, finish);
    sample_stream_metrics_end(SAMPLE_STREAM_METRIC_PUBLISH, metric_start);
    return published;
}
