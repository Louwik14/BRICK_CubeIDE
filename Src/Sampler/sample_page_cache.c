#include "Sampler/sample_page_cache.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/looper_storage.h"
#include "Storage/wav_audio_codec.h"
#include "Sampler/sample_stream_fatfs_map.h"
#include "stm32h7xx.h"

#define SAMPLE_PAGE_WINDOW_LOCK_MAX (SAMPLE_PAGE_CACHE_MAX_VOICES * SAMPLE_PAGE_MULTI_WINDOW_PAGES * 2U)
#define SAMPLE_PAGE_SLOT_FLOAT_CAPACITY (SAMPLE_PAGE_BYTES / sizeof(float))
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(float) == SAMPLE_PAGE_SAMPLE_BYTES, "sample_page_cache expects 32-bit float");
_Static_assert((SAMPLE_PAGE_SLOT_FLOAT_CAPACITY * sizeof(float)) == SAMPLE_PAGE_BYTES,
               "sample page slot must remain exactly one physical page");
#endif

#define SAMPLE_PAGE_INDEX_SIZE (SAMPLE_PAGE_MAX_COUNT * 2U)

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
    uint32_t data_offset;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    sample_stream_safe_metadata_t stream_safe;
    uint8_t valid;
    uint8_t fully_loaded;
    uint8_t raw_pcm24;
} sample_page_sample_desc_t;

typedef struct
{
    uint8_t used;
    sample_audio_key_t key;
    uint16_t slot_index;
    uint32_t page_index;
} sample_page_index_entry_t;

typedef struct
{
    uint8_t used;
    uint8_t owner_kind;
    uint8_t owner_id;
    uint8_t reserved;
    uint16_t slot_index;
    uint16_t lock_count;
    uint32_t owner_generation;
} sample_page_window_lock_t;

SDRAM_PAGE_META static sample_page_desc_t g_sample_page_desc[SAMPLE_PAGE_MAX_COUNT];
SDRAM_PAGE_POOL static float g_sample_page_data[SAMPLE_PAGE_MAX_COUNT][SAMPLE_PAGE_SLOT_FLOAT_CAPACITY];
static CTRL_STATE sample_page_cache_state_t g_sample_page_cache_state;
SDRAM_PAGE_META static sample_page_sample_desc_t g_sample_page_sample_desc[SAMPLE_PAGE_CACHE_MAX_SAMPLES];
static CTRL_STATE uint16_t g_sample_page_last_slot[SAMPLE_PAGE_CACHE_MAX_SAMPLES];
SDRAM_PAGE_INDEX static sample_page_index_entry_t g_sample_page_index[SAMPLE_PAGE_INDEX_SIZE];
static CTRL_STATE sample_page_window_lock_t g_sample_page_window_lock[SAMPLE_PAGE_WINDOW_LOCK_MAX];
static CTRL_STATE uint16_t g_sample_page_queued_count[SAMPLE_PAGE_CACHE_MAX_SAMPLES];
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
            || (page->window_pin_count != 0U)
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
        if ((page->state == SAMPLE_PAGE_QUEUED) && (g_sample_page_queued_count[key_slot] != 0U))
        {
            g_sample_page_queued_count[key_slot]--;
        }
        if (state == SAMPLE_PAGE_QUEUED)
        {
            g_sample_page_queued_count[key_slot]++;
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
        sample_page_cache_set_state(page, SAMPLE_PAGE_EMPTY);
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
    for (uint32_t i = 0U; i < SAMPLE_PAGE_WINDOW_LOCK_MAX; ++i)
    {
        sample_page_window_lock_t *const lock = &g_sample_page_window_lock[i];
        if ((lock->used != 0U) && (lock->slot_index == slot_index))
        {
            memset(lock, 0, sizeof(*lock));
        }
    }
    sample_page_cache_set_state(page, SAMPLE_PAGE_EMPTY);

    memset(page, 0, sizeof(*page));
    page->sample_id = UINT16_MAX;
    page->key = sample_audio_key_classic(UINT16_MAX);
    page->page_index = UINT32_MAX;
    page->start_frame = UINT32_MAX;
    page->data = &g_sample_page_data[slot_index][0];
    page->state = SAMPLE_PAGE_EMPTY;
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
                && (page->state != SAMPLE_PAGE_EMPTY))
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
            return frame_count;
        }
    }

    return sample_audio_format_frames_per_page(SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED);
}

