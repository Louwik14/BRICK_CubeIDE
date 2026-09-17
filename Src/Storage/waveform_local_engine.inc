/* REC editor local engine, adapted from 363640867's editor tile cache.
 * Its PCM and 16/64/256 summaries stay hot independently of audio voices.
 * Only STORAGE fills tiles; the UI sees columns through waveform_service. */
#define WAVEFORM_LOCAL_TILE_COUNT 16U
#define WAVEFORM_LOCAL_TILE_FRAMES 24000U
#define WAVEFORM_LOCAL_CHUNK_BYTES 4092U
#define WAVEFORM_LOCAL_REQUEST_MAX 4U
#define WAVEFORM_LOCAL_KEEP_RADIUS 2U
#define WAVEFORM_LOCAL_MAX_VIEW_TILES 12U
#define WAVEFORM_LOCAL_LEVEL0_FRAMES 16U
#define WAVEFORM_LOCAL_LEVEL1_FRAMES 64U
#define WAVEFORM_LOCAL_LEVEL2_FRAMES 256U
#define WAVEFORM_LOCAL_LEVEL0_POINTS 1500U
#define WAVEFORM_LOCAL_LEVEL1_POINTS 375U
#define WAVEFORM_LOCAL_LEVEL2_POINTS 94U

typedef struct
{
    int16_t min, max, first, last;
} waveform_local_point_t;

typedef enum
{
    WAVEFORM_LOCAL_EMPTY = 0,
    WAVEFORM_LOCAL_LOADING,
    WAVEFORM_LOCAL_READY,
    WAVEFORM_LOCAL_STALE
} waveform_local_state_t;

typedef struct
{
    waveform_source_t source;
    uint32_t start_frame;
    uint32_t frame_count;
    uint32_t loaded_frames;
    uint32_t generation;
    waveform_local_state_t state;
} waveform_local_tile_t;

typedef struct
{
    waveform_source_t source;
    uint32_t focus_frame;
    uint32_t data_offset;
    uint32_t media_epoch;
    uint32_t generation;
    uint8_t valid;
    uint8_t format_ready;
    uint8_t desired;
    waveform_local_tile_t tiles[WAVEFORM_LOCAL_TILE_COUNT];
} waveform_local_cache_t;

EDITOR_AUDIO_CACHE_SDRAM static int16_t
    g_waveform_local_pcm[WAVEFORM_LOCAL_TILE_COUNT][WAVEFORM_LOCAL_TILE_FRAMES];
EDITOR_AUDIO_CACHE_SDRAM static waveform_local_point_t
    g_waveform_local_level0[WAVEFORM_LOCAL_TILE_COUNT][WAVEFORM_LOCAL_LEVEL0_POINTS];
EDITOR_AUDIO_CACHE_SDRAM static waveform_local_point_t
    g_waveform_local_level1[WAVEFORM_LOCAL_TILE_COUNT][WAVEFORM_LOCAL_LEVEL1_POINTS];
EDITOR_AUDIO_CACHE_SDRAM static waveform_local_point_t
    g_waveform_local_level2[WAVEFORM_LOCAL_TILE_COUNT][WAVEFORM_LOCAL_LEVEL2_POINTS];
STORAGE_STATE_SDRAM static waveform_local_cache_t g_waveform_local;

typedef struct
{
    waveform_source_t source;
    uint32_t start_frame;
    uint32_t frame_count;
    uint8_t width;
    uint8_t valid;
    waveform_column_t columns[126];
    waveform_line_column_t line[126];
} waveform_local_line_hot_t;
STORAGE_STATE_SDRAM static waveform_local_line_hot_t g_waveform_local_line_hot;

static uint32_t waveform_local_align(uint32_t frame)
{
    return (frame / WAVEFORM_LOCAL_TILE_FRAMES) * WAVEFORM_LOCAL_TILE_FRAMES;
}

static int8_t waveform_local_find(uint32_t start_frame)
{
    for(uint8_t i = 0U; i < WAVEFORM_LOCAL_TILE_COUNT; ++i)
    {
        const waveform_local_tile_t *const tile = &g_waveform_local.tiles[i];
        if(tile->start_frame == start_frame
                && tile->state != WAVEFORM_LOCAL_EMPTY
                && tile->state != WAVEFORM_LOCAL_STALE)
            return (int8_t)i;
    }
    return -1;
}

static uint32_t waveform_local_distance(uint32_t start_frame, uint32_t focus)
{
    if(focus < start_frame) { return start_frame - focus; }
    const uint32_t end = start_frame + WAVEFORM_LOCAL_TILE_FRAMES;
    if(focus >= end) { return focus - end + 1U; }
    return 0U;
}

