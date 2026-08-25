#include "Sampler/sample_page_cache.h"

#include <string.h>

#include "Storage/memory_layout.h"
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
    float *data;
    volatile sample_page_state_t state;
    uint16_t pin_count;
    uint16_t use_count;
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

SDRAM_PAGE_META static sample_page_desc_t g_sample_page_desc[SAMPLE_PAGE_MAX_COUNT];
SDRAM_PAGE_POOL static float g_sample_page_data[SAMPLE_PAGE_MAX_COUNT][SAMPLE_PAGE_SLOT_FLOAT_CAPACITY];
static CTRL_STATE sample_page_cache_state_t g_sample_page_cache_state;
SDRAM_PAGE_META static sample_page_sample_desc_t g_sample_page_sample_desc[SAMPLE_PAGE_CACHE_MAX_SAMPLES];
static CTRL_STATE uint16_t g_sample_page_last_slot[SAMPLE_PAGE_CACHE_MAX_SAMPLES];
SDRAM_PAGE_INDEX static sample_page_index_entry_t g_sample_page_index[SAMPLE_PAGE_INDEX_SIZE];
static CTRL_STATE uint16_t g_sample_page_reserved_count[SAMPLE_PAGE_CACHE_MAX_SAMPLES];
static CTRL_STATE uint16_t g_sample_page_free_cursor;
static CTRL_STATE uint16_t g_sample_page_evict_cursor;

static uint16_t sample_page_cache_key_slot(sample_audio_key_t key);
static uint8_t sample_page_cache_page_is_contractual(const sample_page_desc_t *page);
static uint8_t sample_page_cache_can_evict_for_request(sample_audio_key_t request_key,
                                                       sample_audio_key_t victim_key);

static sample_audio_format_t sample_page_cache_format_key(sample_audio_key_t key)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
        if ((sample->valid != 0U) && (sample_audio_format_is_valid(sample->format) != 0U))
        {
            return sample->format;
        }
    }
    return SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED;
}

static void sample_page_cache_set_page_geometry(sample_page_desc_t *page,
                                                sample_audio_key_t key)
{
    if (page == 0)
    {
        return;
    }

    page->format = sample_page_cache_format_key(key);
    page->stride_floats = (uint16_t)sample_audio_format_stride_floats(page->format);
    page->frames_per_page = sample_audio_format_frames_per_page(page->format);
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    page->registration_epoch = (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
                                   ? g_sample_page_sample_desc[key_slot].registration_epoch
                                   : 0U;
}

static uint32_t sample_page_cache_lock(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void sample_page_cache_unlock(uint32_t primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}

sample_audio_key_t sample_audio_key_classic(uint16_t sample_id)
{
    sample_audio_key_t key;
    key.domain = SAMPLE_AUDIO_DOMAIN_CLASSIC;
    key.object_id = sample_id;
    return key;
}

sample_audio_key_t sample_audio_key_looper(uint16_t looper_id)
{
    sample_audio_key_t key;
    key.domain = SAMPLE_AUDIO_DOMAIN_LOOPER;
    key.object_id = looper_id;
    return key;
}

sample_audio_key_t sample_audio_key_multi(uint16_t multi_sample_id)
{
    sample_audio_key_t key;
    key.domain = SAMPLE_AUDIO_DOMAIN_MULTI;
    key.object_id = multi_sample_id;
    return key;
}

uint8_t sample_audio_key_equal(const sample_audio_key_t *a, const sample_audio_key_t *b)
{
    if ((a == 0) || (b == 0))
    {
        return 0U;
    }

    return ((a->domain == b->domain) && (a->object_id == b->object_id)) ? 1U : 0U;
}

static uint16_t sample_page_cache_key_slot(sample_audio_key_t key)
{
    switch (key.domain)
    {
        case SAMPLE_AUDIO_DOMAIN_CLASSIC:
            return (key.object_id < SAMPLE_CACHE_HOT_SAMPLE_CAPACITY) ? key.object_id : UINT16_MAX;

        case SAMPLE_AUDIO_DOMAIN_LOOPER:
        {
            const uint32_t slot = (uint32_t)SAMPLE_PAGE_CACHE_LOOPER_ID_BASE
                                + (uint32_t)key.object_id;
            return (slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES) ? (uint16_t)slot : UINT16_MAX;
        }

        case SAMPLE_AUDIO_DOMAIN_MULTI:
        {
            const uint32_t slot = (uint32_t)SAMPLE_PAGE_CACHE_MULTI_ID_BASE
                                + (uint32_t)key.object_id;
            return (key.object_id < SAMPLE_PAGE_CACHE_MULTI_ID_CAPACITY)
                       ? (uint16_t)slot
                       : UINT16_MAX;
        }

        default:
            break;
    }

    return UINT16_MAX;
}

static uint8_t sample_page_cache_key_valid(sample_audio_key_t key)
{
    return (sample_page_cache_key_slot(key) < SAMPLE_PAGE_CACHE_MAX_SAMPLES) ? 1U : 0U;
}

static uint8_t sample_page_cache_can_evict_for_request(sample_audio_key_t request_key,
                                                       sample_audio_key_t victim_key)
{
    if ((request_key.domain == SAMPLE_AUDIO_DOMAIN_MULTI)
        && (victim_key.domain != SAMPLE_AUDIO_DOMAIN_MULTI))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t sample_page_cache_page_is_full_contract(const sample_page_desc_t *page)
{
    if (page == 0)
    {
        return 0U;
    }

    const uint16_t key_slot = sample_page_cache_key_slot(page->key);
    if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        return 0U;
    }

    const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    return ((sample->valid != 0U) && (sample->fully_loaded != 0U)) ? 1U : 0U;
}

static uint8_t sample_page_cache_page_is_contractual(const sample_page_desc_t *page)
{
    if (page == 0)
    {
        return 0U;
    }

    return ((page->use_count != 0U)
            || (page->pin_count != 0U)
            || (sample_stream_needs_registry_contains_any(page->key,
                                                          page->page_index,
                                                          page->registration_epoch) != 0U)
            || (sample_page_cache_page_is_full_contract(page) != 0U)) ? 1U : 0U;
}

static uint32_t sample_page_cache_hash_key(sample_audio_key_t key, uint32_t page_index)
{
    return ((((uint32_t)key.object_id * 2654435761UL)
             ^ ((uint32_t)key.domain * 40503UL)
             ^ (page_index * 2246822519UL))
           % SAMPLE_PAGE_INDEX_SIZE);
}

static uint8_t sample_page_cache_index_find_slot_key(sample_audio_key_t key,
                                                 uint32_t page_index,
                                                 uint16_t *out_slot)
{
    const uint32_t start = sample_page_cache_hash_key(key, page_index);

    for (uint32_t probe = 0U; probe < SAMPLE_PAGE_INDEX_SIZE; ++probe)
    {
        const uint32_t index = (start + probe) % SAMPLE_PAGE_INDEX_SIZE;
        const sample_page_index_entry_t *const entry = &g_sample_page_index[index];
        if (entry->used == 0U)
        {
            return 0U;
        }

        if ((entry->used == 1U) && (sample_audio_key_equal(&entry->key, &key) != 0U)
            && (entry->page_index == page_index))
        {
            if (out_slot != 0)
            {
                *out_slot = entry->slot_index;
            }
            return 1U;
        }
    }
    return 0U;
}

static void sample_page_cache_index_put_key(sample_audio_key_t key, uint32_t page_index, uint16_t slot_index)
{
    const uint32_t start = sample_page_cache_hash_key(key, page_index);
    uint32_t first_deleted = UINT32_MAX;

    for (uint32_t probe = 0U; probe < SAMPLE_PAGE_INDEX_SIZE; ++probe)
    {
        const uint32_t index = (start + probe) % SAMPLE_PAGE_INDEX_SIZE;
        sample_page_index_entry_t *const entry = &g_sample_page_index[index];
        if ((entry->used == 1U) && (sample_audio_key_equal(&entry->key, &key) != 0U)
            && (entry->page_index == page_index))
        {
            entry->slot_index = slot_index;
            return;
        }
        if ((entry->used == 2U) && (first_deleted == UINT32_MAX))
        {
            first_deleted = index;
        }
        if (entry->used == 0U)
        {
            sample_page_index_entry_t *const dst =
                (first_deleted != UINT32_MAX) ? &g_sample_page_index[first_deleted] : entry;
            dst->used = 1U;
            dst->key.domain = key.domain;
            dst->key.object_id = key.object_id;
            dst->page_index = page_index;
            dst->slot_index = slot_index;
            return;
        }
    }

    if (first_deleted != UINT32_MAX)
    {
        sample_page_index_entry_t *const dst = &g_sample_page_index[first_deleted];
        dst->used = 1U;
        dst->key.domain = key.domain;
        dst->key.object_id = key.object_id;
        dst->page_index = page_index;
        dst->slot_index = slot_index;
    }
}

static void sample_page_cache_index_remove_key(sample_audio_key_t key, uint32_t page_index)
{
    const uint32_t start = sample_page_cache_hash_key(key, page_index);
    for (uint32_t probe = 0U; probe < SAMPLE_PAGE_INDEX_SIZE; ++probe)
    {
        const uint32_t index = (start + probe) % SAMPLE_PAGE_INDEX_SIZE;
        sample_page_index_entry_t *const entry = &g_sample_page_index[index];
        if (entry->used == 0U)
        {
            return;
        }
        if ((entry->used == 1U) && (sample_audio_key_equal(&entry->key, &key) != 0U)
            && (entry->page_index == page_index))
        {
            entry->used = 2U;
            return;
        }
    }
}

static void sample_page_cache_set_state(sample_page_desc_t *page, sample_page_state_t state)
{
    if (page == 0)
    {
        return;
    }

    const uint16_t key_slot = sample_page_cache_key_slot(page->key);
    if ((key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES) && (page->state != state))
    {
        if ((page->state == SAMPLE_PAGE_RESERVED) && (g_sample_page_reserved_count[key_slot] != 0U))
        {
            g_sample_page_reserved_count[key_slot]--;
        }
        if (state == SAMPLE_PAGE_RESERVED)
        {
            g_sample_page_reserved_count[key_slot]++;
        }
    }

    if (state == SAMPLE_PAGE_READY)
    {
        __DMB();
    }
    page->state = state;
}

static uint8_t sample_page_cache_claim_for_recycle(sample_page_desc_t *page)
{
    const uint32_t primask = sample_page_cache_lock();
    const uint8_t claimed = (uint8_t)((page != 0)
        && (page->state == SAMPLE_PAGE_READY)
        && (sample_page_cache_page_is_contractual(page) == 0U));
    if (claimed != 0U)
    {
        sample_page_cache_set_state(page, SAMPLE_PAGE_FREE);
    }
    sample_page_cache_unlock(primask);
    return claimed;
}

static void sample_page_cache_clear_desc(sample_page_desc_t *page, uint32_t slot_index)
{
    if (page == 0)
    {
        return;
    }

    const uint16_t key_slot = sample_page_cache_key_slot(page->key);
    if ((key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
        && (g_sample_page_last_slot[key_slot] == slot_index))
    {
        g_sample_page_last_slot[key_slot] = UINT16_MAX;
    }
    if ((key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES) && (page->page_index != UINT32_MAX))
    {
        sample_page_cache_index_remove_key(page->key, page->page_index);
    }
    sample_page_cache_set_state(page, SAMPLE_PAGE_FREE);

    memset(page, 0, sizeof(*page));
    page->sample_id = UINT16_MAX;
    page->key = sample_audio_key_classic(UINT16_MAX);
    page->page_index = UINT32_MAX;
    page->start_frame = UINT32_MAX;
    page->data = &g_sample_page_data[slot_index][0];
    page->state = SAMPLE_PAGE_FREE;
}

static sample_page_desc_t *sample_page_cache_find_page_mut_key(sample_audio_key_t key, uint32_t page_index)
{
    uint16_t slot = UINT16_MAX;
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        const uint16_t last_slot = g_sample_page_last_slot[key_slot];
        if (last_slot < SAMPLE_PAGE_MAX_COUNT)
        {
            sample_page_desc_t *const last_page = &g_sample_page_desc[last_slot];
            if ((sample_audio_key_equal(&last_page->key, &key) != 0U)
                && (last_page->page_index == page_index))
            {
                return last_page;
            }
        }

        if (sample_page_cache_index_find_slot_key(key, page_index, &slot) != 0U)
        {
            sample_page_desc_t *const page = &g_sample_page_desc[slot];
            if ((sample_audio_key_equal(&page->key, &key) != 0U)
                && (page->page_index == page_index)
                && (page->state != SAMPLE_PAGE_FREE))
            {
                g_sample_page_last_slot[key_slot] = slot;
                return page;
            }
        }
    }

    return 0;
}

static const sample_page_desc_t *sample_page_cache_find_page_key(sample_audio_key_t key, uint32_t page_index)
{
    return sample_page_cache_find_page_mut_key(key, page_index);
}

static uint8_t sample_page_cache_fill_ref(const sample_page_desc_t *page,
                                          sample_page_ref_t *out_ref)
{
    if ((page == 0) || (out_ref == 0))
    {
        return 0U;
    }

    out_ref->key = page->key;
    out_ref->page_index = page->page_index;
    out_ref->page_generation = page->generation;
    out_ref->format = page->format;
    out_ref->stride_floats = page->stride_floats;
    out_ref->frames_per_page = page->frames_per_page;
    out_ref->registration_epoch = page->registration_epoch;
    out_ref->slot_index = (uint32_t)(page - g_sample_page_desc);
    return 1U;
}

static uint8_t sample_page_cache_sample_is_stream_loadable_key(sample_audio_key_t key);

static uint32_t sample_page_cache_stream_page_frame_count_key(sample_audio_key_t key,
                                                              uint32_t page_index)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
        if ((sample->valid != 0U) && (sample->fully_loaded == 0U) && (sample->total_frames != 0U))
        {
            const sample_audio_format_t format = sample_audio_format_or_stereo(sample->format);
            const uint32_t frames_per_page = sample_audio_format_frames_per_page(format);
            const uint32_t start_frame = sample_audio_format_page_start_frame(format, page_index);
            if (start_frame >= sample->total_frames)
            {
                return 0U;
            }

            uint32_t frame_count = sample->total_frames - start_frame;
            if (frame_count > frames_per_page)
            {
                frame_count = frames_per_page;
            }
            if (((uint64_t)start_frame + frame_count) > sample->readable_frames)
            {
                return 0U;
            }
            return frame_count;
        }
    }

    return sample_audio_format_frames_per_page(SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED);
}