static uint8_t sample_page_cache_sample_is_stream_loadable(uint16_t sample_id)
{
    return sample_page_cache_sample_is_stream_loadable_key(sample_audio_key_classic(sample_id));
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

static uint16_t sample_page_cache_repair_queued_count_key(sample_audio_key_t key)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        return 0U;
    }

    uint16_t queued_count = 0U;
    const uint8_t loadable = sample_page_cache_sample_is_stream_loadable_key(key);

    for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
    {
        sample_page_desc_t *const page = &g_sample_page_desc[i];
        if ((sample_audio_key_equal(&page->key, &key) == 0U)
            || (page->state != SAMPLE_PAGE_QUEUED))
        {
            continue;
        }

        if ((loadable == 0U) || (page->data == 0) || (page->frame_count == 0U))
        {
            sample_page_cache_set_state(page, SAMPLE_PAGE_ERROR);
            continue;
        }

        if (queued_count != UINT16_MAX)
        {
            queued_count++;
        }
    }

    g_sample_page_queued_count[key_slot] = queued_count;
    return queued_count;
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
        if (page->state == SAMPLE_PAGE_EMPTY)
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
            page->window_pin_count = 0U;
            sample_page_cache_set_state(page, SAMPLE_PAGE_QUEUED);
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
    evict_page->window_pin_count = 0U;
    sample_page_cache_set_state(evict_page, SAMPLE_PAGE_QUEUED);
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
            if (g_sample_page_desc[start + i].state != SAMPLE_PAGE_EMPTY)
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
    sample_page_cache_set_state(page, SAMPLE_PAGE_QUEUED);
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        sample_page_cache_index_put_key(key, page_index, (uint16_t)slot_index);
        g_sample_page_last_slot[key_slot] = (uint16_t)slot_index;
    }
    return page;
}

static sample_page_load_result_t sample_page_cache_decode_page(FIL *fp,
                                                               const wav_info_t *info,
                                                               sample_page_desc_t *page,
                                                               uint8_t *io_buffer,
                                                               uint32_t io_buffer_size)
{
    if ((fp == 0) || (info == 0) || (page == 0) || (io_buffer == 0) || (io_buffer_size == 0U)
        || (info->block_align == 0U))
    {
        return SAMPLE_PAGE_LOAD_INVALID_ARG;
    }

    uint32_t remaining_frames = page->frame_count;
    uint32_t write_frame = 0U;
    const wav_audio_codec_decode_block_fn decode_block =
        (info->channels == 1U)
            ? wav_audio_codec_select_pcm_decode_mono_block(info->bits_per_sample)
            : wav_audio_codec_select_pcm_decode_block(info->channels, info->bits_per_sample);
    const uint32_t expected_block_align =
        (uint32_t)info->channels * ((uint32_t)info->bits_per_sample / 8U);
    const sample_audio_format_t expected_format = sample_audio_format_from_channels(info->channels);
    if ((decode_block == 0) || (info->block_align != expected_block_align)
        || (page->format != expected_format)
        || (page->stride_floats != sample_audio_format_stride_floats(expected_format)))
    {
        return SAMPLE_PAGE_LOAD_DECODE_FAILED;
    }

    while (remaining_frames != 0U)
    {
        uint32_t request_frames = remaining_frames;
        uint32_t request_bytes = request_frames * info->block_align;
        if (request_bytes > io_buffer_size)
        {
            request_bytes = io_buffer_size - (io_buffer_size % info->block_align);
        }
        if (request_bytes == 0U)
        {
            return SAMPLE_PAGE_LOAD_INVALID_ARG;
        }

        UINT br = 0U;
        const FRESULT fr = f_read(fp, io_buffer, request_bytes, &br);
        if (fr != FR_OK)
        {
            return SAMPLE_PAGE_LOAD_READ_FAILED;
        }

        const uint32_t valid_bytes = br - (br % info->block_align);
        if (valid_bytes == 0U)
        {
            return SAMPLE_PAGE_LOAD_READ_FAILED;
        }

        const uint32_t decoded_frames = valid_bytes / info->block_align;
        decode_block(io_buffer,
                     &page->data[write_frame * page->stride_floats],
                     decoded_frames);
        write_frame += decoded_frames;
        remaining_frames -= decoded_frames;
    }

    sample_page_cache_set_state(page, SAMPLE_PAGE_READY);
    return SAMPLE_PAGE_LOAD_OK;
}

static float sample_page_cache_decode_s24_le(const uint8_t *src)
{
    int32_t value = ((int32_t)src[0]) | (((int32_t)src[1]) << 8) | (((int32_t)src[2]) << 16);
    if((value & 0x00800000L) != 0)
    {
        value |= (int32_t)0xFF000000L;
    }
    return (float)value * (1.0f / 8388608.0f);
}

static sample_page_load_result_t sample_page_cache_decode_raw_pcm24_page(FIL *fp,
                                                                         sample_page_desc_t *page,
                                                                         uint8_t *io_buffer,
                                                                         uint32_t io_buffer_size)
{
    if((fp == 0) || (page == 0) || (io_buffer == 0)
            || (io_buffer_size < LOOPER_STORAGE_RAW_BYTES_PER_FRAME))
    {
        return SAMPLE_PAGE_LOAD_INVALID_ARG;
    }

    uint32_t remaining_frames = page->frame_count;
    uint32_t write_frame = 0U;

    while(remaining_frames != 0U)
    {
        uint32_t request_frames = remaining_frames;
        uint32_t request_bytes = request_frames * LOOPER_STORAGE_RAW_BYTES_PER_FRAME;
        if(request_bytes > io_buffer_size)
        {
            request_bytes = io_buffer_size
                - (io_buffer_size % LOOPER_STORAGE_RAW_BYTES_PER_FRAME);
        }
        if(request_bytes == 0U)
        {
            return SAMPLE_PAGE_LOAD_INVALID_ARG;
        }

        UINT br = 0U;
        const FRESULT fr = f_read(fp, io_buffer, request_bytes, &br);
        if(fr != FR_OK)
        {
            return SAMPLE_PAGE_LOAD_READ_FAILED;
        }

        const uint32_t valid_bytes = br - (br % LOOPER_STORAGE_RAW_BYTES_PER_FRAME);
        if(valid_bytes == 0U)
        {
            return SAMPLE_PAGE_LOAD_READ_FAILED;
        }

        uint32_t pos = 0U;
        while((pos + LOOPER_STORAGE_RAW_BYTES_PER_FRAME <= valid_bytes)
                && (remaining_frames != 0U))
        {
            page->data[(write_frame * SAMPLE_PAGE_FRAME_STRIDE_FLOATS)] =
                sample_page_cache_decode_s24_le(&io_buffer[pos]);
            page->data[(write_frame * SAMPLE_PAGE_FRAME_STRIDE_FLOATS) + 1U] =
                sample_page_cache_decode_s24_le(&io_buffer[pos + 3U]);
            write_frame++;
            remaining_frames--;
            pos += LOOPER_STORAGE_RAW_BYTES_PER_FRAME;
        }
    }

    sample_page_cache_set_state(page, SAMPLE_PAGE_READY);
    return SAMPLE_PAGE_LOAD_OK;
}

