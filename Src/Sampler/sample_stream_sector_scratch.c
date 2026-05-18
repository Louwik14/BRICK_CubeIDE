#include "Sampler/sample_stream_sector_scratch.h"

#include "Storage/memory_layout.h"

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert((SAMPLE_STREAM_SECTOR_SCRATCH_BYTES % 512U) == 0U,
               "STREAM sector scratch must stay sector sized");
#endif

STORAGE_SCRATCH_SDRAM static uint8_t
    g_sample_stream_sector_scratch[SAMPLE_STREAM_SECTOR_SCRATCH_BYTES];

uint8_t *sample_stream_sector_scratch_buffer(void)
{
    return g_sample_stream_sector_scratch;
}

uint32_t sample_stream_sector_scratch_size_bytes(void)
{
    return SAMPLE_STREAM_SECTOR_SCRATCH_BYTES;
}
