#include "Sampler/sample_page_cache.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/wav_audio_codec.h"

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(float) == SAMPLE_PAGE_SAMPLE_BYTES, "sample_page_cache expects 32-bit float");
#endif

typedef struct
{
    uint8_t initialized;
    uint8_t reserved[3];
    uint32_t generation_counter;
    uint32_t touch_counter;
} sample_page_cache_state_t;

typedef struct
{
    char path[SAMPLE_POOL_PATH_MAX];
    wav_info_t info;
    uint16_t first_slot;
    uint16_t page_count;
    uint32_t total_frames;
    uint32_t data_offset;
    uint8_t valid;
    uint8_t fully_loaded;
} sample_page_sample_desc_t;

SDRAM_SAMPLES static sample_page_desc_t g_sample_page_desc[SAMPLE_PAGE_MAX_COUNT];
SDRAM_SAMPLES static float g_sample_page_data[SAMPLE_PAGE_MAX_COUNT][SAMPLE_PAGE_FRAMES
                                                                     * SAMPLE_PAGE_FRAME_STRIDE_FLOATS];
static CTRL_STATE sample_page_cache_state_t g_sample_page_cache_state;
static CTRL_STATE sample_page_sample_desc_t g_sample_page_sample_desc[SAMPLE_PAGE_CACHE_MAX_SAMPLES];
static CTRL_STATE uint16_t g_sample_page_last_slot[SAMPLE_PAGE_CACHE_MAX_SAMPLES];

static void sample_page_cache_clear_desc(sample_page_desc_t *page, uint32_t slot_index)
{
    if (page == 0)
    {
        return;
    }

    memset(page, 0, sizeof(*page));
    page->sample_id = UINT16_MAX;
    page->page_index = UINT32_MAX;
    page->start_frame = UINT32_MAX;
    page->data = &g_sample_page_data[slot_index][0];
    page->state = SAMPLE_PAGE_EMPTY;
}