static int8_t waveform_local_pick_slot(uint32_t focus)
{
    for(uint8_t i = 0U; i < WAVEFORM_LOCAL_TILE_COUNT; ++i)
    {
        if(g_waveform_local.tiles[i].state == WAVEFORM_LOCAL_EMPTY
                || g_waveform_local.tiles[i].state == WAVEFORM_LOCAL_STALE)
            return (int8_t)i;
    }
    int8_t candidate = -1;
    uint32_t worst = 0U;
    for(uint8_t i = 0U; i < WAVEFORM_LOCAL_TILE_COUNT; ++i)
    {
        const waveform_local_tile_t *const tile = &g_waveform_local.tiles[i];
        if(tile->state == WAVEFORM_LOCAL_LOADING) { continue; }
        const uint32_t distance = waveform_local_distance(tile->start_frame, focus);
        if(candidate < 0 || distance > worst)
        {
            candidate = (int8_t)i;
            worst = distance;
        }
    }
    return candidate;
}

static uint8_t waveform_local_queue(uint32_t start_frame, uint8_t *queued)
{
    if(start_frame >= g_waveform_local.source.frame_count
            || waveform_local_find(start_frame) >= 0
            || *queued >= WAVEFORM_LOCAL_REQUEST_MAX)
        return 0U;
    const int8_t slot = waveform_local_pick_slot(g_waveform_local.focus_frame);
    if(slot < 0) { return 0U; }
    waveform_local_tile_t *const tile = &g_waveform_local.tiles[(uint8_t)slot];
    tile->source = g_waveform_local.source;
    tile->start_frame = start_frame;
    tile->frame_count = g_waveform_local.source.frame_count - start_frame;
    if(tile->frame_count > WAVEFORM_LOCAL_TILE_FRAMES)
        tile->frame_count = WAVEFORM_LOCAL_TILE_FRAMES;
    tile->loaded_frames = 0U;
    tile->generation = ++g_waveform_local.generation;
    tile->state = WAVEFORM_LOCAL_LOADING;
    ++*queued;
    return 1U;
}

static void waveform_local_request(const waveform_source_t *source,
    uint32_t start_frame, uint32_t frame_count)
{
    if(g_waveform_local.valid == 0U
            || waveform_source_equal(&g_waveform_local.source, source) == 0U
            || g_waveform_local.media_epoch != sd_access_media_epoch())
    {
        memset(&g_waveform_local, 0, sizeof(g_waveform_local));
        g_waveform_local_line_hot.valid = 0U;
        g_waveform_local.source = *source;
        g_waveform_local.media_epoch = sd_access_media_epoch();
        g_waveform_local.valid = 1U;
    }
    uint32_t focus = start_frame + frame_count / 2U;
    if(focus >= source->frame_count) { focus = source->frame_count - 1U; }
    g_waveform_local.focus_frame = focus;
    g_waveform_local.desired = 1U;
    const uint32_t focus_tile = waveform_local_align(focus);
    const uint32_t first_tile = waveform_local_align(start_frame);
    const uint32_t last_tile = waveform_local_align(start_frame + frame_count - 1U);
    const uint32_t keep_first = (first_tile >=
        WAVEFORM_LOCAL_KEEP_RADIUS * WAVEFORM_LOCAL_TILE_FRAMES)
        ? first_tile - WAVEFORM_LOCAL_KEEP_RADIUS * WAVEFORM_LOCAL_TILE_FRAMES
        : 0U;
    const uint32_t keep_last = (last_tile <= UINT32_MAX
        - WAVEFORM_LOCAL_KEEP_RADIUS * WAVEFORM_LOCAL_TILE_FRAMES)
        ? last_tile + WAVEFORM_LOCAL_KEEP_RADIUS * WAVEFORM_LOCAL_TILE_FRAMES
        : UINT32_MAX;
    for(uint8_t i = 0U; i < WAVEFORM_LOCAL_TILE_COUNT; ++i)
    {
        waveform_local_tile_t *const tile = &g_waveform_local.tiles[i];
        if(tile->state == WAVEFORM_LOCAL_LOADING
                && (tile->start_frame < keep_first
                    || tile->start_frame > keep_last))
            tile->state = WAVEFORM_LOCAL_STALE;
    }
    uint8_t queued = 0U;
    (void)waveform_local_queue(focus_tile, &queued);
    for(uint32_t tile = first_tile; tile <= last_tile
            && queued < WAVEFORM_LOCAL_REQUEST_MAX; )
    {
        (void)waveform_local_queue(tile, &queued);
        if(tile > UINT32_MAX - WAVEFORM_LOCAL_TILE_FRAMES) { break; }
        tile += WAVEFORM_LOCAL_TILE_FRAMES;
    }
    if(first_tile >= WAVEFORM_LOCAL_TILE_FRAMES)
        (void)waveform_local_queue(first_tile - WAVEFORM_LOCAL_TILE_FRAMES,
            &queued);
    if(last_tile <= UINT32_MAX - WAVEFORM_LOCAL_TILE_FRAMES)
        (void)waveform_local_queue(last_tile + WAVEFORM_LOCAL_TILE_FRAMES,
            &queued);
}