static uint8_t sample_page_cache_sample_is_stream_loadable_key(sample_audio_key_t key)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        return 0U;
    }

    const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    return ((sample->valid != 0U) && (sample->fully_loaded == 0U)
            && (sample->path[0] != '\0')) ? 1U : 0U;
}

static void sample_page_cache_alloc_range(sample_page_alloc_type_t alloc_type,
                                          uint32_t *out_start,
                                          uint32_t *out_count)
{
    uint32_t start = 0U;
    uint32_t count = SAMPLE_PAGE_MAX_COUNT;

    switch (alloc_type)
    {
        case SAMPLE_PAGE_ALLOC_SLOT_PERMANENT:
            start = SAMPLE_PAGE_SLOT_POOL_START;
            count = SAMPLE_PAGE_SLOT_POOL_COUNT;
            break;

        case SAMPLE_PAGE_ALLOC_VOICE_WINDOW:
            start = SAMPLE_PAGE_VOICE_WINDOW_POOL_START;
            count = SAMPLE_PAGE_VOICE_WINDOW_POOL_COUNT;
            break;

        case SAMPLE_PAGE_ALLOC_MARGIN:
            start = SAMPLE_PAGE_MARGIN_POOL_START;
            count = SAMPLE_PAGE_MARGIN_POOL_COUNT;
            break;

        case SAMPLE_PAGE_ALLOC_LEGACY_DEFAULT:
        default:
            break;
    }

    if (out_start != 0)
    {
        *out_start = start;
    }
    if (out_count != 0)
    {
        *out_count = count;
    }
}

static uint32_t sample_page_cache_range_cursor_offset(uint16_t cursor,
                                                      uint32_t range_start,
                                                      uint32_t range_count)
{
    if (((uint32_t)cursor >= range_start)
        && ((uint32_t)cursor < (range_start + range_count)))
    {
        return (uint32_t)cursor - range_start;
    }
    return 0U;
}

