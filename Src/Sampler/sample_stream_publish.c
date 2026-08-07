#include "Sampler/sample_stream_publish.h"

uint8_t sample_stream_publish_result(const sample_stream_io_result_t *result)
{
    if (result == 0)
    {
        return 0U;
    }

    const sample_page_finish_result_t finish =
        (result->load_result == SAMPLE_PAGE_LOAD_OK)
            ? SAMPLE_PAGE_FINISH_READY
            : SAMPLE_PAGE_FINISH_ERROR;
    return sample_page_cache_finish_loading(&result->token, finish);
}
