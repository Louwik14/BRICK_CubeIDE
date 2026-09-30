#include "Sampler/sample_page_cache_audio.h"
#include "Sampler/sample_page_cache_backing.h"

#include <string.h>

#include "stm32h7xx.h"

typedef sample_page_backing_descriptor_t sample_page_desc_t;
typedef sample_page_backing_index_entry_t sample_page_index_entry_t;

void sample_page_cache_audio_init(void)
{
}

#define g_sample_page_desc g_sample_page_descriptor
static float *sample_page_cache_audio_data_resolve(
    const sample_page_desc_t *page)
{
    if ((page == NULL) || (page->data_offset >= sizeof(g_sample_page_data))
            || ((page->data_offset % SAMPLE_PAGE_BYTES) != 0U))
        return NULL;
    return (float *)((uint8_t *)g_sample_page_data + page->data_offset);
}

#define sample_page_cache_data_resolve sample_page_cache_audio_data_resolve

#include "../Sampler/PageCache/sample_page_cache_audio_index.inc"
#include "../Sampler/PageCache/sample_page_cache_audio_lifecycle.inc"