static sample_page_desc_t *sample_page_cache_alloc_empty_slot_key(sample_audio_key_t key,
                                                                  uint32_t page_index,
                                                                  sample_page_alloc_type_t alloc_type)
{
    sample_page_desc_t *evict_page = 0;
    const uint32_t frame_count = sample_page_cache_stream_page_frame_count_key(key, page_index);
    if (frame_count == 0U)
    {
        return 0;
    }

    uint32_t range_start = 0U;
    uint32_t range_count = SAMPLE_PAGE_MAX_COUNT;
    sample_page_cache_alloc_range(alloc_type, &range_start, &range_count);

    for (uint32_t scan = 0U; scan < range_count; ++scan)
    {
        const uint32_t cursor_offset =
            sample_page_cache_range_cursor_offset(g_sample_page_free_cursor,
                                                  range_start,
                                                  range_count);
        const uint32_t i = range_start + ((cursor_offset + scan) % range_count);
        sample_page_desc_t *const page = &g_sample_page_desc[i];
        if (page->state == SAMPLE_PAGE_FREE)
        {
            page->key = key;
            page->sample_id = sample_page_cache_key_slot(key);
            page->page_index = page_index;
            sample_page_cache_set_page_geometry(page, key);
            page->start_frame = sample_audio_format_page_start_frame(page->format, page_index);
            page->frame_count = frame_count;
            page->generation = ++g_sample_page_cache_state.generation_counter;
            page->last_touch = ++g_sample_page_cache_state.touch_counter;
            page->pin_count = 0U;
            page->use_count = 0U;
            sample_page_cache_set_state(page, SAMPLE_PAGE_RESERVED);
            const uint16_t key_slot = sample_page_cache_key_slot(key);
            if (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
            {
                sample_page_cache_index_put_key(key, page_index, (uint16_t)i);
                g_sample_page_last_slot[key_slot] = (uint16_t)i;
            }
            g_sample_page_free_cursor =
                (uint16_t)(range_start + (((i - range_start) + 1U) % range_count));
            return page;
        }
    }

    for (uint32_t scan = 0U; scan < range_count; ++scan)
    {
        const uint32_t cursor_offset =
            sample_page_cache_range_cursor_offset(g_sample_page_evict_cursor,
                                                  range_start,
                                                  range_count);
        const uint32_t i = range_start + ((cursor_offset + scan) % range_count);
        sample_page_desc_t *const page = &g_sample_page_desc[i];
        if ((page->state != SAMPLE_PAGE_READY)
            || (sample_page_cache_page_is_contractual(page) != 0U)
            || (sample_page_cache_key_slot(page->key) >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
            || (sample_page_cache_can_evict_for_request(key, page->key) == 0U))
        {
            continue;
        }

        if ((evict_page == 0) || (page->last_touch < evict_page->last_touch))
        {
            evict_page = page;
        }
    }

    if (evict_page == 0)
    {
        return 0;
    }

    if (sample_page_cache_claim_for_recycle(evict_page) == 0U)
    {
        return 0;
    }

    const uint32_t slot_index = (uint32_t)(evict_page - g_sample_page_desc);
    sample_page_cache_clear_desc(evict_page, slot_index);
    evict_page->key = key;
    evict_page->sample_id = sample_page_cache_key_slot(key);
    evict_page->page_index = page_index;
    sample_page_cache_set_page_geometry(evict_page, key);
    evict_page->start_frame = sample_audio_format_page_start_frame(evict_page->format, page_index);
    evict_page->frame_count = frame_count;
    evict_page->generation = ++g_sample_page_cache_state.generation_counter;
    evict_page->last_touch = ++g_sample_page_cache_state.touch_counter;
    evict_page->pin_count = 0U;
    evict_page->use_count = 0U;
    sample_page_cache_set_state(evict_page, SAMPLE_PAGE_RESERVED);
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        sample_page_cache_index_put_key(key, page_index, (uint16_t)slot_index);
        g_sample_page_last_slot[key_slot] = (uint16_t)slot_index;
    }
    g_sample_page_evict_cursor =
        (uint16_t)(range_start + (((slot_index - range_start) + 1U) % range_count));
    return evict_page;
}

static uint32_t sample_page_cache_trim_path_copy(char *dst, uint32_t dst_size, const char *src)
{
    uint32_t start = 0U;
    uint32_t end;

    if ((dst == 0) || (dst_size == 0U) || (src == 0))
    {
        return 0U;
    }

    end = (uint32_t)strlen(src);
    while ((start < end) && (((unsigned char)src[start]) <= ' '))
    {
        start++;
    }
    while ((end > start) && (((unsigned char)src[end - 1U]) <= ' '))
    {
        end--;
    }

    const uint32_t len = end - start;
    if ((len == 0U) || (len >= dst_size))
    {
        return 0U;
    }

    memcpy(dst, &src[start], len);
    dst[len] = '\0';
    return len;
}

static int32_t sample_page_cache_find_contiguous_empty_run_in_range(uint32_t page_count,
                                                                    uint32_t range_start,
                                                                    uint32_t range_count)
{
    if ((page_count == 0U) || (page_count > range_count)
        || ((range_start + range_count) > SAMPLE_PAGE_MAX_COUNT))
    {
        return -1;
    }

    for (uint32_t start = range_start; (start + page_count) <= (range_start + range_count); ++start)
    {
        uint32_t i = 0U;
        for (; i < page_count; ++i)
        {
            if (g_sample_page_desc[start + i].state != SAMPLE_PAGE_FREE)
            {
                break;
            }
        }

        if (i == page_count)
        {
            return (int32_t)start;
        }

        start += i;
    }

    return -1;
}

static void sample_page_cache_reclaim_stream_pages_for_full_load_in_range(uint32_t range_start,
                                                                          uint32_t range_count)
{
    if ((range_start + range_count) > SAMPLE_PAGE_MAX_COUNT)
    {
        return;
    }

    for (uint32_t i = range_start; i < (range_start + range_count); ++i)
    {
        sample_page_desc_t *const page = &g_sample_page_desc[i];
        if ((page->state != SAMPLE_PAGE_READY)
            || (sample_page_cache_page_is_contractual(page) != 0U)
            || (sample_page_cache_key_slot(page->key) >= SAMPLE_PAGE_CACHE_MAX_SAMPLES))
        {
            continue;
        }

        const sample_page_sample_desc_t *const sample =
            &g_sample_page_sample_desc[sample_page_cache_key_slot(page->key)];
        if ((sample->valid != 0U) && (sample->fully_loaded == 0U))
        {
            if (sample_page_cache_claim_for_recycle(page) != 0U)
            {
                sample_page_cache_clear_desc(page, i);
            }
        }
    }
}

static sample_page_desc_t *sample_page_cache_assign_slot_key(uint32_t slot_index,
                                                             sample_audio_key_t key,
                                                             uint32_t page_index,
                                                             uint32_t frame_count);

static sample_page_desc_t *sample_page_cache_assign_slot_key(uint32_t slot_index,
                                                             sample_audio_key_t key,
                                                             uint32_t page_index,
                                                             uint32_t frame_count)
{
    if (slot_index >= SAMPLE_PAGE_MAX_COUNT)
    {
        return 0;
    }

    sample_page_desc_t *const page = &g_sample_page_desc[slot_index];
    sample_page_cache_clear_desc(page, slot_index);
    page->key = key;
    page->sample_id = sample_page_cache_key_slot(key);
    page->page_index = page_index;
    sample_page_cache_set_page_geometry(page, key);
    page->start_frame = sample_audio_format_page_start_frame(page->format, page_index);
    page->frame_count = frame_count;
    page->generation = ++g_sample_page_cache_state.generation_counter;
    page->last_touch = ++g_sample_page_cache_state.touch_counter;
    sample_page_cache_set_state(page, SAMPLE_PAGE_RESERVED);
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        sample_page_cache_index_put_key(key, page_index, (uint16_t)slot_index);
        g_sample_page_last_slot[key_slot] = (uint16_t)slot_index;
    }
    return page;
}

void sample_page_cache_init(void)
{
    sample_page_cache_reset();
    g_sample_page_cache_state.initialized = 1U;
}

void sample_page_cache_reset(void)
{
    sample_stream_transport_reset_storage_maps();
    memset(&g_sample_page_cache_state, 0, sizeof(g_sample_page_cache_state));
    memset(g_sample_page_sample_desc, 0, sizeof(g_sample_page_sample_desc));
    memset(g_sample_page_index, 0, sizeof(g_sample_page_index));
    memset(g_sample_page_reserved_count, 0, sizeof(g_sample_page_reserved_count));
    g_sample_page_free_cursor = 0U;
    g_sample_page_evict_cursor = 0U;
    for (uint32_t i = 0U; i < SAMPLE_PAGE_CACHE_MAX_SAMPLES; ++i)
    {
        g_sample_page_last_slot[i] = UINT16_MAX;
    }
    for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
    {
        sample_page_cache_clear_desc(&g_sample_page_desc[i], i);
    }
}

void sample_page_cache_clear_sample(uint16_t sample_id)
{
    sample_page_cache_clear_key(sample_audio_key_classic(sample_id));
}

void sample_page_cache_clear_key(sample_audio_key_t key)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        return;
    }

    for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
    {
        if (sample_audio_key_equal(&g_sample_page_desc[i].key, &key) != 0U)
        {
            if (g_sample_page_desc[i].state == SAMPLE_PAGE_LOADING)
            {
                g_sample_page_desc[i].load_cancel_requested = 1U;
                continue;
            }
            if (sample_page_cache_page_is_contractual(&g_sample_page_desc[i]) != 0U)
            {
                /* Withdraw immediately from new admissions, but preserve the
                 * storage until all AUDIO refs/pins/contracts are gone. */
                g_sample_page_desc[i].lifecycle_reserved[0] = 1U;
                sample_page_cache_set_state(&g_sample_page_desc[i], SAMPLE_PAGE_FAILED);
                continue;
            }
            sample_page_cache_clear_desc(&g_sample_page_desc[i], i);
        }
    }

    sample_stream_transport_release_map(
        &g_sample_page_sample_desc[key_slot].stream_safe.physical_map);
    memset(&g_sample_page_sample_desc[key_slot], 0, sizeof(g_sample_page_sample_desc[key_slot]));
    g_sample_page_sample_desc[key_slot].key = key;
    g_sample_page_sample_desc[key_slot].first_slot = UINT16_MAX;
    g_sample_page_last_slot[key_slot] = UINT16_MAX;
}