void sample_page_cache_init(void)
{
    sample_page_cache_reset();
    g_sample_page_cache_state.initialized = 1U;
}

void sample_page_cache_reset(void)
{
    memset(&g_sample_page_cache_state, 0, sizeof(g_sample_page_cache_state));
    memset(g_sample_page_sample_desc, 0, sizeof(g_sample_page_sample_desc));
    memset(g_sample_page_index, 0, sizeof(g_sample_page_index));
    memset(g_sample_page_window_lock, 0, sizeof(g_sample_page_window_lock));
    memset(g_sample_page_queued_count, 0, sizeof(g_sample_page_queued_count));
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
            sample_page_cache_clear_desc(&g_sample_page_desc[i], i);
        }
    }

    memset(&g_sample_page_sample_desc[key_slot], 0, sizeof(g_sample_page_sample_desc[key_slot]));
    g_sample_page_sample_desc[key_slot].key = key;
    g_sample_page_sample_desc[key_slot].first_slot = UINT16_MAX;
    g_sample_page_last_slot[key_slot] = UINT16_MAX;
}

uint8_t sample_page_cache_cancel_queued_page_key(sample_audio_key_t key,
                                                 uint32_t page_index,
                                                 uint8_t reason)
{
    (void)reason;
    sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    if ((page == 0) || (page->state != SAMPLE_PAGE_QUEUED))
    {
        return 0U;
    }
    if ((page->pin_count != 0U) || (page->use_count != 0U) || (page->window_pin_count != 0U))
    {
        return 0U;
    }

    sample_page_cache_clear_desc(page, (uint32_t)(page - g_sample_page_desc));
    return 1U;
}

uint32_t sample_page_cache_cancel_queued_key(sample_audio_key_t key, uint8_t reason)
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
        if ((page->state == SAMPLE_PAGE_QUEUED)
            && (sample_audio_key_equal(&page->key, &key) != 0U)
            && (page->pin_count == 0U)
            && (page->use_count == 0U)
            && (page->window_pin_count == 0U))
        {
            sample_page_cache_clear_desc(page, i);
            count++;
        }
    }
    return count;
}

