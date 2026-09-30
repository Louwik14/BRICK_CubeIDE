#include "Sampler/sample_page_cache_backing.h"

#include "Platform/memory_layout.h"

AUDIO_WARM sample_page_backing_descriptor_t
    g_sample_page_descriptor[SAMPLE_PAGE_MAX_COUNT];
SDRAM_PAGE_POOL float g_sample_page_data
    [SAMPLE_PAGE_MAX_COUNT][SAMPLE_PAGE_SLOT_FLOAT_CAPACITY];
STREAM_LOCAL_D2 volatile uint16_t
    g_sample_page_last_slot[SAMPLE_PAGE_CACHE_MAX_SAMPLES];
AUDIO_WARM sample_page_backing_index_entry_t
    g_sample_page_index[SAMPLE_PAGE_INDEX_SIZE];