uint8_t sample_page_cache_cancel_reserved_page_key(sample_audio_key_t key,
                                                 uint32_t page_index,
                                                 uint8_t reason)
{
    (void)reason;
    sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    if ((page == 0) || (page->state != SAMPLE_PAGE_RESERVED))
    {
        return 0U;
    }
    if ((page->pin_count != 0U) || (page->use_count != 0U))
    {
        return 0U;
    }

    sample_page_cache_clear_desc(page, (uint32_t)(page - g_sample_page_desc));
    return 1U;
}

uint32_t sample_page_cache_cancel_reserved_key(sample_audio_key_t key, uint8_t reason)
{
    (void)reason;
    uint32_t count = 0U;
    if (sample_page_cache_key_valid(key) == 0U)
    {
        return 0U;
    }

    for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
    {
        sample_page_desc_t *const page = &g_sample_page_desc[i];
        if ((page->state == SAMPLE_PAGE_RESERVED)
            && (sample_audio_key_equal(&page->key, &key) != 0U)
            && (page->pin_count == 0U)
            && (page->use_count == 0U))
        {
            sample_page_cache_clear_desc(page, i);
            count++;
        }
    }
    return count;
}

uint32_t sample_page_cache_cancel_reserved_domain(sample_audio_domain_t domain, uint8_t reason)
{
    (void)reason;
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
    {
        sample_page_desc_t *const page = &g_sample_page_desc[i];
        if ((page->state == SAMPLE_PAGE_RESERVED)
            && (page->key.domain == domain)
            && (page->pin_count == 0U)
            && (page->use_count == 0U))
        {
            sample_page_cache_clear_desc(page, i);
            count++;
        }
    }
    return count;
}

sample_page_state_t sample_page_cache_get_page_state(uint16_t sample_id, uint32_t page_index)
{
    return sample_page_cache_get_page_state_key(sample_audio_key_classic(sample_id), page_index);
}

sample_page_state_t sample_page_cache_get_page_state_key(sample_audio_key_t key,
                                                         uint32_t page_index)
{
    const sample_page_desc_t *const page = sample_page_cache_find_page_key(key, page_index);
    const sample_page_state_t state = (page != 0) ? page->state : SAMPLE_PAGE_FREE;
    if (state == SAMPLE_PAGE_READY)
    {
        __DMB();
    }
    return state;
}

uint8_t sample_page_cache_page_exists_key(sample_audio_key_t key,
                                          uint32_t page_index)
{
    return (sample_page_cache_find_page_key(key, page_index) != 0) ? 1U : 0U;
}

uint8_t sample_page_cache_try_acquire_page(uint16_t sample_id,
                                           uint32_t page_index,
                                           sample_page_span_t *out_span)
{
    return sample_page_cache_try_acquire_page_key(sample_audio_key_classic(sample_id),
                                                  page_index,
                                                  out_span);
}

uint8_t sample_page_cache_try_acquire_page_key(sample_audio_key_t key,
                                               uint32_t page_index,
                                               sample_page_span_t *out_span)
{
    if (out_span == 0)
    {
        return 0U;
    }

    memset(out_span, 0, sizeof(*out_span));

    const uint32_t primask = sample_page_cache_lock();
    sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    if ((page == 0) || (page->state != SAMPLE_PAGE_READY) || (page->data == 0))
    {
        sample_page_cache_unlock(primask);
        return 0U;
    }

    __DMB();
    page->use_count++;
    page->last_touch = ++g_sample_page_cache_state.touch_counter;

    out_span->frames_interleaved = page->data;
    out_span->frame_count = page->frame_count;
    out_span->start_frame = page->start_frame;
    out_span->page_index = page->page_index;
    out_span->page_generation = page->generation;
    out_span->key = page->key;
    out_span->format = page->format;
    out_span->stride_floats = page->stride_floats;
    out_span->frames_per_page = page->frames_per_page;
    out_span->registration_epoch = page->registration_epoch;
    out_span->slot_index = (uint32_t)(page - g_sample_page_desc);
    sample_page_cache_unlock(primask);
    return 1U;
}

uint8_t sample_page_cache_try_acquire_page_ref(uint16_t sample_id,
                                               const sample_page_ref_t *ref,
                                               sample_page_span_t *out_span)
{
    return sample_page_cache_try_acquire_page_ref_key(sample_audio_key_classic(sample_id),
                                                      ref,
                                                      out_span);
}

uint8_t sample_page_cache_try_acquire_page_ref_key(sample_audio_key_t key,
                                                   const sample_page_ref_t *ref,
                                                   sample_page_span_t *out_span)
{
    if ((ref == 0) || (out_span == 0) || (ref->slot_index >= SAMPLE_PAGE_MAX_COUNT))
    {
        return 0U;
    }

    memset(out_span, 0, sizeof(*out_span));

    const uint32_t primask = sample_page_cache_lock();
    sample_page_desc_t *const page = &g_sample_page_desc[ref->slot_index];
    if ((sample_audio_key_equal(&page->key, &key) == 0U)
        || (page->page_index != ref->page_index)
        || (page->generation != ref->page_generation) || (page->state != SAMPLE_PAGE_READY)
        || (page->data == 0)
        || (sample_audio_key_equal(&ref->key, &key) == 0U)
        || (sample_audio_format_is_valid(ref->format) == 0U)
        || (page->format != ref->format)
        || ((ref->registration_epoch != 0U)
            && (page->registration_epoch != ref->registration_epoch))
        || (ref->stride_floats != page->stride_floats)
        || (ref->frames_per_page != page->frames_per_page))
    {
        sample_page_cache_unlock(primask);
        return 0U;
    }

    __DMB();
    page->use_count++;
    out_span->frames_interleaved = page->data;
    out_span->frame_count = page->frame_count;
    out_span->start_frame = page->start_frame;
    out_span->page_index = page->page_index;
    out_span->page_generation = page->generation;
    out_span->key = page->key;
    out_span->format = page->format;
    out_span->stride_floats = page->stride_floats;
    out_span->frames_per_page = page->frames_per_page;
    out_span->registration_epoch = page->registration_epoch;
    out_span->slot_index = ref->slot_index;
    sample_page_cache_unlock(primask);
    return 1U;
}

void sample_page_cache_release_page(uint16_t sample_id, uint32_t page_index)
{
    sample_page_cache_release_page_key(sample_audio_key_classic(sample_id), page_index);
}

void sample_page_cache_release_page_key(sample_audio_key_t key, uint32_t page_index)
{
    const uint32_t primask = sample_page_cache_lock();
    sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    if (page == 0)
    {
        sample_page_cache_unlock(primask);
        return;
    }

    if (page->use_count != 0U)
    {
        page->use_count--;
    }
    page->last_touch = ++g_sample_page_cache_state.touch_counter;
    sample_page_cache_unlock(primask);
}

void sample_page_cache_release_page_ref(uint16_t sample_id, const sample_page_ref_t *ref)
{
    sample_page_cache_release_page_ref_key(sample_audio_key_classic(sample_id), ref);
}

void sample_page_cache_release_page_ref_key(sample_audio_key_t key, const sample_page_ref_t *ref)
{
    if ((ref == 0) || (ref->slot_index >= SAMPLE_PAGE_MAX_COUNT))
    {
        return;
    }

    const uint32_t primask = sample_page_cache_lock();
    sample_page_desc_t *const page = &g_sample_page_desc[ref->slot_index];
    if ((sample_audio_key_equal(&page->key, &key) == 0U)
        || (page->page_index != ref->page_index)
        || (page->generation != ref->page_generation)
        || (sample_audio_key_equal(&ref->key, &key) == 0U)
        || (sample_audio_format_is_valid(ref->format) == 0U)
        || (page->format != ref->format)
        || ((ref->registration_epoch != 0U)
            && (page->registration_epoch != ref->registration_epoch))
        || (ref->stride_floats != page->stride_floats)
        || (ref->frames_per_page != page->frames_per_page))
    {
        sample_page_cache_unlock(primask);
        return;
    }

    if (page->use_count != 0U)
    {
        page->use_count--;
    }
    sample_page_cache_unlock(primask);
}