static void waveform_local_accumulate_point(waveform_local_point_t *level,
    uint32_t block_frames, uint32_t frame, int16_t sample)
{
    waveform_local_point_t *const point = &level[frame / block_frames];
    if(frame % block_frames == 0U)
    {
        point->min = sample;
        point->max = sample;
        point->first = sample;
        point->last = sample;
        return;
    }
    if(sample < point->min) { point->min = sample; }
    if(sample > point->max) { point->max = sample; }
    point->last = sample;
}

static void waveform_local_accumulate_levels(uint8_t slot, uint32_t frame,
    int16_t sample)
{
    waveform_local_accumulate_point(g_waveform_local_level0[slot],
        WAVEFORM_LOCAL_LEVEL0_FRAMES, frame, sample);
    waveform_local_accumulate_point(g_waveform_local_level1[slot],
        WAVEFORM_LOCAL_LEVEL1_FRAMES, frame, sample);
    waveform_local_accumulate_point(g_waveform_local_level2[slot],
        WAVEFORM_LOCAL_LEVEL2_FRAMES, frame, sample);
}

static int8_t waveform_local_next_loading(void)
{
    int8_t best = -1;
    uint32_t distance = UINT32_MAX;
    for(uint8_t i = 0U; i < WAVEFORM_LOCAL_TILE_COUNT; ++i)
    {
        waveform_local_tile_t *const tile = &g_waveform_local.tiles[i];
        if(tile->state == WAVEFORM_LOCAL_STALE)
            tile->state = WAVEFORM_LOCAL_EMPTY;
        if(tile->state != WAVEFORM_LOCAL_LOADING) { continue; }
        const uint32_t d = waveform_local_distance(tile->start_frame,
            g_waveform_local.focus_frame);
        if(d < distance) { best = (int8_t)i; distance = d; }
    }
    return best;
}

static void waveform_local_storage_service(void)
{
    if(g_waveform_local.valid == 0U) { return; }
    const int8_t index = waveform_local_next_loading();
    if(index < 0) { return; }
    const char *path;
    if(g_waveform_local.media_epoch != sd_access_media_epoch()
            || waveform_source_current(&g_waveform_local.source, &path) == 0U)
    {
        g_waveform_local.valid = 0U;
        return;
    }
    waveform_local_tile_t *const tile = &g_waveform_local.tiles[(uint8_t)index];
    if(tile->loaded_frames >= tile->frame_count)
    {
        tile->state = WAVEFORM_LOCAL_READY;
        return;
    }
    const sd_scheduler_background_request_t admission =
        { WAVEFORM_LOCAL_CHUNK_BYTES, g_waveform_local.media_epoch,
            SD_SCHEDULER_BACKGROUND_DATA };
    if(sd_scheduler_runtime_background_try_begin(&admission)
            != SD_SCHEDULER_BACKGROUND_GO)
    {
        return;
    }
    FIL fp;
    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() == 0U
            || f_open(&fp, path, FA_READ) != FR_OK)
    {
        goto end;
    }
    if(g_waveform_local.format_ready == 0U)
    {
        wav_info_t info;
        if(wav_parser_parse_info(&fp, &info) == 0
                || info.audio_format != 1U
                || info.channels != 2U
                || info.bits_per_sample != 24U
                || info.block_align != 6U)
        {
            tile->state = WAVEFORM_LOCAL_EMPTY;
            (void)f_close(&fp);
            goto end;
        }
        g_waveform_local.data_offset = info.data_offset;
        g_waveform_local.format_ready = 1U;
    }
    uint32_t frames = tile->frame_count - tile->loaded_frames;
    if(frames > WAVEFORM_LOCAL_CHUNK_BYTES / 6U)
        frames = WAVEFORM_LOCAL_CHUNK_BYTES / 6U;
    const uint32_t bytes = frames * 6U;
    UINT read = 0U;
    if(f_lseek(&fp, g_waveform_local.data_offset
            + (tile->start_frame + tile->loaded_frames) * 6U) == FR_OK
            && f_read(&fp, g_waveform_read, bytes, &read) == FR_OK
            && read == bytes)
    {
        for(uint32_t f = 0U; f < frames; ++f)
        {
            const uint8_t *const pcm = &g_waveform_read[f * 6U];
            const int16_t left = waveform_decode_sample(pcm, 24U);
            const int16_t right = waveform_decode_sample(pcm + 3U, 24U);
            const int16_t mono = (int16_t)(((int32_t)left + right) / 2);
            const uint32_t rel = tile->loaded_frames + f;
            g_waveform_local_pcm[(uint8_t)index][rel] = mono;
            waveform_local_accumulate_levels((uint8_t)index, rel, mono);
        }
        tile->loaded_frames += frames;
        if(tile->loaded_frames >= tile->frame_count)
            tile->state = WAVEFORM_LOCAL_READY;
        ok = 1U;
    }
    (void)f_close(&fp);
