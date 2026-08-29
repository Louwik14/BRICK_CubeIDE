#include "Sampler/sample_page_cache.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "Platform/cache_maintenance.h"
#include "Platform/intercore_cache.h"
#include "Storage/audio_recorder.h"
#include "Sampler/sample_stream_fatfs_map.h"
#include "Sampler/sample_stream_needs.h"
#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_stream_transport.h"
#include "stm32h7xx.h"

#define SAMPLE_PAGE_SLOT_FLOAT_CAPACITY (SAMPLE_PAGE_BYTES / sizeof(float))
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(float) == SAMPLE_PAGE_SAMPLE_BYTES, "sample_page_cache expects 32-bit float");
_Static_assert((SAMPLE_PAGE_SLOT_FLOAT_CAPACITY * sizeof(float)) == SAMPLE_PAGE_BYTES,
               "sample page slot must remain exactly one physical page");
#endif

#define SAMPLE_PAGE_INDEX_SIZE (SAMPLE_PAGE_MAX_COUNT * 2U)

typedef struct
{
    sample_audio_key_t key;
    uint16_t sample_id;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t page_index;
    uint32_t start_frame;
    uint32_t frame_count;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    uint32_t data_offset;
    volatile sample_page_state_t state;
    uint16_t pin_count;
    uint16_t reserved;
    uint32_t generation;
    uint8_t load_cancel_requested;
    uint8_t lifecycle_reserved[3];
    uint32_t last_touch;
} sample_page_desc_t;

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

typedef struct
{
    uint8_t used;
    sample_audio_key_t key;
    uint16_t slot_index;
    uint32_t page_index;
} sample_page_index_entry_t;

CONTROL_STREAM_META_SDRAM static sample_page_desc_t g_sample_page_desc[SAMPLE_PAGE_MAX_COUNT];
AUDIO_SHARED_PAGE_PAYLOAD_SDRAM static float g_sample_page_data[SAMPLE_PAGE_MAX_COUNT][SAMPLE_PAGE_SLOT_FLOAT_CAPACITY];
/* M7 owns increments/decrements; M4 only reads the credit before recycling.
 * SRAM3 is shareable non-cacheable, so no cache line is written by both cores. */
D2_IPC static volatile uint32_t g_sample_page_audio_use_count[SAMPLE_PAGE_MAX_COUNT];
static CTRL_STATE sample_page_cache_state_t g_sample_page_cache_state;
CONTROL_STREAM_META_SDRAM static sample_page_sample_desc_t g_sample_page_sample_desc[SAMPLE_PAGE_CACHE_MAX_SAMPLES];
D2_IPC static volatile uint16_t g_sample_page_last_slot[SAMPLE_PAGE_CACHE_MAX_SAMPLES];
CONTROL_STREAM_INDEX_SDRAM static sample_page_index_entry_t g_sample_page_index[SAMPLE_PAGE_INDEX_SIZE];
static CTRL_STATE uint16_t g_sample_page_reserved_count[SAMPLE_PAGE_CACHE_MAX_SAMPLES];
static CTRL_STATE uint16_t g_sample_page_free_cursor;
static CTRL_STATE uint16_t g_sample_page_evict_cursor;


/* Index, Stream registry, lifecycle, reservations and AUDIO references remain in their original sequence.
 * Private fragments share this translation unit to preserve static state and call order. */

#include "PageCache/sample_page_cache_index.inc"

#include "PageCache/sample_page_cache_stream_registry.inc"

#include "PageCache/sample_page_cache_lifecycle.inc"

#include "PageCache/sample_page_cache_reservation.inc"

#include "PageCache/sample_page_cache_audio_refs.inc"