uint8_t sample_page_cache_alloc_slot_pool_bytes(uint32_t bytes,
                                                sample_page_raw_allocation_t *out_allocation)
{
    if (out_allocation != 0)
    {
        memset(out_allocation, 0, sizeof(*out_allocation));
    }
    if ((bytes == 0U) || (out_allocation == 0))
    {
        return 0U;
    }

    const uint32_t page_count = (bytes + SAMPLE_PAGE_BYTES - 1U) / SAMPLE_PAGE_BYTES;
    if ((page_count == 0U) || (page_count > SAMPLE_PAGE_SLOT_POOL_COUNT)
        || (page_count > UINT16_MAX))
    {
        return 0U;
    }

    int32_t start_slot =
        sample_page_cache_find_contiguous_empty_run_in_range(page_count,
                                                             SAMPLE_PAGE_SLOT_POOL_START,
                                                             SAMPLE_PAGE_SLOT_POOL_COUNT);
    if (start_slot < 0)
    {
        sample_page_cache_reclaim_stream_pages_for_full_load_in_range(SAMPLE_PAGE_SLOT_POOL_START,
                                                                      SAMPLE_PAGE_SLOT_POOL_COUNT);
        start_slot = sample_page_cache_find_contiguous_empty_run_in_range(page_count,
                                                                         SAMPLE_PAGE_SLOT_POOL_START,
                                                                         SAMPLE_PAGE_SLOT_POOL_COUNT);
    }
    if (start_slot < 0)
    {
        return 0U;
    }

    for (uint32_t i = 0U; i < page_count; ++i)
    {
        const uint32_t slot_index = (uint32_t)start_slot + i;
        sample_page_desc_t *const page = &g_sample_page_desc[slot_index];
        sample_page_cache_clear_desc(page, slot_index);
        page->key = sample_audio_key_classic(UINT16_MAX);
        page->sample_id = UINT16_MAX;
        page->page_index = i;
        page->start_frame = 0U;
        page->frame_count = 0U;
        page->generation = ++g_sample_page_cache_state.generation_counter;
        page->last_touch = ++g_sample_page_cache_state.touch_counter;
        page->pin_count = 1U;
        sample_page_cache_set_state(page, SAMPLE_PAGE_READY);
    }

    out_allocation->first_slot = (uint16_t)start_slot;
    out_allocation->page_count = (uint16_t)page_count;
    out_allocation->capacity_bytes = page_count * SAMPLE_PAGE_BYTES;
    out_allocation->data = (void *)&g_sample_page_data[start_slot][0];
    return 1U;
}

void sample_page_cache_release_slot_pool_allocation(uint16_t first_slot,
                                                    uint16_t page_count)
{
    if ((page_count == 0U)
        || (first_slot < SAMPLE_PAGE_SLOT_POOL_START)
        || ((uint32_t)first_slot >= (SAMPLE_PAGE_SLOT_POOL_START + SAMPLE_PAGE_SLOT_POOL_COUNT))
        || (((uint32_t)first_slot + page_count)
            > (SAMPLE_PAGE_SLOT_POOL_START + SAMPLE_PAGE_SLOT_POOL_COUNT)))
    {
        return;
    }

    for (uint32_t i = 0U; i < page_count; ++i)
    {
        const uint32_t slot_index = (uint32_t)first_slot + i;
        sample_page_desc_t *const page = &g_sample_page_desc[slot_index];
        /* pin_count==1 is the allocation owner's lifetime pin. Releasing the
         * allocation removes that contract; foreign users still defer reuse. */
        if (page->pin_count != 0U) page->pin_count--;
        if ((page->use_count == 0U) && (page->pin_count == 0U))
            sample_page_cache_clear_desc(page, slot_index);
        else
        {
            page->lifecycle_reserved[0] = 1U;
            sample_page_cache_set_state(page, SAMPLE_PAGE_FAILED);
        }
    }
}

uint32_t sample_page_cache_slot_pool_total_bytes(void)
{
    return SAMPLE_PAGE_SLOT_POOL_COUNT * SAMPLE_PAGE_BYTES;
}

uint32_t sample_page_cache_slot_pool_free_bytes(void)
{
    uint32_t free_pages = 0U;
    for (uint32_t i = SAMPLE_PAGE_SLOT_POOL_START;
         i < (SAMPLE_PAGE_SLOT_POOL_START + SAMPLE_PAGE_SLOT_POOL_COUNT);
         ++i)
    {
        if (g_sample_page_desc[i].state == SAMPLE_PAGE_FREE)
        {
            free_pages++;
        }
    }
    return free_pages * SAMPLE_PAGE_BYTES;
}

uint8_t sample_page_cache_slot_pool_offset(uint16_t first_slot,
                                           uint32_t byte_offset,
                                           uint32_t length,
                                           uint32_t *out_offset)
{
    if (out_offset != NULL) *out_offset = 0U;
    if ((out_offset == NULL) || (first_slot >= SAMPLE_PAGE_MAX_COUNT)) return 0U;
    const uint64_t offset = ((uint64_t)first_slot * SAMPLE_PAGE_BYTES) + byte_offset;
    const uint64_t end = offset + length;
    if ((end < offset) || (end > sizeof(g_sample_page_data))) return 0U;
    *out_offset = (uint32_t)offset;
    return 1U;
}

const void *sample_page_cache_slot_pool_resolve(uint32_t offset,
                                                uint32_t length)
{
    if ((offset > sizeof(g_sample_page_data))
        || (length > (sizeof(g_sample_page_data) - offset))) return NULL;
    return &((const uint8_t *)g_sample_page_data)[offset];
}

const float *sample_page_cache_get_full_sample_base(uint16_t sample_id, uint32_t *out_frames)
{
    return sample_page_cache_get_full_sample_base_key(sample_audio_key_classic(sample_id), out_frames);
}

const float *sample_page_cache_get_full_sample_base_key(sample_audio_key_t key,
                                                        uint32_t *out_frames)
{
    if (out_frames != 0)
    {
        *out_frames = 0U;
    }

    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        return 0;
    }

    const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if ((sample->valid == 0U) || (sample->fully_loaded == 0U) || (sample->page_count == 0U))
    {
        return 0;
    }

    if (out_frames != 0)
    {
        *out_frames = sample->total_frames;
    }

    return g_sample_page_desc[sample->first_slot].data;
}

uint8_t sample_page_cache_begin_read_block(uint16_t sample_id,
                                           uint32_t frame_index,
                                           uint32_t max_frames,
                                           sample_page_block_t *out_block)
{
    return sample_page_cache_begin_read_block_key(sample_audio_key_classic(sample_id),
                                                  frame_index,
                                                  max_frames,
                                                  out_block);
}

uint8_t sample_page_cache_begin_read_block_key(sample_audio_key_t key,
                                               uint32_t frame_index,
                                               uint32_t max_frames,
                                               sample_page_block_t *out_block)
{
    if (out_block == 0)
    {
        return 0U;
    }

    memset(out_block, 0, sizeof(*out_block));
    out_block->status = SAMPLE_PAGE_BLOCK_NOT_READY;

    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if ((key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) || (max_frames == 0U))
    {
        return 0U;
    }

    const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if (sample->valid == 0U)
    {
        return 1U;
    }

    if (frame_index >= sample->total_frames)
    {
        out_block->status = SAMPLE_PAGE_BLOCK_DONE;
        return 1U;
    }

    const sample_audio_format_t format = sample_audio_format_or_stereo(sample->format);
    const uint32_t page_index = sample_audio_format_page_index_from_frame(format, frame_index);
    sample_page_span_t span;
    if (sample_page_cache_try_acquire_page_key(key, page_index, &span) == 0U)
    {
        return 1U;
    }

    const uint32_t page_offset = frame_index - span.start_frame;
    uint32_t frame_count = span.frame_count - page_offset;
    if (frame_count > max_frames)
    {
        frame_count = max_frames;
    }

    out_block->frames_interleaved =
        &span.frames_interleaved[page_offset * span.stride_floats];
    out_block->frame_count = frame_count;
    out_block->start_frame = frame_index;
    out_block->page_index = page_index;
    out_block->format = span.format;
    out_block->stride_floats = span.stride_floats;
    out_block->frames_per_page = span.frames_per_page;
    out_block->status = (frame_count != 0U) ? SAMPLE_PAGE_BLOCK_OK : SAMPLE_PAGE_BLOCK_NOT_READY;
    return 1U;
}

void sample_page_cache_commit_read_block(uint16_t sample_id,
                                         uint32_t page_index)
{
    sample_page_cache_commit_read_block_key(sample_audio_key_classic(sample_id), page_index);
}

void sample_page_cache_commit_read_block_key(sample_audio_key_t key,
                                             uint32_t page_index)
{
    sample_page_cache_release_page_key(key, page_index);
}

uint8_t sample_page_cache_has_reserved_range(uint16_t first_sample_id,
                                           uint16_t sample_count)
{
    return sample_page_cache_has_reserved_domain_range(SAMPLE_AUDIO_DOMAIN_CLASSIC,
                                                     first_sample_id,
                                                     sample_count);
}