uint32_t sample_page_cache_cancel_queued_domain(sample_audio_domain_t domain, uint8_t reason)
{
    (void)reason;
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
    {
        sample_page_desc_t *const page = &g_sample_page_desc[i];
        if ((page->state == SAMPLE_PAGE_QUEUED)
            && (page->key.domain == domain)
            && (page->pin_count == 0U)
            && (page->use_count == 0U)
            && (page->window_pin_count == 0U))
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
    const sample_page_state_t state = (page != 0) ? page->state : SAMPLE_PAGE_EMPTY;
    if (state == SAMPLE_PAGE_READY)
    {
        __DMB();
    }
    return state;
}

const sample_page_desc_t *sample_page_cache_get_page_desc(uint32_t slot_index)
{
    if (slot_index >= SAMPLE_PAGE_MAX_COUNT)
    {
        return 0;
    }

    return &g_sample_page_desc[slot_index];
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
        sample_page_cache_clear_desc(&g_sample_page_desc[slot_index], slot_index);
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
        if (g_sample_page_desc[i].state == SAMPLE_PAGE_EMPTY)
        {
            free_pages++;
        }
    }
    return free_pages * SAMPLE_PAGE_BYTES;
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

uint8_t sample_page_cache_has_queued_range(uint16_t first_sample_id,
                                           uint16_t sample_count)
{
    return sample_page_cache_has_queued_domain_range(SAMPLE_AUDIO_DOMAIN_CLASSIC,
                                                     first_sample_id,
                                                     sample_count);
}

uint8_t sample_page_cache_has_queued_domain_range(sample_audio_domain_t domain,
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

        if ((g_sample_page_queued_count[key_slot] != 0U)
            && (sample_page_cache_repair_queued_count_key(key) != 0U))
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

    const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if (sample_page_cache_sample_is_stream_loadable_key(key) == 0U)
    {
        return 0U;
    }

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
    out_info->raw_pcm24 = sample->raw_pcm24;
    return 1U;
}

uint8_t sample_page_cache_find_queued_load_target(uint16_t first_sample_id,
                                                  uint16_t sample_count,
                                                  sample_page_load_target_t *out_target)
{
    if ((first_sample_id >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) || (sample_count == 0U)
        || (out_target == 0))
    {
        return 0U;
    }

    uint32_t end_sample_id = (uint32_t)first_sample_id + (uint32_t)sample_count;
    if (end_sample_id > SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        end_sample_id = SAMPLE_PAGE_CACHE_MAX_SAMPLES;
    }

    for (uint16_t sample_id = first_sample_id; sample_id < end_sample_id; ++sample_id)
    {
        if (sample_page_cache_sample_is_stream_loadable(sample_id) == 0U)
        {
            continue;
        }

        for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
        {
            sample_page_desc_t *const page = &g_sample_page_desc[i];
            if ((page->sample_id != sample_id) || (page->state != SAMPLE_PAGE_QUEUED)
                || (page->data == 0))
            {
                continue;
            }

            memset(out_target, 0, sizeof(*out_target));
            out_target->key = sample_audio_key_classic(sample_id);
            out_target->sample_id = sample_id;
            out_target->slot_index = (uint16_t)i;
            out_target->page_index = page->page_index;
            out_target->start_frame = page->start_frame;
            out_target->frame_count = page->frame_count;
            out_target->format = page->format;
            out_target->stride_floats = page->stride_floats;
            out_target->frames_per_page = page->frames_per_page;
            out_target->registration_epoch = page->registration_epoch;
            out_target->frames_interleaved = page->data;
            return 1U;
        }
    }

    return 0U;
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
    if ((page == 0) || (page->state != SAMPLE_PAGE_QUEUED) || (page->data == 0))
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

    sample_page_cache_set_state(page, state);
    return 1U;
}

uint8_t sample_page_cache_acquire_window_page_key(sample_audio_key_t key,
                                                  uint32_t page_index,
                                                  uint8_t owner_kind,
                                                  uint8_t owner_id,
                                                  uint32_t owner_generation)
{
    if ((owner_kind == 0U) || (sample_page_cache_key_valid(key) == 0U))
    {
        return 0U;
    }

    sample_page_desc_t *page = sample_page_cache_find_page_mut_key(key, page_index);
    if (page == 0)
    {
        page = sample_page_cache_alloc_empty_slot_key(key,
                                                      page_index,
                                                      SAMPLE_PAGE_ALLOC_VOICE_WINDOW);
        if (page == 0)
        {
            return 0U;
        }
    }

    if ((page->state != SAMPLE_PAGE_READY) && (page->state != SAMPLE_PAGE_QUEUED)
        && (page->state != SAMPLE_PAGE_LOADING))
    {
        return 0U;
    }

    const uint16_t slot_index = (uint16_t)(page - g_sample_page_desc);
    sample_page_window_lock_t *free_lock = 0;
    for (uint32_t i = 0U; i < SAMPLE_PAGE_WINDOW_LOCK_MAX; ++i)
    {
        sample_page_window_lock_t *const lock = &g_sample_page_window_lock[i];
        if (lock->used == 0U)
        {
            if (free_lock == 0)
            {
                free_lock = lock;
            }
            continue;
        }

        if ((lock->slot_index == slot_index)
            && (lock->owner_kind == owner_kind)
            && (lock->owner_id == owner_id)
            && (lock->owner_generation == owner_generation))
        {
            return 1U;
        }
    }

    if (free_lock == 0)
    {
        return 0U;
    }
    if (page->window_pin_count == UINT16_MAX)
    {
        return 0U;
    }

    free_lock->used = 1U;
    free_lock->owner_kind = owner_kind;
    free_lock->owner_id = owner_id;
    free_lock->slot_index = slot_index;
    free_lock->lock_count = 1U;
    free_lock->owner_generation = owner_generation;
    page->window_pin_count++;
    return 1U;
}

uint8_t sample_page_cache_find_window_owner_key(sample_audio_key_t key,
                                                uint32_t page_index,
                                                sample_page_window_owner_t *out_owner)
{
    if ((sample_page_cache_key_valid(key) == 0U) || (out_owner == 0))
    {
        return 0U;
    }

    const sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    if (page == 0)
    {
        return 0U;
    }

    const uint16_t slot_index = (uint16_t)(page - g_sample_page_desc);
    for (uint32_t i = 0U; i < SAMPLE_PAGE_WINDOW_LOCK_MAX; ++i)
    {
        const sample_page_window_lock_t *const lock = &g_sample_page_window_lock[i];
        if ((lock->used == 0U) || (lock->slot_index != slot_index))
        {
            continue;
        }

        out_owner->owner_kind = lock->owner_kind;
        out_owner->owner_id = lock->owner_id;
        out_owner->reserved = 0U;
        out_owner->owner_generation = lock->owner_generation;
        return 1U;
    }

    return 0U;
}

void sample_page_cache_release_window_owner(uint8_t owner_kind,
                                            uint8_t owner_id,
                                            uint32_t owner_generation)
{
    if (owner_kind == 0U)
    {
        return;
    }

    for (uint32_t i = 0U; i < SAMPLE_PAGE_WINDOW_LOCK_MAX; ++i)
    {
        sample_page_window_lock_t *const lock = &g_sample_page_window_lock[i];
        if (lock->used == 0U)
        {
            continue;
        }
        if ((lock->owner_kind != owner_kind)
            || (lock->owner_id != owner_id)
            || (lock->owner_generation != owner_generation))
        {
            continue;
        }

        if (lock->slot_index < SAMPLE_PAGE_MAX_COUNT)
        {
            sample_page_desc_t *const page = &g_sample_page_desc[lock->slot_index];
            if (page->window_pin_count >= lock->lock_count)
            {
                page->window_pin_count -= lock->lock_count;
            }
            else
            {
                page->window_pin_count = 0U;
            }
        }

        memset(lock, 0, sizeof(*lock));
    }
}

void sample_page_cache_release_window_owner_outside_key(uint8_t owner_kind,
                                                        uint8_t owner_id,
                                                        uint32_t owner_generation,
                                                        sample_audio_key_t key,
                                                        uint32_t first_page,
                                                        uint32_t last_page)
{
    if ((owner_kind == 0U) || (sample_page_cache_key_valid(key) == 0U))
    {
        return;
    }

    for (uint32_t i = 0U; i < SAMPLE_PAGE_WINDOW_LOCK_MAX; ++i)
    {
        sample_page_window_lock_t *const lock = &g_sample_page_window_lock[i];
        if ((lock->used == 0U)
            || (lock->owner_kind != owner_kind)
            || (lock->owner_id != owner_id)
            || (lock->owner_generation != owner_generation)
            || (lock->slot_index >= SAMPLE_PAGE_MAX_COUNT))
        {
            continue;
        }

        sample_page_desc_t *const page = &g_sample_page_desc[lock->slot_index];
        if ((sample_audio_key_equal(&page->key, &key) == 0U)
            || ((page->page_index >= first_page) && (page->page_index <= last_page)))
        {
            continue;
        }

        if (page->window_pin_count >= lock->lock_count)
        {
            page->window_pin_count -= lock->lock_count;
        }
        else
        {
            page->window_pin_count = 0U;
        }
        memset(lock, 0, sizeof(*lock));
    }
}

uint8_t sample_page_cache_has_window_locks(void)
{
    for (uint32_t i = 0U; i < SAMPLE_PAGE_WINDOW_LOCK_MAX; ++i)
    {
        if (g_sample_page_window_lock[i].used != 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

#if defined(BRICK6_MULTI_STREAM_DIAG)
uint8_t sample_page_cache_get_window_page_debug(sample_audio_key_t key,
                                                uint32_t page_index,
                                                uint8_t owner_kind,
                                                uint8_t owner_id,
                                                uint32_t owner_generation,
                                                sample_page_window_debug_t *out_debug)
{
    if (out_debug == 0)
    {
        return 0U;
    }

    memset(out_debug, 0, sizeof(*out_debug));
    out_debug->page_index = page_index;
    out_debug->slot_index = UINT16_MAX;
    out_debug->state = SAMPLE_PAGE_EMPTY;

    const uint32_t primask = sample_page_cache_lock();
    const sample_page_desc_t *const page = sample_page_cache_find_page_key(key, page_index);
    if (page == 0)
    {
        sample_page_cache_unlock(primask);
        return 0U;
    }

    out_debug->generation = page->generation;
    out_debug->slot_index = (uint16_t)(page - g_sample_page_desc);
    out_debug->frame_count = (page->frame_count > UINT16_MAX)
                                 ? UINT16_MAX
                                 : (uint16_t)page->frame_count;
    out_debug->use_count = page->use_count;
    out_debug->window_pin_count = page->window_pin_count;
    out_debug->state = page->state;
    for (uint32_t i = 0U; i < SAMPLE_PAGE_WINDOW_LOCK_MAX; ++i)
    {
        const sample_page_window_lock_t *const lock = &g_sample_page_window_lock[i];
        if ((lock->used != 0U)
            && (lock->slot_index == out_debug->slot_index)
            && (lock->owner_kind == owner_kind)
            && (lock->owner_id == owner_id)
            && (lock->owner_generation == owner_generation))
        {
            out_debug->owner_lock_count = (lock->lock_count > UINT8_MAX)
                                              ? UINT8_MAX
                                              : (uint8_t)lock->lock_count;
            break;
        }
    }
    sample_page_cache_unlock(primask);
    return 1U;
}

uint32_t sample_page_cache_debug_count_window_locks(void)
{
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < SAMPLE_PAGE_WINDOW_LOCK_MAX; ++i)
    {
        if (g_sample_page_window_lock[i].used != 0U)
        {
            count++;
        }
    }
    return count;
}

uint32_t sample_page_cache_debug_count_free_pages(void)
{
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
    {
        if (g_sample_page_desc[i].state == SAMPLE_PAGE_EMPTY)
        {
            count++;
        }
    }
    return count;
}
#endif

uint8_t sample_page_cache_request_page(uint16_t sample_id, uint32_t page_index)
{
    return sample_page_cache_request_page_key(sample_audio_key_classic(sample_id), page_index);
}

uint8_t sample_page_cache_request_page_key(sample_audio_key_t key, uint32_t page_index)
{
    return sample_page_cache_request_page_key_alloc(key,
                                                    page_index,
                                                    SAMPLE_PAGE_ALLOC_LEGACY_DEFAULT);
}

uint8_t sample_page_cache_request_page_key_alloc(sample_audio_key_t key,
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

    if (page->state == SAMPLE_PAGE_EMPTY)
    {
        sample_page_cache_set_state(page, SAMPLE_PAGE_QUEUED);
    }

    sample_page_cache_set_page_geometry(page, key);
    page->start_frame = sample_audio_format_page_start_frame(page->format, page_index);
    page->frame_count = sample_page_cache_stream_page_frame_count_key(key, page_index);
    if (page->frame_count == 0U)
    {
        sample_page_cache_set_state(page, SAMPLE_PAGE_ERROR);
        return 0U;
    }

    page->last_touch = ++g_sample_page_cache_state.touch_counter;
    return 1U;
}

uint8_t sample_page_cache_request_page_ref(uint16_t sample_id,
                                           uint32_t page_index,
                                           sample_page_ref_t *out_ref)
{
    return sample_page_cache_request_page_ref_key(sample_audio_key_classic(sample_id),
                                                  page_index,
                                                  out_ref);
}

uint8_t sample_page_cache_request_page_ref_key(sample_audio_key_t key,
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

    if (page->state == SAMPLE_PAGE_EMPTY)
    {
        sample_page_cache_set_state(page, SAMPLE_PAGE_QUEUED);
    }

    sample_page_cache_set_page_geometry(page, key);
    page->start_frame = sample_audio_format_page_start_frame(page->format, page_index);
    page->frame_count = sample_page_cache_stream_page_frame_count_key(key, page_index);
    if (page->frame_count == 0U)
    {
        sample_page_cache_set_state(page, SAMPLE_PAGE_ERROR);
        return 0U;
    }

    page->last_touch = ++g_sample_page_cache_state.touch_counter;
    if (out_ref != 0)
    {
        (void)sample_page_cache_fill_ref(page, out_ref);
    }
    return 1U;
}

uint8_t sample_page_cache_request_start_pages(uint16_t sample_id,
                                              uint32_t start_frame,
                                              uint32_t page_count)
{
    return sample_page_cache_request_start_pages_key(sample_audio_key_classic(sample_id),
                                                     start_frame,
                                                     page_count);
}

uint8_t sample_page_cache_request_start_pages_key(sample_audio_key_t key,
                                                  uint32_t start_frame,
                                                  uint32_t page_count)
{
    return sample_page_cache_request_start_pages_key_alloc(
        key,
        start_frame,
        page_count,
        SAMPLE_PAGE_ALLOC_LEGACY_DEFAULT);
}

uint8_t sample_page_cache_request_start_pages_key_alloc(sample_audio_key_t key,
                                                        uint32_t start_frame,
                                                        uint32_t page_count,
                                                        sample_page_alloc_type_t alloc_type)
{
    const uint32_t first_page = sample_audio_format_page_index_from_frame(
        sample_page_cache_format_key(key), start_frame);
    uint8_t ok = 1U;

    for (uint32_t i = 0U; i < page_count; ++i)
    {
        if (sample_page_cache_request_page_key_alloc(key, first_page + i, alloc_type) == 0U)
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

sample_page_load_result_t sample_page_cache_load_full_sample(uint16_t sample_id,
                                                             FIL *fp,
                                                             const wav_info_t *info,
                                                             uint32_t total_frames,
                                                             uint32_t data_offset,
                                                             uint8_t *io_buffer,
                                                             uint32_t io_buffer_size)
{
    return sample_page_cache_load_full_sample_key(sample_audio_key_classic(sample_id),
                                                  fp,
                                                  info,
                                                  total_frames,
                                                  data_offset,
                                                  io_buffer,
                                                  io_buffer_size);
}

sample_page_load_result_t sample_page_cache_load_full_sample_key(sample_audio_key_t key,
                                                                 FIL *fp,
                                                                 const wav_info_t *info,
                                                                 uint32_t total_frames,
                                                                 uint32_t data_offset,
                                                                 uint8_t *io_buffer,
                                                                 uint32_t io_buffer_size)
{
    return sample_page_cache_load_full_sample_key_alloc(
        key,
        fp,
        info,
        total_frames,
        data_offset,
        io_buffer,
        io_buffer_size,
        SAMPLE_PAGE_ALLOC_LEGACY_DEFAULT);
}

sample_page_load_result_t sample_page_cache_load_full_sample_key_alloc(
    sample_audio_key_t key,
    FIL *fp,
    const wav_info_t *info,
    uint32_t total_frames,
    uint32_t data_offset,
    uint8_t *io_buffer,
    uint32_t io_buffer_size,
    sample_page_alloc_type_t alloc_type)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if ((key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) || (fp == 0) || (info == 0) || (io_buffer == 0)
        || (info->block_align == 0U) || (total_frames == 0U))
    {
        return SAMPLE_PAGE_LOAD_INVALID_ARG;
    }

    if ((info->channels != 1U) && (info->channels != 2U))
    {
        return SAMPLE_PAGE_LOAD_UNSUPPORTED_SAMPLE;
    }

    const sample_audio_format_t format = sample_audio_format_from_channels(info->channels);
    const uint32_t frames_per_page = sample_audio_format_frames_per_page(format);
    const uint32_t page_count = sample_audio_format_required_page_count(format, total_frames);
    uint32_t range_start = 0U;
    uint32_t range_count = SAMPLE_PAGE_MAX_COUNT;
    sample_page_cache_alloc_range(alloc_type, &range_start, &range_count);
    int32_t start_slot =
        sample_page_cache_find_contiguous_empty_run_in_range(page_count,
                                                             range_start,
                                                             range_count);
    if (start_slot < 0)
    {
        sample_page_cache_reclaim_stream_pages_for_full_load_in_range(range_start, range_count);
        start_slot = sample_page_cache_find_contiguous_empty_run_in_range(page_count,
                                                                         range_start,
                                                                         range_count);
    }
    if (start_slot < 0)
    {
        return SAMPLE_PAGE_LOAD_NO_SPACE;
    }

    sample_page_cache_clear_key(key);
    sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    sample->key = key;
    sample->info = *info;
    sample->total_frames = total_frames;
    sample->data_offset = data_offset;
    sample->format = format;
    sample->stride_floats = (uint16_t)sample_audio_format_stride_floats(format);
    sample->frames_per_page = frames_per_page;
    sample->registration_epoch = ++g_sample_page_cache_state.registration_epoch_counter;
    sample->valid = 1U;
    sample->fully_loaded = 1U;

    const FRESULT seek_fr = f_lseek(fp, (FSIZE_t)data_offset);
    if (seek_fr != FR_OK)
    {
        return SAMPLE_PAGE_LOAD_SEEK_FAILED;
    }

    for (uint32_t i = 0U; i < page_count; ++i)
    {
        const uint32_t start_frame = sample_audio_format_page_start_frame(format, i);
        uint32_t frame_count = total_frames - start_frame;
        if (frame_count > frames_per_page)
        {
            frame_count = frames_per_page;
        }

        sample_page_desc_t *const page =
            sample_page_cache_assign_slot_key((uint32_t)start_slot + i, key, i, frame_count);
        if (page == 0)
        {
            sample_page_cache_clear_key(key);
            return SAMPLE_PAGE_LOAD_NO_SPACE;
        }

        sample_page_cache_set_state(page, SAMPLE_PAGE_LOADING);
        const sample_page_load_result_t load_result =
            sample_page_cache_decode_page(fp, info, page, io_buffer, io_buffer_size);
        if (load_result != SAMPLE_PAGE_LOAD_OK)
        {
            sample_page_cache_set_state(page, SAMPLE_PAGE_ERROR);
            sample_page_cache_clear_key(key);
            return load_result;
        }
    }

    g_sample_page_sample_desc[key_slot].key = key;
    g_sample_page_sample_desc[key_slot].first_slot = (uint16_t)start_slot;
    g_sample_page_sample_desc[key_slot].page_count = (uint16_t)page_count;
    g_sample_page_sample_desc[key_slot].total_frames = total_frames;
    g_sample_page_sample_desc[key_slot].data_offset = data_offset;
    g_sample_page_sample_desc[key_slot].info = *info;
    g_sample_page_sample_desc[key_slot].valid = 1U;
    g_sample_page_sample_desc[key_slot].fully_loaded = 1U;
    return SAMPLE_PAGE_LOAD_OK;
}

uint8_t sample_page_cache_register_stream_sample(uint16_t sample_id,
                                                 const char *path,
                                                 const wav_info_t *info,
                                                 uint32_t total_frames,
                                                 uint32_t data_offset)
{
    return sample_page_cache_register_stream_sample_key(sample_audio_key_classic(sample_id),
                                                        path,
                                                        info,
                                                        total_frames,
                                                        data_offset);
}

uint8_t sample_page_cache_register_stream_sample_key(sample_audio_key_t key,
                                                     const char *path,
                                                     const wav_info_t *info,
                                                     uint32_t total_frames,
                                                     uint32_t data_offset)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if ((key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) || (path == 0) || (info == 0)
        || (total_frames == 0U) || (info->block_align == 0U))
    {
        return 0U;
    }

    const sample_audio_format_t format = sample_audio_format_from_channels(info->channels);
    if (sample_audio_format_is_valid(format) == 0U)
    {
        return 0U;
    }

    sample_page_cache_clear_key(key);
    sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if (sample_page_cache_trim_path_copy(sample->path, sizeof(sample->path), path) == 0U)
    {
        return 0U;
    }

    sample->key = key;
    sample->info = *info;
    sample->total_frames = total_frames;
    sample->data_offset = data_offset;
    sample->format = format;
    sample->stride_floats = (uint16_t)sample_audio_format_stride_floats(format);
    sample->frames_per_page = sample_audio_format_frames_per_page(format);
    sample->registration_epoch = ++g_sample_page_cache_state.registration_epoch_counter;
    sample_stream_safe_metadata_init_fatfs(key,
                                           info,
                                           total_frames,
                                           data_offset,
                                           &sample->stream_safe);
    (void)sample_stream_fatfs_map_certify_contiguous(key,
                                                     sample->path,
                                                     info,
                                                     total_frames,
                                                     data_offset,
                                                     &sample->stream_safe);
    sample->valid = 1U;
    sample->fully_loaded = 0U;
    sample->first_slot = UINT16_MAX;
    return 1U;
}

uint8_t sample_page_cache_register_raw_pcm24_stereo_sample(uint16_t sample_id,
                                                           const char *path,
                                                           uint32_t total_frames)
{
    return sample_page_cache_register_raw_pcm24_stereo_sample_key(sample_audio_key_classic(sample_id),
                                                                  path,
                                                                  total_frames);
}

uint8_t sample_page_cache_register_raw_pcm24_stereo_sample_key(sample_audio_key_t key,
                                                               const char *path,
                                                               uint32_t total_frames)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if((key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) || (path == 0) || (total_frames == 0U))
    {
        return 0U;
    }

    sample_page_cache_clear_key(key);
    sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if(sample_page_cache_trim_path_copy(sample->path, sizeof(sample->path), path) == 0U)
    {
        return 0U;
    }

    sample->key = key;
    sample->info.sample_rate = LOOPER_STORAGE_RAW_SAMPLE_RATE_HZ;
    sample->info.channels = LOOPER_STORAGE_RAW_CHANNELS;
    sample->info.bits_per_sample = LOOPER_STORAGE_RAW_BITS_PER_SAMPLE;
    sample->info.block_align = LOOPER_STORAGE_RAW_BYTES_PER_FRAME;
    sample->info.byte_rate = LOOPER_STORAGE_RAW_SAMPLE_RATE_HZ * LOOPER_STORAGE_RAW_BYTES_PER_FRAME;
    sample->total_frames = total_frames;
    sample->data_offset = 0U;
    sample->format = SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED;
    sample->stride_floats = (uint16_t)sample_audio_format_stride_floats(sample->format);
    sample->frames_per_page = sample_audio_format_frames_per_page(sample->format);
    sample->registration_epoch = ++g_sample_page_cache_state.registration_epoch_counter;
    sample_stream_safe_metadata_init_fatfs(key,
                                           &sample->info,
                                           total_frames,
                                           0U,
                                           &sample->stream_safe);
    sample->valid = 1U;
    sample->fully_loaded = 0U;
    sample->raw_pcm24 = 1U;
    sample->first_slot = UINT16_MAX;
    return 1U;
}

void sample_page_cache_service_range(uint16_t first_sample_id,
                                     uint16_t sample_count,
                                     uint32_t byte_budget)
{
    sample_page_cache_service_domain_range(SAMPLE_AUDIO_DOMAIN_CLASSIC,
                                           first_sample_id,
                                           sample_count,
                                           byte_budget);
}

void sample_page_cache_service_domain_range(sample_audio_domain_t domain,
                                            uint16_t first_object_id,
                                            uint16_t object_count,
                                            uint32_t byte_budget)
{
    /*
     * Legacy/transient range loader. Sampler pool STREAM service is routed
     * through sample_stream_manager; callers here must already own the SD gate.
     */
    if ((byte_budget == 0U) || (object_count == 0U))
    {
        return;
    }

    uint8_t io_buffer[4096U];

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

        sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
        if ((sample->valid == 0U) || (sample->fully_loaded != 0U) || (sample->path[0] == '\0'))
        {
            continue;
        }

        for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
        {
            sample_page_desc_t *const page = &g_sample_page_desc[i];
            if ((sample_audio_key_equal(&page->key, &key) == 0U)
                || (page->state != SAMPLE_PAGE_QUEUED))
            {
                continue;
            }

            FIL fp;
            const FRESULT open_fr = f_open(&fp, sample->path, FA_READ);
            if (open_fr != FR_OK)
            {
                sample_page_cache_set_state(page, SAMPLE_PAGE_ERROR);
                return;
            }

            const FSIZE_t offset = (FSIZE_t)sample->data_offset
                                 + ((FSIZE_t)page->start_frame * (FSIZE_t)sample->info.block_align);
            if (f_lseek(&fp, offset) != FR_OK)
            {
                (void)f_close(&fp);
                sample_page_cache_set_state(page, SAMPLE_PAGE_ERROR);
                return;
            }

            sample_page_cache_set_state(page, SAMPLE_PAGE_LOADING);
            const sample_page_load_result_t load_result =
                (sample->raw_pcm24 != 0U)
                    ? sample_page_cache_decode_raw_pcm24_page(&fp, page, io_buffer, sizeof(io_buffer))
                    : sample_page_cache_decode_page(&fp,
                                                    &sample->info,
                                                    page,
                                                    io_buffer,
                                                    sizeof(io_buffer));
            (void)f_close(&fp);
            if (load_result != SAMPLE_PAGE_LOAD_OK)
            {
                sample_page_cache_set_state(page, SAMPLE_PAGE_ERROR);
                return;
            }

            const uint32_t consumed = page->frame_count * sample->info.block_align;
            if (consumed >= byte_budget)
            {
                return;
            }
            byte_budget -= consumed;
            if (byte_budget == 0U)
            {
                return;
            }
        }
    }
}
