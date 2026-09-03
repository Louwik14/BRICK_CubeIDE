#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_page_cache_shared_contract.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "Platform/cache_maintenance.h"
#include "Platform/intercore_cache.h"
#include "Storage/audio_recorder.h"
#include "Sampler/sample_stream_fatfs_map.h"
#include "Sampler/sample_page_lease_control.h"
#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_stream_transport.h"
#include "stm32h7xx.h"

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(float) == SAMPLE_PAGE_SAMPLE_BYTES, "sample_page_cache expects 32-bit float");
_Static_assert((SAMPLE_PAGE_SLOT_FLOAT_CAPACITY * sizeof(float)) == SAMPLE_PAGE_BYTES,
               "sample page slot must remain exactly one physical page");
#endif

typedef sample_page_shared_descriptor_t sample_page_desc_t;

typedef struct
{
    uint8_t initialized;
    uint8_t reserved[3];
    uint32_t generation_counter;
    uint32_t touch_counter;
    uint32_t registration_epoch_counter;
} sample_page_cache_state_t;

typedef struct
{
    sample_audio_key_t key;
    char path[SAMPLE_PAGE_CACHE_PATH_MAX];
    wav_info_t info;
    uint16_t first_slot;
    uint16_t page_count;
    uint32_t total_frames;
    volatile uint32_t readable_frames;
    uint32_t data_offset;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    sample_stream_safe_metadata_t stream_safe;
    uint8_t valid;
    uint8_t fully_loaded;
    uint8_t live_committed;
    uint8_t physical_only;
} sample_page_sample_desc_t;

typedef sample_page_shared_index_entry_t sample_page_index_entry_t;

static CTRL_STATE sample_page_cache_state_t g_sample_page_cache_state;
CONTROL_STREAM_META_SDRAM static sample_page_sample_desc_t g_sample_page_sample_desc[SAMPLE_PAGE_CACHE_MAX_SAMPLES];
static CTRL_STATE uint16_t g_sample_page_reserved_count[SAMPLE_PAGE_CACHE_MAX_SAMPLES];
static CTRL_STATE uint16_t g_sample_page_free_cursor;
static CTRL_STATE uint16_t g_sample_page_evict_cursor;

#define g_sample_page_desc g_sample_page_shared_descriptor
#define g_sample_page_data g_sample_page_shared_data
#define g_sample_page_last_slot g_sample_page_shared_last_slot
#define g_sample_page_index g_sample_page_shared_index


/* CONTROL-owned index, registry, lifecycle and reservations
 * remain in their original sequence and share this private mutable state. */

#include "PageCache/sample_page_cache_index.inc"

#include "PageCache/sample_page_cache_stream_registry.inc"

#include "PageCache/sample_page_cache_lifecycle.inc"

#include "PageCache/sample_page_cache_reservation.inc"

#include "PageCache/sample_page_cache_audio_refs.inc"

uint8_t sample_page_cache_control_resolve_page(uint16_t sample_id,
                                               uint32_t page_index,
                                               sample_page_span_t *out_span)
{
    if (out_span == NULL) return 0U;
    memset(out_span, 0, sizeof(*out_span));
    const sample_page_desc_t *const page = sample_page_cache_find_page_key(
        sample_audio_key_classic(sample_id), page_index);
    if ((page == NULL) || (page->state != SAMPLE_PAGE_READY)) return 0U;
    float *const payload = sample_page_cache_data_resolve(page);
    if (payload == NULL) return 0U;
    out_span->frames_interleaved = payload;
    out_span->frame_count = page->frame_count;
    out_span->start_frame = page->start_frame;
    out_span->page_index = page->page_index;
    out_span->page_generation = page->generation;
    out_span->key = page->key;
    out_span->format = (sample_audio_format_t)page->format;
    out_span->stride_floats = page->stride_floats;
    out_span->frames_per_page = page->frames_per_page;
    out_span->registration_epoch = page->registration_epoch;
    out_span->slot_index = (uint32_t)(page - g_sample_page_desc);
    return 1U;
}