uint8_t sample_page_cache_has_reserved_domain_range(sample_audio_domain_t domain,
                                                  uint16_t first_object_id,
                                                  uint16_t object_count)
{
    if (object_count == 0U)
    {
        return 0U;
    }

    for (uint16_t offset_id = 0U; offset_id < object_count; ++offset_id)
    {
        sample_audio_key_t key;
        key.domain = domain;
        key.object_id = (uint16_t)(first_object_id + offset_id);
        const uint16_t key_slot = sample_page_cache_key_slot(key);
        if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
        {
            continue;
        }

        if (g_sample_page_reserved_count[key_slot] != 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

uint8_t sample_page_cache_get_stream_info(uint16_t sample_id,
                                          sample_page_stream_info_t *out_info)
{
    return sample_page_cache_get_stream_info_key(sample_audio_key_classic(sample_id), out_info);
}

uint8_t sample_page_cache_get_stream_info_key(sample_audio_key_t key,
                                              sample_page_stream_info_t *out_info)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if ((key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) || (out_info == 0))
    {
        return 0U;
    }

    sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if (sample_page_cache_sample_is_stream_loadable_key(key) == 0U)
    {
        return 0U;
    }

    sample_stream_physical_map_t *const map = &sample->stream_safe.physical_map;
    if ((sample->physical_only != 0U)
        && (sample_stream_physical_map_is_current(map) == 0U))
        return 0U;

    memset(out_info, 0, sizeof(*out_info));
    out_info->key = key;
    memcpy(out_info->path, sample->path, sizeof(out_info->path));
    out_info->info = sample->info;
    out_info->total_frames = sample->total_frames;
    out_info->data_offset = sample->data_offset;
    out_info->format = sample->format;
    out_info->stride_floats = sample->stride_floats;
    out_info->frames_per_page = sample->frames_per_page;
    out_info->registration_epoch = sample->registration_epoch;
    out_info->stream_safe = sample->stream_safe;
    out_info->physical_only = sample->physical_only;
    return 1U;
}

uint8_t sample_page_cache_register_prepared_stream(
    const sample_page_stream_info_t *registration)
{
    if (registration == NULL) return 0U;
    const uint16_t key_slot = sample_page_cache_key_slot(registration->key);
    if ((key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
        || (registration->total_frames == 0U)
        || (registration->frames_per_page == 0U)
        || (sample_audio_format_is_valid(registration->format) == 0U)
        || ((registration->physical_only != 0U)
            && (sample_stream_physical_map_is_current(
                    &registration->stream_safe.physical_map) == 0U))) return 0U;
    sample_page_cache_clear_key(registration->key);
    sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if (sample_page_cache_trim_path_copy(sample->path, sizeof(sample->path),
                                         registration->path) == 0U) return 0U;
    sample->key = registration->key;
    sample->info = registration->info;
    sample->total_frames = registration->total_frames;
    sample->readable_frames = registration->total_frames;
    sample->data_offset = registration->data_offset;
    sample->format = registration->format;
    sample->stride_floats = registration->stride_floats;
    sample->frames_per_page = registration->frames_per_page;
    sample->registration_epoch =
        ++g_sample_page_cache_state.registration_epoch_counter;
    sample->stream_safe = registration->stream_safe;
    sample->valid = 1U;
    sample->fully_loaded = 0U;
    sample->physical_only = registration->physical_only;
    sample->first_slot = UINT16_MAX;
    return 1U;
}

uint8_t sample_page_cache_begin_full_reservation(
    const sample_page_stream_info_t *registration,
    sample_page_alloc_type_t alloc_type,
    uint32_t *out_page_count)
{
    if (out_page_count != NULL) *out_page_count = 0U;
    if ((registration == NULL) || (out_page_count == NULL)
        || (sample_page_cache_register_prepared_stream(registration) == 0U))
        return 0U;
    const uint32_t page_count = sample_audio_format_required_page_count(
        registration->format, registration->total_frames);
    uint32_t range_start = 0U, range_count = 0U;
    sample_page_cache_alloc_range(alloc_type, &range_start, &range_count);
    int32_t first = sample_page_cache_find_contiguous_empty_run_in_range(
        page_count, range_start, range_count);
    if (first < 0)
    {
        sample_page_cache_reclaim_stream_pages_for_full_load_in_range(
            range_start, range_count);
        first = sample_page_cache_find_contiguous_empty_run_in_range(
            page_count, range_start, range_count);
    }
    if (first < 0) { sample_page_cache_clear_key(registration->key); return 0U; }
    sample_page_sample_desc_t *const sample =
        &g_sample_page_sample_desc[sample_page_cache_key_slot(registration->key)];
    for (uint32_t i = 0U; i < page_count; ++i)
    {
        const uint32_t start = sample_audio_format_page_start_frame(
            registration->format, i);
        uint32_t frames = registration->total_frames - start;
        if (frames > registration->frames_per_page)
            frames = registration->frames_per_page;
        if (sample_page_cache_assign_slot_key((uint32_t)first + i,
                                              registration->key, i,
                                              frames) == NULL)
        { sample_page_cache_clear_key(registration->key); return 0U; }
    }
    sample->first_slot = (uint16_t)first;
    sample->page_count = (uint16_t)page_count;
    *out_page_count = page_count;
    return 1U;
}

uint8_t sample_page_cache_finish_full_reservation(sample_audio_key_t key)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) return 0U;
    sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if ((sample->valid == 0U) || (sample->page_count == 0U)
        || (sample->first_slot == UINT16_MAX)) return 0U;
    for (uint32_t i = 0U; i < sample->page_count; ++i)
    {
        const sample_page_desc_t *const page =
            &g_sample_page_desc[(uint32_t)sample->first_slot + i];
        if ((page->state != SAMPLE_PAGE_READY)
            || (sample_audio_key_equal(&page->key, &key) == 0U)
            || (page->page_index != i)) return 0U;
    }
    sample_stream_transport_release_map(&sample->stream_safe.physical_map);
    memset(&sample->stream_safe.physical_map, 0,
           sizeof(sample->stream_safe.physical_map));
    sample->stream_safe.physical_map.first_pool_block =
        SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK;
    sample->physical_only = 0U;
    sample->fully_loaded = 1U;
    return 1U;
}
uint8_t sample_page_cache_get_load_target(uint16_t sample_id,
                                          uint32_t page_index,
                                          sample_page_load_target_t *out_target)
{
    return sample_page_cache_get_load_target_key(sample_audio_key_classic(sample_id),
                                                 page_index,
                                                 out_target);
}

uint8_t sample_page_cache_get_load_target_key(sample_audio_key_t key,
                                              uint32_t page_index,
                                              sample_page_load_target_t *out_target)
{
    if ((sample_page_cache_key_valid(key) == 0U) || (out_target == 0))
    {
        return 0U;
    }

    sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    if ((page == 0) || (page->state != SAMPLE_PAGE_RESERVED) || (page->data == 0))
    {
        return 0U;
    }

    memset(out_target, 0, sizeof(*out_target));
    out_target->key = key;
    out_target->sample_id = sample_page_cache_key_slot(key);
    out_target->slot_index = (uint16_t)(page - g_sample_page_desc);
    out_target->page_index = page->page_index;
    out_target->start_frame = page->start_frame;
    out_target->frame_count = page->frame_count;
    out_target->format = page->format;
    out_target->stride_floats = page->stride_floats;
    out_target->frames_per_page = page->frames_per_page;
    out_target->registration_epoch = page->registration_epoch;
    out_target->page_generation = page->generation;
    out_target->frames_interleaved = page->data;
    return 1U;
}

uint8_t sample_page_cache_begin_loading(const sample_page_load_target_t *target,
                                          sample_page_load_token_t *out_token)
{
    if ((target == 0) || (out_token == 0) || (target->slot_index >= SAMPLE_PAGE_MAX_COUNT))
    {
        return 0U;
    }

    const uint32_t primask = sample_page_cache_lock();
    sample_page_desc_t *const page = &g_sample_page_desc[target->slot_index];
    const uint8_t valid = (uint8_t)((page->state == SAMPLE_PAGE_RESERVED)
        && (page->data == target->frames_interleaved)
        && (page->page_index == target->page_index)
        && (page->generation == target->page_generation)
        && (page->registration_epoch == target->registration_epoch)
        && (sample_audio_key_equal(&page->key, &target->key) != 0U));
    if (valid != 0U)
    {
        memset(out_token, 0, sizeof(*out_token));
        out_token->key = page->key;
        out_token->page_index = page->page_index;
        out_token->page_generation = page->generation;
        out_token->registration_epoch = page->registration_epoch;
        out_token->slot_index = target->slot_index;
        page->load_cancel_requested = 0U;
        sample_page_cache_set_state(page, SAMPLE_PAGE_LOADING);
    }
    sample_page_cache_unlock(primask);
    return valid;
}

uint8_t sample_page_cache_resolve_loading_target(const sample_page_load_token_t *token,
                                                 sample_page_load_target_t *out_target)
{
    if ((token == 0) || (out_target == 0) || (token->slot_index >= SAMPLE_PAGE_MAX_COUNT))
    {
        return 0U;
    }
    const uint32_t primask = sample_page_cache_lock();
    const sample_page_desc_t *const page = &g_sample_page_desc[token->slot_index];
    const uint8_t valid = (uint8_t)((page->state == SAMPLE_PAGE_LOADING)
        && (page->load_cancel_requested == 0U)
        && (page->page_index == token->page_index)
        && (page->generation == token->page_generation)
        && (page->registration_epoch == token->registration_epoch)
        && (sample_audio_key_equal(&page->key, &token->key) != 0U)
        && (page->data != 0));
    if (valid != 0U)
    {
        memset(out_target, 0, sizeof(*out_target));
        out_target->key = page->key;
        out_target->sample_id = page->sample_id;
        out_target->slot_index = token->slot_index;
        out_target->page_index = page->page_index;
        out_target->start_frame = page->start_frame;
        out_target->frame_count = page->frame_count;
        out_target->format = page->format;
        out_target->stride_floats = page->stride_floats;
        out_target->frames_per_page = page->frames_per_page;
        out_target->registration_epoch = page->registration_epoch;
        out_target->page_generation = page->generation;
        out_target->frames_interleaved = page->data;
    }
    sample_page_cache_unlock(primask);
    return valid;
}

uint8_t sample_page_cache_finish_loading(const sample_page_load_token_t *token,
                                           sample_page_finish_result_t result)
{
    if ((token == 0) || (token->slot_index >= SAMPLE_PAGE_MAX_COUNT)
        || ((result != SAMPLE_PAGE_FINISH_READY) && (result != SAMPLE_PAGE_FINISH_ERROR)))
    {
        return 0U;
    }

    const uint32_t primask = sample_page_cache_lock();
    sample_page_desc_t *const page = &g_sample_page_desc[token->slot_index];
    const uint8_t valid = (uint8_t)((page->state == SAMPLE_PAGE_LOADING)
        && (page->page_index == token->page_index)
        && (page->generation == token->page_generation)
        && (page->registration_epoch == token->registration_epoch)
        && (sample_audio_key_equal(&page->key, &token->key) != 0U));
    uint8_t completed = valid;
    if (valid != 0U)
    {
        const uint8_t cancelled = page->load_cancel_requested;
        const sample_page_state_t final_state =
            ((result == SAMPLE_PAGE_FINISH_READY) && (cancelled == 0U))
                ? SAMPLE_PAGE_READY
                : SAMPLE_PAGE_FAILED;
        page->load_cancel_requested = 0U;
        if ((cancelled != 0U) && (page->pin_count == 0U) && (page->use_count == 0U))
        {
            sample_page_cache_clear_desc(page, token->slot_index);
        }
        else
        {
            sample_page_cache_set_state(page, final_state);
        }
        if ((result == SAMPLE_PAGE_FINISH_READY) && (final_state != SAMPLE_PAGE_READY))
        {
            completed = 0U;
        }
    }
    sample_page_cache_unlock(primask);
    return completed;
}

uint8_t sample_page_cache_cancel_loading_key(sample_audio_key_t key,
                                               uint32_t page_index)
{
    const uint32_t primask = sample_page_cache_lock();
    sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    const uint8_t found = (uint8_t)((page != 0) && (page->state == SAMPLE_PAGE_LOADING));
    if (found != 0U)
    {
        page->load_cancel_requested = 1U;
    }
    sample_page_cache_unlock(primask);
    return found;
}

uint8_t sample_page_cache_prepare_bulk_page_key_alloc(
    sample_audio_key_t key,
    uint32_t page_index,
    sample_page_alloc_type_t alloc_type)
{
    if (sample_page_cache_reserve_page_key_alloc(key, page_index, alloc_type) == 0U)
    {
        return 0U;
    }

    sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    if ((page == 0) || (page->data == 0) || (page->frame_count == 0U))
    {
        return 0U;
    }
    if (page->state != SAMPLE_PAGE_RESERVED)
    {
        return 0U;
    }
    return 1U;
}

uint8_t sample_page_cache_get_bulk_load_target_key(
    sample_audio_key_t key,
    uint32_t page_index,
    sample_page_load_target_t *out_target)
{
    if ((sample_page_cache_key_valid(key) == 0U) || (out_target == 0))
    {
        return 0U;
    }

    sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    if ((page == 0) || (page->state != SAMPLE_PAGE_RESERVED) || (page->data == 0))
    {
        return 0U;
    }

    memset(out_target, 0, sizeof(*out_target));
    out_target->key = key;
    out_target->sample_id = sample_page_cache_key_slot(key);
    out_target->slot_index = (uint16_t)(page - g_sample_page_desc);
    out_target->page_index = page->page_index;
    out_target->start_frame = page->start_frame;
    out_target->frame_count = page->frame_count;
    out_target->format = page->format;
    out_target->stride_floats = page->stride_floats;
    out_target->frames_per_page = page->frames_per_page;
    out_target->registration_epoch = page->registration_epoch;
    out_target->page_generation = page->generation;
    out_target->frames_interleaved = page->data;
    return 1U;
}

uint8_t sample_page_cache_set_page_state(uint16_t sample_id,
                                         uint32_t page_index,
                                         sample_page_state_t state)
{
    return sample_page_cache_set_page_state_key(sample_audio_key_classic(sample_id),
                                                page_index,
                                                state);
}

uint8_t sample_page_cache_set_page_state_key(sample_audio_key_t key,
                                             uint32_t page_index,
                                             sample_page_state_t state)
{
    sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    if (page == 0)
    {
        return 0U;
    }

    const sample_page_state_t current = page->state;
    const uint8_t transition_allowed =
        (uint8_t)(((current == SAMPLE_PAGE_RESERVED) && (state == SAMPLE_PAGE_FAILED))
                  || ((current == SAMPLE_PAGE_READY) && (state == SAMPLE_PAGE_READY))
                  || ((current == SAMPLE_PAGE_FAILED) && (state == SAMPLE_PAGE_FAILED)));
    if (transition_allowed == 0U)
    {
        return 0U;
    }
    sample_page_cache_set_state(page, state);
    return 1U;
}
uint8_t sample_page_cache_reserve_page(uint16_t sample_id, uint32_t page_index)
{
    return sample_page_cache_reserve_page_key(sample_audio_key_classic(sample_id), page_index);
}

uint8_t sample_page_cache_reserve_page_key(sample_audio_key_t key, uint32_t page_index)
{
    return sample_page_cache_reserve_page_key_alloc(key,
                                                    page_index,
                                                    SAMPLE_PAGE_ALLOC_LEGACY_DEFAULT);
}

uint8_t sample_page_cache_reserve_page_key_alloc(sample_audio_key_t key,
                                                 uint32_t page_index,
                                                 sample_page_alloc_type_t alloc_type)
{
    sample_page_desc_t *page = sample_page_cache_find_page_mut_key(key, page_index);
    if (page == 0)
    {
        page = sample_page_cache_alloc_empty_slot_key(key, page_index, alloc_type);
        if (page == 0)
        {
            return 0U;
        }
    }

    if ((page->state == SAMPLE_PAGE_FREE)
        || ((page->state == SAMPLE_PAGE_FAILED)
            && (page->pin_count == 0U)
            && (page->use_count == 0U)))
    {
        page->load_cancel_requested = 0U;
        sample_page_cache_set_state(page, SAMPLE_PAGE_RESERVED);
    }

    sample_page_cache_set_page_geometry(page, key);
    page->start_frame = sample_audio_format_page_start_frame(page->format, page_index);
    page->frame_count = sample_page_cache_stream_page_frame_count_key(key, page_index);
    if (page->frame_count == 0U)
    {
        sample_page_cache_set_state(page, SAMPLE_PAGE_FAILED);
        return 0U;
    }

    page->last_touch = ++g_sample_page_cache_state.touch_counter;
    return 1U;
}

uint8_t sample_page_cache_reserve_page_ref(uint16_t sample_id,
                                           uint32_t page_index,
                                           sample_page_ref_t *out_ref)
{
    return sample_page_cache_reserve_page_ref_key(sample_audio_key_classic(sample_id),
                                                  page_index,
                                                  out_ref);
}

uint8_t sample_page_cache_reserve_page_ref_key(sample_audio_key_t key,
                                               uint32_t page_index,
                                               sample_page_ref_t *out_ref)
{
    sample_page_desc_t *page = sample_page_cache_find_page_mut_key(key, page_index);
    if (page == 0)
    {
        page = sample_page_cache_alloc_empty_slot_key(key,
                                                      page_index,
                                                      SAMPLE_PAGE_ALLOC_LEGACY_DEFAULT);
        if (page == 0)
        {
            return 0U;
        }
    }

    if (page->state == SAMPLE_PAGE_FREE)
    {
        sample_page_cache_set_state(page, SAMPLE_PAGE_RESERVED);
    }

    sample_page_cache_set_page_geometry(page, key);
    page->start_frame = sample_audio_format_page_start_frame(page->format, page_index);
    page->frame_count = sample_page_cache_stream_page_frame_count_key(key, page_index);
    if (page->frame_count == 0U)
    {
        sample_page_cache_set_state(page, SAMPLE_PAGE_FAILED);
        return 0U;
    }

    page->last_touch = ++g_sample_page_cache_state.touch_counter;
    if (out_ref != 0)
    {
        (void)sample_page_cache_fill_ref(page, out_ref);
    }
    return 1U;
}

uint8_t sample_page_cache_reserve_start_pages(uint16_t sample_id,
                                              uint32_t start_frame,
                                              uint32_t page_count)
{
    return sample_page_cache_reserve_start_pages_key(sample_audio_key_classic(sample_id),
                                                     start_frame,
                                                     page_count);
}

uint8_t sample_page_cache_reserve_start_pages_key(sample_audio_key_t key,
                                                  uint32_t start_frame,
                                                  uint32_t page_count)
{
    return sample_page_cache_reserve_start_pages_key_alloc(
        key,
        start_frame,
        page_count,
        SAMPLE_PAGE_ALLOC_LEGACY_DEFAULT);
}

uint8_t sample_page_cache_reserve_start_pages_key_alloc(sample_audio_key_t key,
                                                        uint32_t start_frame,
                                                        uint32_t page_count,
                                                        sample_page_alloc_type_t alloc_type)
{
    const uint32_t first_page = sample_audio_format_page_index_from_frame(
        sample_page_cache_format_key(key), start_frame);
    uint8_t ok = 1U;

    for (uint32_t i = 0U; i < page_count; ++i)
    {
        if (sample_page_cache_reserve_page_key_alloc(key, first_page + i, alloc_type) == 0U)
        {
            ok = 0U;
            break;
        }
    }

    return ok;
}

uint8_t sample_page_cache_pin_page(uint16_t sample_id, uint32_t page_index)
{
    return sample_page_cache_pin_page_key(sample_audio_key_classic(sample_id), page_index);
}

uint8_t sample_page_cache_pin_page_key(sample_audio_key_t key, uint32_t page_index)
{
    return sample_page_cache_pin_page_key_alloc(key,
                                                page_index,
                                                SAMPLE_PAGE_ALLOC_LEGACY_DEFAULT);
}

uint8_t sample_page_cache_pin_page_key_alloc(sample_audio_key_t key,
                                             uint32_t page_index,
                                             sample_page_alloc_type_t alloc_type)
{
    sample_page_desc_t *page = sample_page_cache_find_page_mut_key(key, page_index);
    if (page == 0)
    {
        page = sample_page_cache_alloc_empty_slot_key(key, page_index, alloc_type);
        if (page == 0)
        {
            return 0U;
        }
    }

    if (page->pin_count != UINT16_MAX)
    {
        page->pin_count++;
    }
    page->last_touch = ++g_sample_page_cache_state.touch_counter;
    return 1U;
}

void sample_page_cache_unpin_page(uint16_t sample_id, uint32_t page_index)
{
    sample_page_cache_unpin_page_key(sample_audio_key_classic(sample_id), page_index);
}

void sample_page_cache_unpin_page_key(sample_audio_key_t key, uint32_t page_index)
{
    sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    if (page == 0)
    {
        return;
    }

    if (page->pin_count != 0U)
    {
        page->pin_count--;
    }
    page->last_touch = ++g_sample_page_cache_state.touch_counter;
}

void sample_page_cache_unpin_page_ref_key(sample_audio_key_t key,
                                          const sample_page_ref_t *ref)
{
    if ((ref == 0) || (sample_audio_key_equal(&key, &ref->key) == 0U)
        || (ref->slot_index >= SAMPLE_PAGE_MAX_COUNT))
    {
        return;
    }
    sample_page_desc_t *const page = &g_sample_page_desc[ref->slot_index];
    if ((sample_audio_key_equal(&page->key, &ref->key) == 0U)
        || (page->page_index != ref->page_index)
        || (page->generation != ref->page_generation)
        || (page->registration_epoch != ref->registration_epoch))
    {
        return;
    }
    if (page->pin_count != 0U)
    {
        page->pin_count--;
    }
    page->last_touch = ++g_sample_page_cache_state.touch_counter;
}

uint8_t sample_page_cache_register_live_pcm24_stereo_sample_key(
    sample_audio_key_t key,
    const char *path,
    uint32_t total_frames,
    uint32_t readable_frames,
    uint32_t data_offset,
    uint32_t file_size,
    const sample_stream_physical_extent_t *extents,
    uint16_t extent_count,
    uint32_t media_epoch)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    const uint64_t data_bytes = (uint64_t)total_frames
                              * AUDIO_RECORDER_BYTES_PER_FRAME;
    if ((key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) || (path == 0)
            || (total_frames == 0U) || (readable_frames > total_frames)
            || (data_bytes > UINT32_MAX)
            || (((uint64_t)data_offset + data_bytes) > file_size))
    {
        return 0U;
    }

    sample_page_cache_clear_key(key);
    sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if (sample_page_cache_trim_path_copy(
            sample->path, sizeof(sample->path), path) == 0U)
    {
        return 0U;
    }
    sample->key = key;
    sample->info.sample_rate = AUDIO_RECORDER_SAMPLE_RATE_HZ;
    sample->info.channels = AUDIO_RECORDER_CHANNELS;
    sample->info.bits_per_sample = 24U;
    sample->info.block_align = AUDIO_RECORDER_BYTES_PER_FRAME;
    sample->info.byte_rate = AUDIO_RECORDER_SAMPLE_RATE_HZ
                           * AUDIO_RECORDER_BYTES_PER_FRAME;
    sample->info.data_size = (uint32_t)data_bytes;
    sample->total_frames = total_frames;
    sample->readable_frames = readable_frames;
    sample->data_offset = data_offset;
    sample->format = SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED;
    sample->stride_floats = (uint16_t)sample_audio_format_stride_floats(
        sample->format);
    sample->frames_per_page = sample_audio_format_frames_per_page(sample->format);
    sample->registration_epoch = ++g_sample_page_cache_state.registration_epoch_counter;
    sample_stream_safe_metadata_init_fatfs(key,
                                           &sample->info,
                                           total_frames,
                                           data_offset,
                                           &sample->stream_safe);
    sample->stream_safe.file_size = file_size;
    if (sample_stream_physical_map_import(
            &sample->stream_safe.physical_map,
            extents, extent_count, media_epoch) == 0U)
    {
        memset(sample, 0, sizeof(*sample));
        return 0U;
    }
    sample->valid = 1U;
    sample->fully_loaded = 0U;
    sample->live_committed = 1U;
    sample->physical_only = 1U;
    sample->first_slot = UINT16_MAX;
    return 1U;
}