static sample_page_desc_t *sample_page_cache_find_page_mut(uint16_t sample_id, uint32_t page_index)
{
    if (sample_id < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        const uint16_t last_slot = g_sample_page_last_slot[sample_id];
        if (last_slot < SAMPLE_PAGE_MAX_COUNT)
        {
            sample_page_desc_t *const last_page = &g_sample_page_desc[last_slot];
            if ((last_page->sample_id == sample_id) && (last_page->page_index == page_index))
            {
                return last_page;
            }
        }
    }

    for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
    {
        sample_page_desc_t *const page = &g_sample_page_desc[i];
        if ((page->sample_id == sample_id) && (page->page_index == page_index))
        {
            if (sample_id < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
            {
                g_sample_page_last_slot[sample_id] = (uint16_t)i;
            }
            return page;
        }
    }

    return 0;
}

static const sample_page_desc_t *sample_page_cache_find_page(uint16_t sample_id, uint32_t page_index)
{
    return sample_page_cache_find_page_mut(sample_id, page_index);
}

static uint8_t sample_page_cache_fill_ref(const sample_page_desc_t *page,
                                          sample_page_ref_t *out_ref)
{
    if ((page == 0) || (out_ref == 0))
    {
        return 0U;
    }

    out_ref->page_index = page->page_index;
    out_ref->page_generation = page->generation;
    out_ref->slot_index = (uint32_t)(page - g_sample_page_desc);
    return 1U;
}

static uint32_t sample_page_cache_stream_page_frame_count(uint16_t sample_id, uint32_t page_index)
{
    if (sample_id < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[sample_id];
        if ((sample->valid != 0U) && (sample->fully_loaded == 0U) && (sample->total_frames != 0U))
        {
            const uint32_t start_frame = page_index * SAMPLE_PAGE_FRAMES;
            if (start_frame >= sample->total_frames)
            {
                return 0U;
            }

            uint32_t frame_count = sample->total_frames - start_frame;
            if (frame_count > SAMPLE_PAGE_FRAMES)
            {
                frame_count = SAMPLE_PAGE_FRAMES;
            }
            return frame_count;
        }
    }

    return SAMPLE_PAGE_FRAMES;
}

static sample_page_desc_t *sample_page_cache_alloc_empty_slot(uint16_t sample_id, uint32_t page_index)
{
    sample_page_desc_t *evict_page = 0;
    const uint32_t frame_count = sample_page_cache_stream_page_frame_count(sample_id, page_index);
    if (frame_count == 0U)
    {
        return 0;
    }

    for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
    {
        sample_page_desc_t *const page = &g_sample_page_desc[i];
        if (page->state == SAMPLE_PAGE_EMPTY)
        {
            page->sample_id = sample_id;
            page->page_index = page_index;
            page->start_frame = page_index * SAMPLE_PAGE_FRAMES;
            page->frame_count = frame_count;
            page->generation = ++g_sample_page_cache_state.generation_counter;
            page->last_touch = ++g_sample_page_cache_state.touch_counter;
            page->pin_count = 0U;
            page->use_count = 0U;
            page->state = SAMPLE_PAGE_QUEUED;
            return page;
        }

        if ((page->state != SAMPLE_PAGE_READY) || (page->use_count != 0U) || (page->pin_count != 0U))
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

    const uint32_t slot_index = (uint32_t)(evict_page - g_sample_page_desc);
    sample_page_cache_clear_desc(evict_page, slot_index);
    evict_page->sample_id = sample_id;
    evict_page->page_index = page_index;
    evict_page->start_frame = page_index * SAMPLE_PAGE_FRAMES;
    evict_page->frame_count = frame_count;
    evict_page->generation = ++g_sample_page_cache_state.generation_counter;
    evict_page->last_touch = ++g_sample_page_cache_state.touch_counter;
    evict_page->pin_count = 0U;
    evict_page->use_count = 0U;
    evict_page->state = SAMPLE_PAGE_QUEUED;
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

static int32_t sample_page_cache_find_contiguous_empty_run(uint32_t page_count)
{
    if ((page_count == 0U) || (page_count > SAMPLE_PAGE_MAX_COUNT))
    {
        return -1;
    }

    for (uint32_t start = 0U; (start + page_count) <= SAMPLE_PAGE_MAX_COUNT; ++start)
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

static sample_page_desc_t *sample_page_cache_assign_slot(uint32_t slot_index,
                                                         uint16_t sample_id,
                                                         uint32_t page_index,
                                                         uint32_t frame_count)
{
    if (slot_index >= SAMPLE_PAGE_MAX_COUNT)
    {
        return 0;
    }

    sample_page_desc_t *const page = &g_sample_page_desc[slot_index];
    sample_page_cache_clear_desc(page, slot_index);
    page->sample_id = sample_id;
    page->page_index = page_index;
    page->start_frame = page_index * SAMPLE_PAGE_FRAMES;
    page->frame_count = frame_count;
    page->generation = ++g_sample_page_cache_state.generation_counter;
    page->last_touch = ++g_sample_page_cache_state.touch_counter;
    page->state = SAMPLE_PAGE_QUEUED;
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

        uint32_t pos = 0U;
        while ((pos + info->block_align <= valid_bytes) && (remaining_frames != 0U))
        {
            float left = 0.0f;
            float right = 0.0f;
            wav_audio_codec_decode_stereo_frame(&io_buffer[pos],
                                                info->channels,
                                                info->bits_per_sample,
                                                &left,
                                                &right);
            page->data[(write_frame * SAMPLE_PAGE_FRAME_STRIDE_FLOATS)] = left;
            page->data[(write_frame * SAMPLE_PAGE_FRAME_STRIDE_FLOATS) + 1U] = right;
            write_frame++;
            remaining_frames--;
            pos += info->block_align;
        }
    }

    page->state = SAMPLE_PAGE_READY;
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
    if (sample_id >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        return;
    }

    for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
    {
        if (g_sample_page_desc[i].sample_id == sample_id)
        {
            sample_page_cache_clear_desc(&g_sample_page_desc[i], i);
        }
    }

    memset(&g_sample_page_sample_desc[sample_id], 0, sizeof(g_sample_page_sample_desc[sample_id]));
    g_sample_page_sample_desc[sample_id].first_slot = UINT16_MAX;
    g_sample_page_last_slot[sample_id] = UINT16_MAX;
}

sample_page_state_t sample_page_cache_get_page_state(uint16_t sample_id, uint32_t page_index)
{
    const sample_page_desc_t *const page = sample_page_cache_find_page(sample_id, page_index);
    return (page != 0) ? page->state : SAMPLE_PAGE_EMPTY;
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
    if (out_span == 0)
    {
        return 0U;
    }

    memset(out_span, 0, sizeof(*out_span));

    sample_page_desc_t *const page = sample_page_cache_find_page_mut(sample_id, page_index);
    if ((page == 0) || (page->state != SAMPLE_PAGE_READY) || (page->data == 0))
    {
        return 0U;
    }

    page->use_count++;
    page->last_touch = ++g_sample_page_cache_state.touch_counter;

    out_span->frames_interleaved = page->data;
    out_span->frame_count = page->frame_count;
    out_span->start_frame = page->start_frame;
    out_span->page_index = page->page_index;
    out_span->page_generation = page->generation;
    out_span->slot_index = (uint32_t)(page - g_sample_page_desc);
    return 1U;
}

uint8_t sample_page_cache_try_acquire_page_ref(uint16_t sample_id,
                                               const sample_page_ref_t *ref,
                                               sample_page_span_t *out_span)
{
    if ((ref == 0) || (out_span == 0) || (ref->slot_index >= SAMPLE_PAGE_MAX_COUNT))
    {
        return 0U;
    }

    memset(out_span, 0, sizeof(*out_span));

    sample_page_desc_t *const page = &g_sample_page_desc[ref->slot_index];
    if ((page->sample_id != sample_id) || (page->page_index != ref->page_index)
        || (page->generation != ref->page_generation) || (page->state != SAMPLE_PAGE_READY)
        || (page->data == 0))
    {
        return 0U;
    }

    page->use_count++;
    out_span->frames_interleaved = page->data;
    out_span->frame_count = page->frame_count;
    out_span->start_frame = page->start_frame;
    out_span->page_index = page->page_index;
    out_span->page_generation = page->generation;
    out_span->slot_index = ref->slot_index;
    return 1U;
}

void sample_page_cache_release_page(uint16_t sample_id, uint32_t page_index)
{
    sample_page_desc_t *const page = sample_page_cache_find_page_mut(sample_id, page_index);
    if (page == 0)
    {
        return;
    }

    if (page->use_count != 0U)
    {
        page->use_count--;
    }
    page->last_touch = ++g_sample_page_cache_state.touch_counter;
}

void sample_page_cache_release_page_ref(uint16_t sample_id, const sample_page_ref_t *ref)
{
    if ((ref == 0) || (ref->slot_index >= SAMPLE_PAGE_MAX_COUNT))
    {
        return;
    }

    sample_page_desc_t *const page = &g_sample_page_desc[ref->slot_index];
    if ((page->sample_id != sample_id) || (page->page_index != ref->page_index)
        || (page->generation != ref->page_generation))
    {
        return;
    }

    if (page->use_count != 0U)
    {
        page->use_count--;
    }
}

const float *sample_page_cache_get_full_sample_base(uint16_t sample_id, uint32_t *out_frames)
{
    if (out_frames != 0)
    {
        *out_frames = 0U;
    }

    if (sample_id >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        return 0;
    }

    const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[sample_id];
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
    if (out_block == 0)
    {
        return 0U;
    }

    memset(out_block, 0, sizeof(*out_block));
    out_block->status = SAMPLE_PAGE_BLOCK_NOT_READY;

    if ((sample_id >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) || (max_frames == 0U))
    {
        return 0U;
    }

    const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[sample_id];
    if (sample->valid == 0U)
    {
        return 1U;
    }

    if (frame_index >= sample->total_frames)
    {
        out_block->status = SAMPLE_PAGE_BLOCK_DONE;
        return 1U;
    }

    const uint32_t page_index = frame_index / SAMPLE_PAGE_FRAMES;
    sample_page_span_t span;
    if (sample_page_cache_try_acquire_page(sample_id, page_index, &span) == 0U)
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
        &span.frames_interleaved[page_offset * SAMPLE_PAGE_FRAME_STRIDE_FLOATS];
    out_block->frame_count = frame_count;
    out_block->start_frame = frame_index;
    out_block->page_index = page_index;
    out_block->status = (frame_count != 0U) ? SAMPLE_PAGE_BLOCK_OK : SAMPLE_PAGE_BLOCK_NOT_READY;
    return 1U;
}

void sample_page_cache_commit_read_block(uint16_t sample_id,
                                         uint32_t page_index)
{
    sample_page_cache_release_page(sample_id, page_index);
}

uint8_t sample_page_cache_request_page(uint16_t sample_id, uint32_t page_index)
{
    sample_page_desc_t *page = sample_page_cache_find_page_mut(sample_id, page_index);
    if (page == 0)
    {
        page = sample_page_cache_alloc_empty_slot(sample_id, page_index);
        if (page == 0)
        {
            return 0U;
        }
    }

    if (page->state == SAMPLE_PAGE_EMPTY)
    {
        page->state = SAMPLE_PAGE_QUEUED;
    }

    page->frame_count = sample_page_cache_stream_page_frame_count(sample_id, page_index);
    if (page->frame_count == 0U)
    {
        page->state = SAMPLE_PAGE_ERROR;
        return 0U;
    }

    page->last_touch = ++g_sample_page_cache_state.touch_counter;
    return 1U;
}

uint8_t sample_page_cache_request_page_ref(uint16_t sample_id,
                                           uint32_t page_index,
                                           sample_page_ref_t *out_ref)
{
    sample_page_desc_t *page = sample_page_cache_find_page_mut(sample_id, page_index);
    if (page == 0)
    {
        page = sample_page_cache_alloc_empty_slot(sample_id, page_index);
        if (page == 0)
        {
            return 0U;
        }
    }

    if (page->state == SAMPLE_PAGE_EMPTY)
    {
        page->state = SAMPLE_PAGE_QUEUED;
    }

    page->frame_count = sample_page_cache_stream_page_frame_count(sample_id, page_index);
    if (page->frame_count == 0U)
    {
        page->state = SAMPLE_PAGE_ERROR;
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
    const uint32_t first_page = start_frame / SAMPLE_PAGE_FRAMES;
    uint8_t ok = 1U;

    for (uint32_t i = 0U; i < page_count; ++i)
    {
        if (sample_page_cache_request_page(sample_id, first_page + i) == 0U)
        {
            ok = 0U;
            break;
        }
    }

    return ok;
}

uint8_t sample_page_cache_pin_page(uint16_t sample_id, uint32_t page_index)
{
    sample_page_desc_t *page = sample_page_cache_find_page_mut(sample_id, page_index);
    if (page == 0)
    {
        page = sample_page_cache_alloc_empty_slot(sample_id, page_index);
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
    sample_page_desc_t *const page = sample_page_cache_find_page_mut(sample_id, page_index);
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
    if ((sample_id >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) || (fp == 0) || (info == 0) || (io_buffer == 0)
        || (info->block_align == 0U) || (total_frames == 0U))
    {
        return SAMPLE_PAGE_LOAD_INVALID_ARG;
    }

    if ((info->channels != 1U) && (info->channels != 2U))
    {
        return SAMPLE_PAGE_LOAD_UNSUPPORTED_SAMPLE;
    }

    const uint32_t page_count = (total_frames + SAMPLE_PAGE_FRAMES - 1U) / SAMPLE_PAGE_FRAMES;
    const int32_t start_slot = sample_page_cache_find_contiguous_empty_run(page_count);
    if (start_slot < 0)
    {
        return SAMPLE_PAGE_LOAD_NO_SPACE;
    }

    sample_page_cache_clear_sample(sample_id);

    const FRESULT seek_fr = f_lseek(fp, (FSIZE_t)data_offset);
    if (seek_fr != FR_OK)
    {
        return SAMPLE_PAGE_LOAD_SEEK_FAILED;
    }

    for (uint32_t i = 0U; i < page_count; ++i)
    {
        const uint32_t start_frame = i * SAMPLE_PAGE_FRAMES;
        uint32_t frame_count = total_frames - start_frame;
        if (frame_count > SAMPLE_PAGE_FRAMES)
        {
            frame_count = SAMPLE_PAGE_FRAMES;
        }

        sample_page_desc_t *const page =
            sample_page_cache_assign_slot((uint32_t)start_slot + i, sample_id, i, frame_count);
        if (page == 0)
        {
            sample_page_cache_clear_sample(sample_id);
            return SAMPLE_PAGE_LOAD_NO_SPACE;
        }

        page->state = SAMPLE_PAGE_LOADING;
        const sample_page_load_result_t load_result =
            sample_page_cache_decode_page(fp, info, page, io_buffer, io_buffer_size);
        if (load_result != SAMPLE_PAGE_LOAD_OK)
        {
            page->state = SAMPLE_PAGE_ERROR;
            sample_page_cache_clear_sample(sample_id);
            return load_result;
        }
    }

    g_sample_page_sample_desc[sample_id].first_slot = (uint16_t)start_slot;
    g_sample_page_sample_desc[sample_id].page_count = (uint16_t)page_count;
    g_sample_page_sample_desc[sample_id].total_frames = total_frames;
    g_sample_page_sample_desc[sample_id].data_offset = data_offset;
    g_sample_page_sample_desc[sample_id].info = *info;
    g_sample_page_sample_desc[sample_id].valid = 1U;
    g_sample_page_sample_desc[sample_id].fully_loaded = 1U;
    return SAMPLE_PAGE_LOAD_OK;
}

uint8_t sample_page_cache_register_stream_sample(uint16_t sample_id,
                                                 const char *path,
                                                 const wav_info_t *info,
                                                 uint32_t total_frames,
                                                 uint32_t data_offset)
{
    if ((sample_id >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) || (path == 0) || (info == 0)
        || (total_frames == 0U) || (info->block_align == 0U))
    {
        return 0U;
    }

    sample_page_cache_clear_sample(sample_id);
    sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[sample_id];
    if (sample_page_cache_trim_path_copy(sample->path, sizeof(sample->path), path) == 0U)
    {
        return 0U;
    }

    sample->info = *info;
    sample->total_frames = total_frames;
    sample->data_offset = data_offset;
    sample->valid = 1U;
    sample->fully_loaded = 0U;
    sample->first_slot = UINT16_MAX;
    return 1U;
}

void sample_page_cache_service(uint32_t byte_budget)
{
    /*
     * FatFs access is intentionally centralized here for stream-page loads.
     * Callers must already hold `sd_access_gate` for the sample-cache client.
     */
    if (byte_budget == 0U)
    {
        return;
    }

    uint8_t io_buffer[4096U];

    for (uint16_t sample_id = 0U; sample_id < SAMPLE_PAGE_CACHE_MAX_SAMPLES; ++sample_id)
    {
        sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[sample_id];
        if ((sample->valid == 0U) || (sample->fully_loaded != 0U) || (sample->path[0] == '\0'))
        {
            continue;
        }

        for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
        {
            sample_page_desc_t *const page = &g_sample_page_desc[i];
            if ((page->sample_id != sample_id) || (page->state != SAMPLE_PAGE_QUEUED))
            {
                continue;
            }

            FIL fp;
            const FRESULT open_fr = f_open(&fp, sample->path, FA_READ);
            if (open_fr != FR_OK)
            {
                page->state = SAMPLE_PAGE_ERROR;
                return;
            }

            const FSIZE_t offset = (FSIZE_t)sample->data_offset
                                 + ((FSIZE_t)page->start_frame * (FSIZE_t)sample->info.block_align);
            if (f_lseek(&fp, offset) != FR_OK)
            {
                (void)f_close(&fp);
                page->state = SAMPLE_PAGE_ERROR;
                return;
            }

            page->state = SAMPLE_PAGE_LOADING;
            const sample_page_load_result_t load_result =
                sample_page_cache_decode_page(&fp, &sample->info, page, io_buffer, sizeof(io_buffer));
            (void)f_close(&fp);
            if (load_result != SAMPLE_PAGE_LOAD_OK)
            {
                page->state = SAMPLE_PAGE_ERROR;
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