end:
    sd_scheduler_runtime_background_end();
    if(ok == 0U && tile->state == WAVEFORM_LOCAL_LOADING)
        tile->state = WAVEFORM_LOCAL_EMPTY;
}

static int8_t waveform_local_find_frame(uint32_t frame)
{
    const int8_t index = waveform_local_find(waveform_local_align(frame));
    if(index < 0) { return -1; }
    const waveform_local_tile_t *const tile =
        &g_waveform_local.tiles[(uint8_t)index];
    if(frame < tile->start_frame + tile->loaded_frames)
        return index;
    return -1;
}

static void waveform_local_merge(waveform_local_point_t *out,
    const waveform_local_point_t *value, uint8_t *seen)
{
    if(*seen == 0U) { *out = *value; *seen = 1U; return; }
    if(value->min < out->min) { out->min = value->min; }
    if(value->max > out->max) { out->max = value->max; }
    out->last = value->last;
}

static uint8_t waveform_local_accumulate_range(uint32_t start_frame, uint32_t end_frame,
    uint32_t samples_per_point, waveform_local_point_t *out)
{
    uint8_t seen = 0U;
    uint32_t cursor = start_frame;
    while(cursor < end_frame)
    {
        const int8_t index = waveform_local_find_frame(cursor);
        if(index < 0) { return 0U; }
        const waveform_local_tile_t *const tile =
            &g_waveform_local.tiles[(uint8_t)index];
        uint32_t span_end = tile->start_frame + tile->loaded_frames;
        if(span_end > end_frame) { span_end = end_frame; }
        const uint32_t rel_start = cursor - tile->start_frame;
        const uint32_t rel_end = span_end - tile->start_frame;
        uint32_t block = 0U;
        const waveform_local_point_t *level = 0;
        if(samples_per_point >= WAVEFORM_LOCAL_LEVEL2_FRAMES)
        { block = WAVEFORM_LOCAL_LEVEL2_FRAMES;
          level = g_waveform_local_level2[(uint8_t)index]; }
        else if(samples_per_point >= WAVEFORM_LOCAL_LEVEL1_FRAMES)
        { block = WAVEFORM_LOCAL_LEVEL1_FRAMES;
          level = g_waveform_local_level1[(uint8_t)index]; }
        else if(samples_per_point >= WAVEFORM_LOCAL_LEVEL0_FRAMES)
        { block = WAVEFORM_LOCAL_LEVEL0_FRAMES;
          level = g_waveform_local_level0[(uint8_t)index]; }
        if(block != 0U)
        {
            for(uint32_t point = rel_start / block;
                    point < (rel_end + block - 1U) / block; ++point)
            {
                if((point + 1U) * block <= tile->loaded_frames
                        || tile->loaded_frames == tile->frame_count)
                    waveform_local_merge(out, &level[point], &seen);
                else
                {
                    const uint32_t raw_start = (point * block > rel_start)
                        ? point * block : rel_start;
                    const uint32_t raw_end = ((point + 1U) * block < rel_end)
                        ? (point + 1U) * block : rel_end;
                    for(uint32_t f = raw_start; f < raw_end; ++f)
                    {
                        const int16_t sample =
                            g_waveform_local_pcm[(uint8_t)index][f];
                        const waveform_local_point_t value =
                            { sample, sample, sample, sample };
                        waveform_local_merge(out, &value, &seen);
                    }
                }
            }
        }
        else
        {
            for(uint32_t f = rel_start; f < rel_end; ++f)
            {
                const int16_t sample =
                    g_waveform_local_pcm[(uint8_t)index][f];
                const waveform_local_point_t value =
                    { sample, sample, sample, sample };
                waveform_local_merge(out, &value, &seen);
            }
        }
        cursor = span_end;
    }
    return seen;
}

static uint16_t waveform_local_abs(int16_t value)
{
    return (value == -32768) ? 32768U
        : (uint16_t)((value < 0) ? -value : value);
}

static int16_t waveform_local_line_point(uint16_t point, uint16_t count,
    const waveform_local_point_t *value)
{
    int16_t sample = value->first;
    if(value->min < 0 && value->max > 0)
        sample = ((point & 1U) == 0U) ? value->max : value->min;
    else
        sample = (waveform_local_abs(value->max)
            >= waveform_local_abs(value->min)) ? value->max : value->min;
    if(point > 0U && point < (uint16_t)(count - 1U)
            && waveform_local_abs(sample) < waveform_local_abs(value->last))
        sample = value->last;
    return sample;
}