uint8_t sample_page_cache_update_readable_frames_key(sample_audio_key_t key,
                                                      uint32_t readable_frames)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        return 0U;
    }
    sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if ((sample->valid == 0U) || (readable_frames > sample->total_frames))
    {
        return 0U;
    }
    const uint32_t primask = sample_page_cache_lock();
    if (readable_frames > sample->readable_frames)
    {
        sample->readable_frames = readable_frames;
    }
    sample_page_cache_unlock(primask);
    return 1U;
}

uint8_t sample_page_cache_update_live_map_key(
    sample_audio_key_t key,
    uint32_t file_size,
    const sample_stream_physical_extent_t *extents,
    uint16_t extent_count,
    uint32_t media_epoch)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if ((key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) || (extents == 0)
            || (extent_count == 0U))
    {
        return 0U;
    }
    sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if ((sample->valid == 0U) || (sample->live_committed == 0U))
    {
        return 0U;
    }

    sample_stream_physical_map_t map;
    memset(&map, 0, sizeof(map));
    map.first_pool_block = SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK;
    if (sample_stream_physical_map_import(
            &map, extents, extent_count, media_epoch) == 0U)
    {
        return 0U;
    }
    const uint32_t primask = sample_page_cache_lock();
    const sample_stream_physical_map_t previous = sample->stream_safe.physical_map;
    sample->stream_safe.physical_map = map;
    sample->stream_safe.file_size = file_size;
    sample_page_cache_unlock(primask);
    sample_stream_physical_map_t released = previous;
    sample_stream_physical_map_release(&released);
    return 1U;
}

uint8_t sample_page_cache_update_stream_path_key(sample_audio_key_t key,
                                                  const char *path)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if ((key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) || (path == 0)
            || (g_sample_page_sample_desc[key_slot].valid == 0U))
    {
        return 0U;
    }
    return (sample_page_cache_trim_path_copy(
        g_sample_page_sample_desc[key_slot].path,
        sizeof(g_sample_page_sample_desc[key_slot].path), path) != 0U) ? 1U : 0U;
}

uint8_t sample_page_cache_get_reserved_load_target_domain_range(
    sample_audio_domain_t domain,
    uint16_t first_object_id,
    uint16_t object_count,
    sample_page_load_target_t *out_target)
{
    if ((out_target == 0) || (object_count == 0U))
    {
        return 0U;
    }
    for (uint16_t offset_id = 0U; offset_id < object_count; ++offset_id)
    {
        const sample_audio_key_t key = {
            .domain = domain,
            .object_id = (uint16_t)(first_object_id + offset_id),
        };
        const uint16_t key_slot = sample_page_cache_key_slot(key);
        if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
        {
            continue;
        }

        sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
        if ((sample->valid == 0U) || (sample->fully_loaded != 0U))
        {
            continue;
        }

        for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
        {
            sample_page_desc_t *const page = &g_sample_page_desc[i];
            if ((sample_audio_key_equal(&page->key, &key) == 0U)
                || (page->state != SAMPLE_PAGE_RESERVED))
            {
                continue;
            }

            return sample_page_cache_get_load_target_key(
                key, page->page_index, out_target);
        }
    }
    return 0U;
}
