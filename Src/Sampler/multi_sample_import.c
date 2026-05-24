#include "Sampler/multi_sample_import.h"

#include <stdio.h>
#include <string.h>

#include "Sampler/multi_sample_index.h"
#include "Sampler/sample_cache.h"
#include "Storage/looper_storage.h"
#include "Storage/memory_layout.h"
#include "Storage/multi_record_writer.h"
#include "Storage/sd_access_gate.h"
#include "Storage/wav_audio_codec.h"
#include "Storage/wav_parser.h"

#include "ff.h"

#define MULTI_SAMPLE_IMPORT_PATH_MAX (160U)
#define MULTI_SAMPLE_IMPORT_DIAG_MAX (80U)
#define MULTI_SAMPLE_AUTO_LOOP_WINDOW_MS (150U)
#define MULTI_SAMPLE_AUTO_LOOP_MIN_MS (150U)
#define MULTI_SAMPLE_AUTO_LOOP_BEGIN_TARGET_NUM (2U)
#define MULTI_SAMPLE_AUTO_LOOP_BEGIN_TARGET_DEN (5U)
#define MULTI_SAMPLE_AUTO_LOOP_END_TARGET_NUM (11U)
#define MULTI_SAMPLE_AUTO_LOOP_END_TARGET_DEN (20U)
#define MULTI_SAMPLE_AUTO_LOOP_MATCH_RADIUS (16U)
#define MULTI_SAMPLE_AUTO_LOOP_MAX_WINDOW_FRAMES \
    (((48000U * MULTI_SAMPLE_AUTO_LOOP_WINDOW_MS * 2U) / 1000U) \
     + (MULTI_SAMPLE_AUTO_LOOP_MATCH_RADIUS * 2U) + 4U)
#define MULTI_SAMPLE_AUTO_LOOP_MAX_CANDIDATES (64U)
#define MULTI_SAMPLE_AUTO_LOOP_IO_BYTES (4096U)

typedef struct
{
    multi_sample_index_source_sample_t sample;
    uint8_t root_fallback_alpha;
    uint8_t velocity_center_valid;
    uint8_t velocity_center;
} multi_sample_import_sample_t;

typedef struct
{
    uint8_t smpl_root_valid;
    uint8_t smpl_root;
    uint8_t smpl_loop_valid;
    uint32_t smpl_loop_begin;
    uint32_t smpl_loop_end;
    uint8_t auto_loop_valid;
    uint32_t auto_loop_begin;
    uint32_t auto_loop_end;
    uint8_t inst_root_valid;
    uint8_t inst_root;
    uint8_t inst_velocity_valid;
    uint8_t inst_vel_low;
    uint8_t inst_vel_high;
    uint8_t filename_valid;
    uint8_t filename_numeric_valid;
    uint8_t filename_root;
    uint8_t filename_vel_low;
    uint8_t filename_vel_high;
} multi_sample_import_metadata_t;

typedef struct
{
    int16_t left;
    int16_t right;
} multi_sample_auto_loop_frame_t;

typedef struct
{
    uint32_t start_frame;
    uint32_t frame_count;
    multi_sample_auto_loop_frame_t *frames;
} multi_sample_auto_loop_window_t;

typedef struct
{
    uint32_t frame;
    uint32_t local;
    uint32_t amp;
    uint32_t energy;
    int32_t slope_left;
    int32_t slope_right;
    int8_t direction;
} multi_sample_auto_loop_candidate_t;

SDRAM_MULTI_IMPORT static multi_sample_import_sample_t
    g_import_samples[MULTI_SAMPLE_POOL_MAX_SAMPLES];
SDRAM_MULTI_IMPORT static multi_sample_index_source_sample_t
    g_import_source_samples[MULTI_SAMPLE_POOL_MAX_SAMPLES];
SDRAM_MULTI_IMPORT static multi_sample_index_zone_t g_import_zones[MULTI_SAMPLE_POOL_MAX_ZONES];
SDRAM_MULTI_IMPORT static char g_import_paths[MULTI_SAMPLE_INDEX_STRING_MAX_BYTES];
SDRAM_MULTI_IMPORT static char g_import_work_path[MULTI_SAMPLE_IMPORT_PATH_MAX];
SDRAM_MULTI_IMPORT static char g_import_scan_dir[MULTI_SAMPLE_IMPORT_PATH_MAX];
SDRAM_MULTI_IMPORT static char g_import_index_path[MULTI_SAMPLE_IMPORT_PATH_MAX];
SDRAM_MULTI_IMPORT static char g_import_last_diag[MULTI_SAMPLE_IMPORT_DIAG_MAX];
SDRAM_MULTI_IMPORT static multi_sample_auto_loop_frame_t
    g_auto_loop_begin_frames[MULTI_SAMPLE_AUTO_LOOP_MAX_WINDOW_FRAMES];
SDRAM_MULTI_IMPORT static multi_sample_auto_loop_frame_t
    g_auto_loop_end_frames[MULTI_SAMPLE_AUTO_LOOP_MAX_WINDOW_FRAMES];
SDRAM_MULTI_IMPORT static multi_sample_auto_loop_candidate_t
    g_auto_loop_begin_candidates[MULTI_SAMPLE_AUTO_LOOP_MAX_CANDIDATES];
SDRAM_MULTI_IMPORT static multi_sample_auto_loop_candidate_t
    g_auto_loop_end_candidates[MULTI_SAMPLE_AUTO_LOOP_MAX_CANDIDATES];
SDRAM_MULTI_IMPORT static uint8_t g_auto_loop_io[MULTI_SAMPLE_AUTO_LOOP_IO_BYTES];
static CTRL_STATE multi_sample_import_result_t g_import_last_result;
static CTRL_STATE uint16_t g_import_sample_count;
static CTRL_STATE uint16_t g_import_zone_count;

static uint8_t multi_import_validate_wav_info(const wav_info_t *info);

static void multi_import_clear_diag(void)
{
    g_import_last_diag[0] = '\0';
}

static void multi_import_set_duplicate_diag(const multi_sample_index_source_sample_t *sample)
{
    if (sample == 0)
    {
        return;
    }

    (void)snprintf(g_import_last_diag,
                   sizeof(g_import_last_diag),
                   "DUP N%03u V%03u %s",
                   (unsigned)sample->root_note,
                   (unsigned)sample->vel_low,
                   (sample->relative_path != 0) ? sample->relative_path : "?");
}

static void multi_import_set_duplicate_pair_diag(
    const multi_sample_index_source_sample_t *a,
    const multi_sample_index_source_sample_t *b)
{
    const multi_sample_index_source_sample_t *const sample = (b != 0) ? b : a;
    if (sample == 0)
    {
        return;
    }

    (void)snprintf(g_import_last_diag,
                   sizeof(g_import_last_diag),
                   "DUP N%03u V%03u %s/%s",
                   (unsigned)sample->root_note,
                   (unsigned)sample->vel_low,
                   ((a != 0) && (a->relative_path != 0)) ? a->relative_path : "?",
                   ((b != 0) && (b->relative_path != 0)) ? b->relative_path : "?");
}

static uint8_t multi_import_velocity_sort_key(const multi_sample_import_sample_t *item)
{
    if (item == 0)
    {
        return 0U;
    }
    return (item->velocity_center_valid != 0U) ? item->velocity_center : item->sample.vel_low;
}

static uint8_t multi_import_is_wav(const char *name)
{
    if (name == 0)
    {
        return 0U;
    }

    const uint32_t len = (uint32_t)strlen(name);
    if (len < 5U)
    {
        return 0U;
    }

    const char *const ext = &name[len - 4U];
    return ((ext[0] == '.')
            && ((ext[1] == 'w') || (ext[1] == 'W'))
            && ((ext[2] == 'a') || (ext[2] == 'A'))
            && ((ext[3] == 'v') || (ext[3] == 'V')))
        ? 1U
        : 0U;
}

static uint8_t multi_import_join_path(char *out,
                                      uint32_t out_size,
                                      const char *dir,
                                      const char *name)
{
    if ((out == 0) || (dir == 0) || (name == 0) || (out_size == 0U))
    {
        return 0U;
    }

    const uint32_t dir_len = (uint32_t)strlen(dir);
    const char *const sep = ((dir_len != 0U) && (dir[dir_len - 1U] == '/')) ? "" : "/";
    const int written = snprintf(out, out_size, "%s%s%s", dir, sep, name);
    return ((written >= 0) && ((uint32_t)written < out_size)) ? 1U : 0U;
}

static uint8_t multi_import_extract_instrument_name(const char *instrument_dir,
                                                    char *out,
                                                    uint32_t out_size)
{
    if ((instrument_dir == 0) || (out == 0) || (out_size == 0U))
    {
        return 0U;
    }

    uint32_t len = (uint32_t)strlen(instrument_dir);
    while ((len != 0U) && ((instrument_dir[len - 1U] == '/') || (instrument_dir[len - 1U] == '\\')))
    {
        len--;
    }

    uint32_t start = len;
    while ((start != 0U) && (instrument_dir[start - 1U] != '/')
           && (instrument_dir[start - 1U] != '\\'))
    {
        start--;
    }

    const uint32_t name_len = len - start;
    if ((name_len == 0U) || (name_len >= out_size))
    {
        return 0U;
    }

    memcpy(out, &instrument_dir[start], name_len);
    out[name_len] = '\0';
    return 1U;
}

static uint8_t multi_import_parse_u8_token(const char *begin,
                                           const char *end,
                                           uint8_t *out_value)
{
    if ((begin == 0) || (end == 0) || (out_value == 0) || (begin >= end))
    {
        return 0U;
    }

    uint32_t value = 0U;
    for (const char *p = begin; p < end; ++p)
    {
        if ((*p < '0') || (*p > '9'))
        {
            return 0U;
        }
        value = (value * 10U) + (uint32_t)(*p - '0');
        if (value > 127U)
        {
            return 0U;
        }
    }

    *out_value = (uint8_t)value;
    return 1U;
}

static uint32_t multi_import_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}

static uint8_t multi_import_read_exact(FIL *fp, void *data, uint32_t size)
{
    UINT br = 0U;
    return ((f_read(fp, data, size, &br) == FR_OK) && (br == size)) ? 1U : 0U;
}

static uint8_t multi_import_chunk_id_eq(const uint8_t *id,
                                        char a,
                                        char b,
                                        char c,
                                        char d)
{
    return (((id[0] == (uint8_t)a) || (id[0] == (uint8_t)(a - ('a' - 'A'))))
            && ((id[1] == (uint8_t)b) || (id[1] == (uint8_t)(b - ('a' - 'A'))))
            && ((id[2] == (uint8_t)c) || (id[2] == (uint8_t)(c - ('a' - 'A'))))
            && ((id[3] == (uint8_t)d) || (id[3] == (uint8_t)(d - ('a' - 'A')))))
        ? 1U
        : 0U;
}

static uint8_t multi_import_parse_smpl_chunk(FIL *fp,
                                             uint32_t chunk_size,
                                             multi_sample_import_metadata_t *metadata)
{
    uint8_t buf[36];
    if ((fp == 0) || (metadata == 0) || (chunk_size < sizeof(buf)))
    {
        return 0U;
    }

    if (multi_import_read_exact(fp, buf, sizeof(buf)) == 0U)
    {
        return 0U;
    }

    const uint32_t root = multi_import_le32(&buf[12]);
    if (root <= 127U)
    {
        metadata->smpl_root_valid = 1U;
        metadata->smpl_root = (uint8_t)root;
    }

    const uint32_t loop_count = multi_import_le32(&buf[28]);
    const uint32_t loop_bytes = (chunk_size > sizeof(buf)) ? (chunk_size - (uint32_t)sizeof(buf)) : 0U;
    uint32_t loop_records = loop_bytes / 24U;
    if (loop_records > loop_count)
    {
        loop_records = loop_count;
    }
    for (uint32_t i = 0U; i < loop_records; ++i)
    {
        uint8_t loop[24];
        if (multi_import_read_exact(fp, loop, sizeof(loop)) == 0U)
        {
            return 0U;
        }

        const uint32_t loop_type = multi_import_le32(&loop[4]);
        const uint32_t loop_begin = multi_import_le32(&loop[8]);
        const uint32_t loop_end_inclusive = multi_import_le32(&loop[12]);
        if ((loop_type == 0U) && (loop_end_inclusive != UINT32_MAX)
            && (loop_end_inclusive >= loop_begin))
        {
            metadata->smpl_loop_valid = 1U;
            metadata->smpl_loop_begin = loop_begin;
            metadata->smpl_loop_end = loop_end_inclusive + 1U;
            break;
        }
    }

    return 1U;
}

static uint8_t multi_import_parse_inst_chunk(FIL *fp,
                                             uint32_t chunk_size,
                                             multi_sample_import_metadata_t *metadata)
{
    uint8_t buf[7];
    if ((fp == 0) || (metadata == 0) || (chunk_size < sizeof(buf)))
    {
        return 0U;
    }

    if (multi_import_read_exact(fp, buf, sizeof(buf)) == 0U)
    {
        return 0U;
    }

    if (buf[0] <= 127U)
    {
        metadata->inst_root_valid = 1U;
        metadata->inst_root = buf[0];
    }

    if ((buf[5] <= 127U) && (buf[6] <= 127U) && (buf[5] <= buf[6]))
    {
        metadata->inst_velocity_valid = 1U;
        metadata->inst_vel_low = buf[5];
        metadata->inst_vel_high = buf[6];
    }

    return 1U;
}

static void multi_import_read_wav_metadata(FIL *fp, multi_sample_import_metadata_t *metadata)
{
    if ((fp == 0) || (metadata == 0))
    {
        return;
    }

    if (f_lseek(fp, 12U) != FR_OK)
    {
        return;
    }

    while ((f_tell(fp) + 8U) <= f_size(fp))
    {
        uint8_t hdr[8];
        if (multi_import_read_exact(fp, hdr, sizeof(hdr)) == 0U)
        {
            return;
        }

        const uint32_t chunk_size = multi_import_le32(&hdr[4]);
        const FSIZE_t payload_start = f_tell(fp);
        const FSIZE_t next_chunk =
            payload_start + (FSIZE_t)chunk_size + (FSIZE_t)(chunk_size & 1U);
        if (next_chunk > f_size(fp))
        {
            return;
        }

        if (multi_import_chunk_id_eq(hdr, 's', 'm', 'p', 'l') != 0U)
        {
            (void)multi_import_parse_smpl_chunk(fp, chunk_size, metadata);
        }
        else if (multi_import_chunk_id_eq(hdr, 'i', 'n', 's', 't') != 0U)
        {
            (void)multi_import_parse_inst_chunk(fp, chunk_size, metadata);
        }

        if (f_lseek(fp, next_chunk) != FR_OK)
        {
            return;
        }
    }
}

static uint32_t multi_import_abs_i32(int32_t value)
{
    return (value < 0) ? (uint32_t)(-value) : (uint32_t)value;
}

static int16_t multi_import_float_to_i16(float value)
{
    if (value > 1.0f)
    {
        value = 1.0f;
    }
    else if (value < -1.0f)
    {
        value = -1.0f;
    }

    const int32_t scaled = (value >= 0.0f)
        ? (int32_t)((value * 32767.0f) + 0.5f)
        : (int32_t)((value * 32768.0f) - 0.5f);
    if (scaled > 32767)
    {
        return 32767;
    }
    if (scaled < -32768)
    {
        return -32768;
    }
    return (int16_t)scaled;
}

static int32_t multi_import_auto_loop_mono(const multi_sample_auto_loop_frame_t *frame)
{
    return ((int32_t)frame->left + (int32_t)frame->right) / 2;
}

static uint32_t multi_import_auto_loop_amp(const multi_sample_auto_loop_frame_t *frame)
{
    return multi_import_abs_i32(frame->left) + multi_import_abs_i32(frame->right);
}

static uint32_t multi_import_auto_loop_energy(const multi_sample_auto_loop_window_t *window,
                                              uint32_t local)
{
    if ((window == 0) || (window->frames == 0) || (window->frame_count == 0U))
    {
        return 0U;
    }

    const uint32_t radius = MULTI_SAMPLE_AUTO_LOOP_MATCH_RADIUS;
    const uint32_t begin = (local > radius) ? (local - radius) : 0U;
    uint32_t end = local + radius + 1U;
    if (end > window->frame_count)
    {
        end = window->frame_count;
    }

    uint64_t sum = 0U;
    uint32_t count = 0U;
    for (uint32_t i = begin; i < end; ++i)
    {
        sum += multi_import_auto_loop_amp(&window->frames[i]);
        count++;
    }

    return (count != 0U) ? (uint32_t)(sum / count) : 0U;
}

static uint8_t multi_import_auto_loop_make_window(const wav_info_t *info,
                                                  uint32_t center,
                                                  multi_sample_auto_loop_frame_t *frames,
                                                  multi_sample_auto_loop_window_t *out)
{
    if ((info == 0) || (frames == 0) || (out == 0) || (info->sample_rate == 0U)
        || (info->block_align == 0U))
    {
        return 0U;
    }

    const uint32_t total_frames = info->data_size / info->block_align;
    if (total_frames == 0U)
    {
        return 0U;
    }

    const uint32_t half_window =
        (uint32_t)(((uint64_t)info->sample_rate * MULTI_SAMPLE_AUTO_LOOP_WINDOW_MS) / 1000U);
    const uint32_t margin = MULTI_SAMPLE_AUTO_LOOP_MATCH_RADIUS + 1U;
    const uint32_t left = half_window + margin;
    const uint32_t right = half_window + margin + 1U;
    const uint32_t start = (center > left) ? (center - left) : 0U;
    uint32_t end = center + right;
    if ((end < center) || (end > total_frames))
    {
        end = total_frames;
    }
    if ((end <= start) || ((end - start) > MULTI_SAMPLE_AUTO_LOOP_MAX_WINDOW_FRAMES))
    {
        return 0U;
    }

    out->start_frame = start;
    out->frame_count = end - start;
    out->frames = frames;
    return 1U;
}

static uint8_t multi_import_auto_loop_mechanical_bounds(const wav_info_t *info,
                                                        uint32_t *out_begin,
                                                        uint32_t *out_end)
{
    if ((info == 0) || (out_begin == 0) || (out_end == 0) || (info->block_align == 0U))
    {
        return 0U;
    }

    const uint32_t total_frames = info->data_size / info->block_align;
    if (total_frames < 2U)
    {
        return 0U;
    }

    const uint32_t min_loop_frames =
        (uint32_t)(((uint64_t)info->sample_rate * MULTI_SAMPLE_AUTO_LOOP_MIN_MS) / 1000U);
    uint32_t begin =
        (uint32_t)(((uint64_t)total_frames * MULTI_SAMPLE_AUTO_LOOP_BEGIN_TARGET_NUM)
                   / MULTI_SAMPLE_AUTO_LOOP_BEGIN_TARGET_DEN);
    uint32_t end =
        (uint32_t)(((uint64_t)total_frames * MULTI_SAMPLE_AUTO_LOOP_END_TARGET_NUM)
                   / MULTI_SAMPLE_AUTO_LOOP_END_TARGET_DEN);

    if ((min_loop_frames != 0U) && (total_frames > min_loop_frames)
        && ((end <= begin) || ((end - begin) < min_loop_frames)))
    {
        end = begin + min_loop_frames;
        if (end > total_frames)
        {
            end = total_frames;
            begin = end - min_loop_frames;
        }
    }

    if (end > total_frames)
    {
        end = total_frames;
    }
    if (begin >= end)
    {
        begin = (end > 1U) ? (end - 1U) : 0U;
    }
    if (begin >= end)
    {
        begin = 0U;
        end = 1U;
    }

    *out_begin = begin;
    *out_end = end;
    return (begin < end) ? 1U : 0U;
}

static uint8_t multi_import_auto_loop_read_window(FIL *fp,
                                                  const wav_info_t *info,
                                                  multi_sample_auto_loop_window_t *window)
{
    if ((fp == 0) || (info == 0) || (window == 0) || (window->frames == 0)
        || (window->frame_count == 0U) || (info->block_align == 0U))
    {
        return 0U;
    }

    const uint64_t byte_offset =
        (uint64_t)info->data_offset + ((uint64_t)window->start_frame * info->block_align);
    const uint64_t byte_end = byte_offset + ((uint64_t)window->frame_count * info->block_align);
    const uint64_t data_end = (uint64_t)info->data_offset + info->data_size;
    if ((byte_end < byte_offset) || (byte_end > data_end) || (byte_end > (uint64_t)f_size(fp)))
    {
        return 0U;
    }
    if (f_lseek(fp, (FSIZE_t)byte_offset) != FR_OK)
    {
        return 0U;
    }

    uint32_t frames_done = 0U;
    while (frames_done < window->frame_count)
    {
        uint32_t request_frames = window->frame_count - frames_done;
        uint32_t request_bytes = request_frames * info->block_align;
        if (request_bytes > sizeof(g_auto_loop_io))
        {
            request_bytes = sizeof(g_auto_loop_io) - (sizeof(g_auto_loop_io) % info->block_align);
        }
        if (request_bytes == 0U)
        {
            return 0U;
        }

        UINT br = 0U;
        if ((f_read(fp, g_auto_loop_io, request_bytes, &br) != FR_OK) || (br == 0U))
        {
            return 0U;
        }

        const uint32_t valid_bytes = br - (br % info->block_align);
        if (valid_bytes == 0U)
        {
            return 0U;
        }

        uint32_t pos = 0U;
        while ((pos + info->block_align <= valid_bytes) && (frames_done < window->frame_count))
        {
            float left = 0.0f;
            float right = 0.0f;
            wav_audio_codec_decode_stereo_frame(&g_auto_loop_io[pos],
                                                info->channels,
                                                info->bits_per_sample,
                                                &left,
                                                &right);
            window->frames[frames_done].left = multi_import_float_to_i16(left);
            window->frames[frames_done].right = multi_import_float_to_i16(right);
            frames_done++;
            pos += info->block_align;
        }
    }

    return 1U;
}

static void multi_import_auto_loop_candidate_insert(
    multi_sample_auto_loop_candidate_t *candidates,
    uint32_t *count,
    const multi_sample_auto_loop_candidate_t *candidate)
{
    if ((candidates == 0) || (count == 0) || (candidate == 0))
    {
        return;
    }

    if (*count < MULTI_SAMPLE_AUTO_LOOP_MAX_CANDIDATES)
    {
        candidates[*count] = *candidate;
        (*count)++;
        return;
    }

    uint32_t worst = 0U;
    uint32_t worst_amp = candidates[0].amp;
    for (uint32_t i = 1U; i < MULTI_SAMPLE_AUTO_LOOP_MAX_CANDIDATES; ++i)
    {
        if (candidates[i].amp > worst_amp)
        {
            worst = i;
            worst_amp = candidates[i].amp;
        }
    }
    if (candidate->amp < worst_amp)
    {
        candidates[worst] = *candidate;
    }
}

static uint32_t multi_import_auto_loop_collect_candidates(
    const multi_sample_auto_loop_window_t *window,
    multi_sample_auto_loop_candidate_t *candidates)
{
    if ((window == 0) || (window->frames == 0) || (candidates == 0)
        || (window->frame_count < ((MULTI_SAMPLE_AUTO_LOOP_MATCH_RADIUS * 2U) + 3U)))
    {
        return 0U;
    }

    uint32_t count = 0U;
    for (uint32_t i = 1U; (i + 1U) < window->frame_count; ++i)
    {
        const int32_t prev = multi_import_auto_loop_mono(&window->frames[i - 1U]);
        const int32_t curr = multi_import_auto_loop_mono(&window->frames[i]);
        int8_t direction = 0;
        if ((prev <= 0) && (curr > 0))
        {
            direction = 1;
        }
        else if ((prev >= 0) && (curr < 0))
        {
            direction = -1;
        }
        else
        {
            continue;
        }

        const uint32_t amp = multi_import_auto_loop_amp(&window->frames[i]);
        const uint32_t energy = multi_import_auto_loop_energy(window, i);

        const multi_sample_auto_loop_candidate_t candidate = {
            .frame = window->start_frame + i,
            .local = i,
            .amp = amp,
            .energy = energy,
            .slope_left = (int32_t)window->frames[i + 1U].left
                          - (int32_t)window->frames[i - 1U].left,
            .slope_right = (int32_t)window->frames[i + 1U].right
                           - (int32_t)window->frames[i - 1U].right,
            .direction = direction,
        };
        multi_import_auto_loop_candidate_insert(candidates, &count, &candidate);
    }

    return count;
}

static uint32_t multi_import_auto_loop_match_score(
    const multi_sample_auto_loop_window_t *begin_window,
    const multi_sample_auto_loop_candidate_t *begin,
    const multi_sample_auto_loop_window_t *end_window,
    const multi_sample_auto_loop_candidate_t *end)
{
    if ((begin_window == 0) || (begin == 0) || (end_window == 0) || (end == 0)
        || (begin_window->frames == 0) || (end_window->frames == 0))
    {
        return UINT32_MAX;
    }

    const uint32_t radius = MULTI_SAMPLE_AUTO_LOOP_MATCH_RADIUS;
    if ((begin->local < radius) || ((begin->local + radius) >= begin_window->frame_count)
        || (end->local < radius) || ((end->local + radius) >= end_window->frame_count))
    {
        return UINT32_MAX;
    }

    uint64_t diff = 0U;
    uint32_t count = 0U;
    for (int32_t k = -(int32_t)radius; k <= (int32_t)radius; ++k)
    {
        const multi_sample_auto_loop_frame_t *const a =
            &begin_window->frames[(uint32_t)((int32_t)begin->local + k)];
        const multi_sample_auto_loop_frame_t *const b =
            &end_window->frames[(uint32_t)((int32_t)end->local + k)];
        diff += multi_import_abs_i32((int32_t)a->left - (int32_t)b->left);
        diff += multi_import_abs_i32((int32_t)a->right - (int32_t)b->right);
        count += 2U;
    }

    const uint32_t avg_diff = (count != 0U) ? (uint32_t)(diff / count) : UINT32_MAX;
    const uint32_t slope_diff =
        multi_import_abs_i32(begin->slope_left - end->slope_left)
        + multi_import_abs_i32(begin->slope_right - end->slope_right);
    return ((begin->amp + end->amp) / 2U)
           + (slope_diff / 8U)
           + (avg_diff * 4U);
}

static uint8_t multi_import_try_auto_loop(FIL *fp,
                                          const wav_info_t *info,
                                          uint32_t *out_begin,
                                          uint32_t *out_end)
{
    if ((fp == 0) || (info == 0) || (out_begin == 0) || (out_end == 0)
        || (multi_import_validate_wav_info(info) == 0U))
    {
        return 0U;
    }

    const uint32_t total_frames = info->data_size / info->block_align;
    const uint32_t min_loop_frames =
        (uint32_t)(((uint64_t)info->sample_rate * MULTI_SAMPLE_AUTO_LOOP_MIN_MS) / 1000U);
    if (total_frames < 2U)
    {
        return 0U;
    }

    multi_sample_auto_loop_window_t begin_window;
    multi_sample_auto_loop_window_t end_window;
    const uint32_t begin_center =
        (uint32_t)(((uint64_t)total_frames * MULTI_SAMPLE_AUTO_LOOP_BEGIN_TARGET_NUM)
                   / MULTI_SAMPLE_AUTO_LOOP_BEGIN_TARGET_DEN);
    const uint32_t end_center =
        (uint32_t)(((uint64_t)total_frames * MULTI_SAMPLE_AUTO_LOOP_END_TARGET_NUM)
                   / MULTI_SAMPLE_AUTO_LOOP_END_TARGET_DEN);
    if ((multi_import_auto_loop_make_window(info,
                                            begin_center,
                                            g_auto_loop_begin_frames,
                                            &begin_window) == 0U)
        || (multi_import_auto_loop_make_window(info,
                                               end_center,
                                               g_auto_loop_end_frames,
                                               &end_window) == 0U)
        || (multi_import_auto_loop_read_window(fp, info, &begin_window) == 0U)
        || (multi_import_auto_loop_read_window(fp, info, &end_window) == 0U))
    {
        return multi_import_auto_loop_mechanical_bounds(info, out_begin, out_end);
    }

    const uint32_t begin_count =
        multi_import_auto_loop_collect_candidates(&begin_window, g_auto_loop_begin_candidates);
    const uint32_t end_count =
        multi_import_auto_loop_collect_candidates(&end_window, g_auto_loop_end_candidates);
    if ((begin_count == 0U) || (end_count == 0U))
    {
        return multi_import_auto_loop_mechanical_bounds(info, out_begin, out_end);
    }

    uint32_t best_score = UINT32_MAX;
    uint32_t best_begin = 0U;
    uint32_t best_end = 0U;
    uint32_t best_any_score = UINT32_MAX;
    uint32_t best_any_begin = 0U;
    uint32_t best_any_end = 0U;
    for (uint32_t i = 0U; i < begin_count; ++i)
    {
        const multi_sample_auto_loop_candidate_t *const begin = &g_auto_loop_begin_candidates[i];
        for (uint32_t j = 0U; j < end_count; ++j)
        {
            const multi_sample_auto_loop_candidate_t *const end = &g_auto_loop_end_candidates[j];
            if (end->frame <= begin->frame)
            {
                continue;
            }

            uint32_t score =
                multi_import_auto_loop_match_score(&begin_window, begin, &end_window, end);
            if (score == UINT32_MAX)
            {
                continue;
            }
            if ((end->frame - begin->frame) < (min_loop_frames * 2U))
            {
                score += ((min_loop_frames * 2U) - (end->frame - begin->frame)) / 16U;
            }
            if (score < best_any_score)
            {
                best_any_score = score;
                best_any_begin = begin->frame;
                best_any_end = end->frame;
            }
            if ((begin->direction == end->direction) && (score < best_score))
            {
                best_score = score;
                best_begin = begin->frame;
                best_end = end->frame;
            }
        }
    }

    if ((best_score == UINT32_MAX) && (best_any_score != UINT32_MAX))
    {
        best_begin = best_any_begin;
        best_end = best_any_end;
    }

    if ((best_end <= best_begin) || (best_end > total_frames))
    {
        return multi_import_auto_loop_mechanical_bounds(info, out_begin, out_end);
    }

    *out_begin = best_begin;
    *out_end = best_end;
    return 1U;
}

static uint8_t multi_import_filename_metadata(const char *filename,
                                              uint8_t *out_root,
                                              uint8_t *out_vel_low,
                                              uint8_t *out_vel_high)
{
    if ((filename == 0) || (out_root == 0) || (out_vel_low == 0) || (out_vel_high == 0))
    {
        return 0U;
    }

    char stem[MULTI_SAMPLE_POOL_PATH_MAX];
    const uint32_t len = (uint32_t)strlen(filename);
    if ((len < 5U) || (len >= sizeof(stem)))
    {
        return 0U;
    }
    memcpy(stem, filename, len - 4U);
    stem[len - 4U] = '\0';

    char *velocity = strrchr(stem, '_');
    if (velocity != 0)
    {
        char *note = velocity;
        *velocity = '\0';
        velocity++;
        note = strrchr(stem, '_');
        if (note != 0)
        {
            *note = '\0';
            note++;

            const char *prev = strrchr(stem, '_');
            const char *prev_token = (prev != 0) ? (prev + 1) : stem;
            uint8_t ignored = 0U;
            uint8_t root = 0U;
            uint8_t vel = 0U;
            if ((multi_import_parse_u8_token(prev_token, note - 1, &ignored) == 0U)
                && (multi_import_parse_u8_token(note, velocity - 1, &root) != 0U)
                && (multi_import_parse_u8_token(velocity, &stem[len - 4U], &vel) != 0U)
                && (vel >= 1U))
            {
                *out_root = root;
                *out_vel_low = vel;
                *out_vel_high = vel;
                return 2U;
            }

            *(note - 1) = '_';
        }
        *(velocity - 1) = '_';
    }

    const uint8_t has_dash = (strchr(stem, '-') != 0) ? 1U : 0U;
    const uint8_t has_underscore = (strchr(stem, '_') != 0) ? 1U : 0U;
    if ((has_dash == has_underscore) || ((has_dash != 0U) && (has_underscore != 0U)))
    {
        return 0U;
    }

    const char delim = (has_dash != 0U) ? '-' : '_';
    char *third = strrchr(stem, delim);
    if (third == 0)
    {
        return 0U;
    }
    *third = '\0';
    char *second = strrchr(stem, delim);
    if (second == 0)
    {
        return 0U;
    }
    *second = '\0';
    char *first = strrchr(stem, delim);
    if (first == 0)
    {
        return 0U;
    }
    *first = '\0';

    uint8_t root = 0U;
    uint8_t vel_low = 0U;
    uint8_t vel_high = 0U;
    if ((multi_import_parse_u8_token(first + 1, second, &root) == 0U)
        || (multi_import_parse_u8_token(second + 1, third, &vel_low) == 0U)
        || (multi_import_parse_u8_token(third + 1, &stem[len - 4U], &vel_high) == 0U)
        || (vel_low > vel_high))
    {
        return 0U;
    }

    *out_root = root;
    *out_vel_low = vel_low;
    *out_vel_high = vel_high;
    return 1U;
}

static int multi_import_path_compare(const char *a, const char *b)
{
    return strcmp((a != 0) ? a : "", (b != 0) ? b : "");
}

static void multi_import_sort_samples_by_path(void)
{
    for (uint16_t i = 1U; i < g_import_sample_count; ++i)
    {
        multi_sample_import_sample_t key = g_import_samples[i];
        uint16_t j = i;
        while ((j != 0U)
               && (multi_import_path_compare(g_import_samples[j - 1U].sample.relative_path,
                                             key.sample.relative_path)
                   > 0))
        {
            g_import_samples[j] = g_import_samples[j - 1U];
            j--;
        }
        g_import_samples[j] = key;
    }
}

static void multi_import_sort_samples_by_layer_root(void)
{
    for (uint16_t i = 1U; i < g_import_sample_count; ++i)
    {
        multi_sample_import_sample_t key = g_import_samples[i];
        uint16_t j = i;
        while (j != 0U)
        {
            const multi_sample_index_source_sample_t *const prev =
                &g_import_samples[j - 1U].sample;
            const uint8_t move =
                ((prev->vel_low > key.sample.vel_low)
                 || ((prev->vel_low == key.sample.vel_low)
                     && (prev->vel_high > key.sample.vel_high))
                 || ((prev->vel_low == key.sample.vel_low)
                     && (prev->vel_high == key.sample.vel_high)
                     && (prev->root_note > key.sample.root_note)))
                    ? 1U
                    : 0U;
            if (move == 0U)
            {
                break;
            }
            g_import_samples[j] = g_import_samples[j - 1U];
            j--;
        }
        g_import_samples[j] = key;
    }
}

static void multi_import_sort_samples_by_root_velocity(void)
{
    for (uint16_t i = 1U; i < g_import_sample_count; ++i)
    {
        multi_sample_import_sample_t key = g_import_samples[i];
        uint16_t j = i;
        while (j != 0U)
        {
            const multi_sample_index_source_sample_t *const prev =
                &g_import_samples[j - 1U].sample;
            const uint8_t prev_vel = multi_import_velocity_sort_key(&g_import_samples[j - 1U]);
            const uint8_t key_vel = multi_import_velocity_sort_key(&key);
            const uint8_t move =
                ((prev->root_note > key.sample.root_note)
                 || ((prev->root_note == key.sample.root_note)
                     && (prev_vel > key_vel))
                 || ((prev->root_note == key.sample.root_note)
                     && (prev_vel == key_vel)
                     && (prev->vel_high > key.sample.vel_high)))
                    ? 1U
                    : 0U;
            if (move == 0U)
            {
                break;
            }
            g_import_samples[j] = g_import_samples[j - 1U];
            j--;
        }
        g_import_samples[j] = key;
    }
}

static multi_sample_import_result_t multi_import_expand_velocity_centers(void)
{
    multi_import_sort_samples_by_root_velocity();

    uint16_t root_start = 0U;
    while (root_start < g_import_sample_count)
    {
        uint16_t root_end = root_start + 1U;
        const uint8_t root = g_import_samples[root_start].sample.root_note;
        while ((root_end < g_import_sample_count)
               && (g_import_samples[root_end].sample.root_note == root))
        {
            root_end++;
        }

        uint16_t center_count = 0U;
        for (uint16_t i = root_start; i < root_end; ++i)
        {
            const multi_sample_import_sample_t *const item = &g_import_samples[i];
            if (item->velocity_center_valid != 0U)
            {
                center_count++;
                if ((i > root_start)
                    && (g_import_samples[i - 1U].velocity_center_valid != 0U)
                    && (g_import_samples[i - 1U].velocity_center == item->velocity_center))
                {
                    multi_import_set_duplicate_pair_diag(&g_import_samples[i - 1U].sample,
                                                         &item->sample);
                    return MULTI_SAMPLE_IMPORT_DUPLICATE_ZONE;
                }
            }
        }

        if (center_count > 1U)
        {
            for (uint16_t i = root_start; i < root_end; ++i)
            {
                multi_sample_import_sample_t *const item = &g_import_samples[i];
                multi_sample_index_source_sample_t *const sample = &item->sample;
                if (item->velocity_center_valid == 0U)
                {
                    continue;
                }

                uint8_t low = 1U;
                uint8_t high = 127U;
                const uint8_t center = item->velocity_center;
                if (i > root_start)
                {
                    const multi_sample_import_sample_t *const prev = &g_import_samples[i - 1U];
                    if ((prev->sample.root_note == root) && (prev->velocity_center_valid != 0U))
                    {
                        low = (uint8_t)((((uint16_t)prev->velocity_center
                                          + (uint16_t)center)
                                         / 2U)
                                        + 1U);
                    }
                }
                if ((i + 1U) < root_end)
                {
                    const multi_sample_import_sample_t *const next = &g_import_samples[i + 1U];
                    if ((next->sample.root_note == root) && (next->velocity_center_valid != 0U))
                    {
                        high = (uint8_t)(((uint16_t)center
                                          + (uint16_t)next->velocity_center)
                                         / 2U);
                    }
                }

                sample->vel_low = low;
                sample->vel_high = high;
            }
        }

        root_start = root_end;
    }

    return MULTI_SAMPLE_IMPORT_OK;
}

static uint8_t multi_import_validate_wav_info(const wav_info_t *info)
{
    if ((info == 0)
        || (info->sample_rate != 48000U)
        || !((info->channels == 1U) || (info->channels == 2U))
        || !((info->bits_per_sample == 16U) || (info->bits_per_sample == 24U)
             || (info->bits_per_sample == 32U))
        || (info->block_align == 0U)
        || (info->data_size < info->block_align))
    {
        return 0U;
    }

    return 1U;
}

static multi_sample_import_result_t multi_import_add_wav(const char *scan_dir,
                                                         const FILINFO *fno,
                                                         uint32_t *path_cursor)
{
    if ((scan_dir == 0) || (fno == 0) || (path_cursor == 0))
    {
        return MULTI_SAMPLE_IMPORT_INVALID_ARG;
    }

    if (g_import_sample_count >= MULTI_SAMPLE_POOL_MAX_SAMPLES)
    {
        return MULTI_SAMPLE_IMPORT_TOO_MANY_SAMPLES;
    }

    if (multi_import_join_path(g_import_work_path,
                               sizeof(g_import_work_path),
                               scan_dir,
                               fno->fname) == 0U)
    {
        return MULTI_SAMPLE_IMPORT_PATH_TOO_LONG;
    }

    FIL fp;
    if (f_open(&fp, g_import_work_path, FA_READ) != FR_OK)
    {
        return MULTI_SAMPLE_IMPORT_WAV_OPEN_FAIL;
    }

    wav_info_t info;
    memset(&info, 0, sizeof(info));
    multi_sample_import_metadata_t metadata;
    memset(&metadata, 0, sizeof(metadata));
    const uint8_t parsed = (wav_parser_parse_info(&fp, &info) != 0) ? 1U : 0U;
    if (parsed != 0U)
    {
        multi_import_read_wav_metadata(&fp, &metadata);
        const uint32_t parsed_total_frames =
            (info.block_align != 0U) ? (info.data_size / info.block_align) : 0U;
        const uint8_t smpl_loop_valid =
            ((metadata.smpl_loop_valid != 0U)
             && (metadata.smpl_loop_end > metadata.smpl_loop_begin)
             && (metadata.smpl_loop_end <= parsed_total_frames))
                ? 1U
                : 0U;
        if ((multi_import_validate_wav_info(&info) != 0U)
            && (smpl_loop_valid == 0U)
            && (multi_import_try_auto_loop(&fp,
                                           &info,
                                           &metadata.auto_loop_begin,
                                           &metadata.auto_loop_end)
                != 0U))
        {
            metadata.auto_loop_valid = 1U;
        }
    }
    (void)f_close(&fp);
    if (parsed == 0U)
    {
        return MULTI_SAMPLE_IMPORT_WAV_PARSE_FAIL;
    }
    if (multi_import_validate_wav_info(&info) == 0U)
    {
        return MULTI_SAMPLE_IMPORT_WAV_UNSUPPORTED;
    }

    char relative_path[MULTI_SAMPLE_POOL_PATH_MAX];
    const uint32_t name_len = (uint32_t)strlen(fno->fname);
    if ((name_len == 0U) || (name_len >= sizeof(relative_path)))
    {
        return MULTI_SAMPLE_IMPORT_PATH_TOO_LONG;
    }
    memcpy(relative_path, fno->fname, name_len + 1U);

    const uint32_t path_len = (uint32_t)strlen(relative_path);
    if ((*path_cursor + path_len + 1U) > MULTI_SAMPLE_INDEX_STRING_MAX_BYTES)
    {
        return MULTI_SAMPLE_IMPORT_PATH_TOO_LONG;
    }

    multi_sample_import_sample_t *const item = &g_import_samples[g_import_sample_count];
    memset(item, 0, sizeof(*item));
    memcpy(&g_import_paths[*path_cursor], relative_path, path_len + 1U);
    item->sample.relative_path = &g_import_paths[*path_cursor];
    *path_cursor += path_len + 1U;

    item->sample.total_frames = info.data_size / info.block_align;
    item->sample.sample_rate = info.sample_rate;
    item->sample.channels = info.channels;
    item->sample.bits_per_sample = info.bits_per_sample;
    item->sample.data_offset = info.data_offset;
    item->sample.data_size = info.data_size - (info.data_size % info.block_align);
    item->sample.wav_size = (uint32_t)fno->fsize;
    item->sample.wav_mtime = ((uint32_t)fno->fdate << 16) | (uint32_t)fno->ftime;
    if ((metadata.smpl_loop_valid != 0U)
        && (metadata.smpl_loop_end > metadata.smpl_loop_begin)
        && (metadata.smpl_loop_end <= item->sample.total_frames))
    {
        item->sample.has_loop = 1U;
        item->sample.loop_begin = metadata.smpl_loop_begin;
        item->sample.loop_end = metadata.smpl_loop_end;
    }
    else if ((metadata.auto_loop_valid != 0U)
             && (metadata.auto_loop_end > metadata.auto_loop_begin)
             && (metadata.auto_loop_end <= item->sample.total_frames))
    {
        item->sample.has_loop = 1U;
        item->sample.loop_begin = metadata.auto_loop_begin;
        item->sample.loop_end = metadata.auto_loop_end;
        item->sample.metadata_flags |= MULTI_SAMPLE_INDEX_META_LOOP_AUTO;
    }

    const uint8_t filename_metadata = multi_import_filename_metadata(fno->fname,
                                                                     &metadata.filename_root,
                                                                     &metadata.filename_vel_low,
                                                                     &metadata.filename_vel_high);
    if (filename_metadata != 0U)
    {
        metadata.filename_valid = 1U;
        metadata.filename_numeric_valid = (filename_metadata == 2U) ? 1U : 0U;
    }

    if (metadata.filename_numeric_valid != 0U)
    {
        item->sample.root_note = metadata.filename_root;
        item->sample.metadata_flags |= MULTI_SAMPLE_INDEX_META_ROOT_FILENAME;
    }
    else if (metadata.smpl_root_valid != 0U)
    {
        item->sample.root_note = metadata.smpl_root;
        item->sample.metadata_flags |= MULTI_SAMPLE_INDEX_META_ROOT_SMPL;
    }
    else if (metadata.inst_root_valid != 0U)
    {
        item->sample.root_note = metadata.inst_root;
        item->sample.metadata_flags |= MULTI_SAMPLE_INDEX_META_ROOT_INST;
    }
    else if (metadata.filename_valid != 0U)
    {
        item->sample.root_note = metadata.filename_root;
        item->sample.metadata_flags |= MULTI_SAMPLE_INDEX_META_ROOT_FILENAME;
    }
    else
    {
        item->sample.root_note = 36U;
        item->root_fallback_alpha = 1U;
        item->sample.metadata_flags |= MULTI_SAMPLE_INDEX_META_ROOT_ALPHA;
    }

    if (metadata.filename_numeric_valid != 0U)
    {
        item->sample.vel_low = metadata.filename_vel_low;
        item->sample.vel_high = metadata.filename_vel_high;
        item->velocity_center_valid = 1U;
        item->velocity_center = metadata.filename_vel_low;
        item->sample.metadata_flags |= MULTI_SAMPLE_INDEX_META_VEL_FILENAME;
    }
    else if (metadata.inst_velocity_valid != 0U)
    {
        item->sample.vel_low = metadata.inst_vel_low;
        item->sample.vel_high = metadata.inst_vel_high;
        item->sample.metadata_flags |= MULTI_SAMPLE_INDEX_META_VEL_INST;
    }
    else if (metadata.filename_valid != 0U)
    {
        item->sample.vel_low = metadata.filename_vel_low;
        item->sample.vel_high = metadata.filename_vel_high;
        item->sample.metadata_flags |= MULTI_SAMPLE_INDEX_META_VEL_FILENAME;
    }
    else
    {
        item->sample.vel_low = 1U;
        item->sample.vel_high = 127U;
        item->sample.metadata_flags |= MULTI_SAMPLE_INDEX_META_VEL_ALPHA;
    }
    g_import_sample_count++;
    return MULTI_SAMPLE_IMPORT_OK;
}

static multi_sample_import_result_t multi_import_generate_zones(void)
{
    if (g_import_sample_count == 0U)
    {
        return MULTI_SAMPLE_IMPORT_NO_WAV;
    }

    multi_import_sort_samples_by_path();
    uint8_t fallback_root = 36U;
    for (uint16_t i = 0U; i < g_import_sample_count; ++i)
    {
        if (g_import_samples[i].root_fallback_alpha != 0U)
        {
            g_import_samples[i].sample.root_note = fallback_root;
            if (fallback_root < 127U)
            {
                fallback_root++;
            }
        }
    }

    multi_sample_import_result_t result = multi_import_expand_velocity_centers();
    if (result != MULTI_SAMPLE_IMPORT_OK)
    {
        return result;
    }

    multi_import_sort_samples_by_layer_root();
    g_import_zone_count = 0U;

    uint16_t group_start = 0U;
    while (group_start < g_import_sample_count)
    {
        uint16_t group_end = group_start + 1U;
        const uint8_t vel_low = g_import_samples[group_start].sample.vel_low;
        const uint8_t vel_high = g_import_samples[group_start].sample.vel_high;
        while ((group_end < g_import_sample_count)
               && (g_import_samples[group_end].sample.vel_low == vel_low)
               && (g_import_samples[group_end].sample.vel_high == vel_high))
        {
            group_end++;
        }

        for (uint16_t i = group_start; i < group_end; ++i)
        {
            if ((i > group_start)
                && (g_import_samples[i - 1U].sample.root_note
                    == g_import_samples[i].sample.root_note))
            {
                multi_import_set_duplicate_pair_diag(&g_import_samples[i - 1U].sample,
                                                     &g_import_samples[i].sample);
                return MULTI_SAMPLE_IMPORT_DUPLICATE_ZONE;
            }

            if (g_import_zone_count >= MULTI_SAMPLE_POOL_MAX_ZONES)
            {
                return MULTI_SAMPLE_IMPORT_ZONE_LIMIT;
            }

            const uint8_t root = g_import_samples[i].sample.root_note;
            const uint8_t note_low =
                (i == group_start)
                    ? 0U
                    : (uint8_t)((((uint16_t)g_import_samples[i - 1U].sample.root_note
                                  + (uint16_t)root)
                                 / 2U)
                                + 1U);
            const uint8_t note_high =
                ((i + 1U) >= group_end)
                    ? 127U
                    : (uint8_t)(((uint16_t)root
                                 + (uint16_t)g_import_samples[i + 1U].sample.root_note)
                                / 2U);

            multi_sample_index_zone_t *const zone = &g_import_zones[g_import_zone_count++];
            zone->note_low = note_low;
            zone->note_high = note_high;
            zone->vel_low = vel_low;
            zone->vel_high = vel_high;
            zone->root_note = root;
            zone->multi_sample_id = i;
        }

        group_start = group_end;
    }

    for (uint16_t a = 0U; a < g_import_zone_count; ++a)
    {
        for (uint16_t b = (uint16_t)(a + 1U); b < g_import_zone_count; ++b)
        {
            const multi_sample_index_zone_t *const za = &g_import_zones[a];
            const multi_sample_index_zone_t *const zb = &g_import_zones[b];
            const uint8_t note_overlap =
                ((za->note_low <= zb->note_high) && (zb->note_low <= za->note_high)) ? 1U : 0U;
            const uint8_t vel_overlap =
                ((za->vel_low <= zb->vel_high) && (zb->vel_low <= za->vel_high)) ? 1U : 0U;
            if ((note_overlap != 0U) && (vel_overlap != 0U)
                && !((za->vel_low == zb->vel_low) && (za->vel_high == zb->vel_high)))
            {
                multi_import_set_duplicate_diag(&g_import_samples[zb->multi_sample_id].sample);
                return MULTI_SAMPLE_IMPORT_DUPLICATE_ZONE;
            }
        }
    }

    return MULTI_SAMPLE_IMPORT_OK;
}

static multi_sample_import_result_t multi_import_index_write_result(
    multi_sample_index_result_t result)
{
    return (result == MULTI_SAMPLE_INDEX_OK)
        ? MULTI_SAMPLE_IMPORT_OK
        : MULTI_SAMPLE_IMPORT_INDEX_WRITE_FAIL;
}

static void multi_import_notify_progress(multi_sample_import_progress_cb_t progress_cb,
                                         void *progress_user,
                                         uint16_t done,
                                         uint16_t total)
{
    if (progress_cb != 0)
    {
        progress_cb(done, total, progress_user);
    }
}

static multi_sample_import_result_t multi_import_count_direct_wavs(const char *scan_dir,
                                                                   uint16_t *out_count)
{
    DIR dir;
    FRESULT fr = f_opendir(&dir, scan_dir);
    if (fr != FR_OK)
    {
        return MULTI_SAMPLE_IMPORT_OPEN_DIR_FAIL;
    }

    uint16_t count = 0U;
    while (1)
    {
        FILINFO fno;
        memset(&fno, 0, sizeof(fno));
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK)
        {
            (void)f_closedir(&dir);
            return MULTI_SAMPLE_IMPORT_OPEN_DIR_FAIL;
        }
        if (fno.fname[0] == '\0')
        {
            break;
        }
        if (((fno.fattrib & AM_DIR) == 0U) && (multi_import_is_wav(fno.fname) != 0U))
        {
            if (count < UINT16_MAX)
            {
                count++;
            }
        }
    }

    (void)f_closedir(&dir);
    if (out_count != 0)
    {
        *out_count = count;
    }
    return MULTI_SAMPLE_IMPORT_OK;
}

multi_sample_import_result_t multi_sample_import_folder_with_progress(
    const char *instrument_dir,
    multi_sample_import_progress_cb_t progress_cb,
    void *progress_user)
{
    g_import_last_result = MULTI_SAMPLE_IMPORT_OK;
    multi_import_clear_diag();
    g_import_sample_count = 0U;
    g_import_zone_count = 0U;
    memset(g_import_paths, 0, sizeof(g_import_paths));
    memset(g_import_samples, 0, sizeof(g_import_samples));
    memset(g_import_zones, 0, sizeof(g_import_zones));

    if ((instrument_dir == 0) || (instrument_dir[0] == '\0'))
    {
        g_import_last_result = MULTI_SAMPLE_IMPORT_INVALID_ARG;
        return g_import_last_result;
    }

    if ((multi_record_writer_any_active() != 0U)
        || (looper_storage_raw_export_is_active() != 0U)
        || (sample_cache_has_pending_sd_work() != 0U))
    {
        g_import_last_result = MULTI_SAMPLE_IMPORT_SD_BUSY;
        return g_import_last_result;
    }

    char instrument_name[MULTI_SAMPLE_POOL_NAME_MAX];
    if ((multi_import_extract_instrument_name(instrument_dir,
                                              instrument_name,
                                              sizeof(instrument_name)) == 0U)
        || (strlen(instrument_dir) >= sizeof(g_import_scan_dir)))
    {
        g_import_last_result = MULTI_SAMPLE_IMPORT_PATH_TOO_LONG;
        return g_import_last_result;
    }
    memcpy(g_import_scan_dir, instrument_dir, strlen(instrument_dir) + 1U);

    const int index_path_written = snprintf(g_import_index_path,
                                            sizeof(g_import_index_path),
                                            "%s/%s.brickmulti",
                                            instrument_dir,
                                            instrument_name);
    if ((index_path_written < 0) || ((uint32_t)index_path_written >= sizeof(g_import_index_path)))
    {
        g_import_last_result = MULTI_SAMPLE_IMPORT_PATH_TOO_LONG;
        return g_import_last_result;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
    {
        g_import_last_result = MULTI_SAMPLE_IMPORT_SD_BUSY;
        return g_import_last_result;
    }

    if (sd_access_fs_mount_if_needed() == 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
        g_import_last_result = MULTI_SAMPLE_IMPORT_SD_MOUNT_FAIL;
        return g_import_last_result;
    }

    uint16_t direct_wav_count = 0U;
    multi_sample_import_result_t count_result =
        multi_import_count_direct_wavs(g_import_scan_dir, &direct_wav_count);
    if (count_result != MULTI_SAMPLE_IMPORT_OK)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
        g_import_last_result = count_result;
        return g_import_last_result;
    }

    const uint16_t progress_total = (uint16_t)(direct_wav_count + 2U);
    uint16_t progress_done = 0U;
    multi_import_notify_progress(progress_cb, progress_user, progress_done, progress_total);

    DIR dir;
    FRESULT fr = f_opendir(&dir, g_import_scan_dir);
    if (fr != FR_OK)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
        g_import_last_result = MULTI_SAMPLE_IMPORT_OPEN_DIR_FAIL;
        return g_import_last_result;
    }

    uint32_t path_cursor = 0U;
    multi_sample_import_result_t result = MULTI_SAMPLE_IMPORT_OK;
    while (result == MULTI_SAMPLE_IMPORT_OK)
    {
        FILINFO fno;
        memset(&fno, 0, sizeof(fno));
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK)
        {
            result = MULTI_SAMPLE_IMPORT_OPEN_DIR_FAIL;
            break;
        }
        if (fno.fname[0] == '\0')
        {
            break;
        }
        if (((fno.fattrib & AM_DIR) != 0U) || (multi_import_is_wav(fno.fname) == 0U))
        {
            continue;
        }

        result = multi_import_add_wav(g_import_scan_dir, &fno, &path_cursor);
        if (result == MULTI_SAMPLE_IMPORT_OK)
        {
            progress_done++;
            multi_import_notify_progress(progress_cb, progress_user, progress_done, progress_total);
        }
    }

    (void)f_closedir(&dir);
    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);

    if (result == MULTI_SAMPLE_IMPORT_OK)
    {
        result = multi_import_generate_zones();
        if (result == MULTI_SAMPLE_IMPORT_OK)
        {
            progress_done = (uint16_t)(direct_wav_count + 1U);
            multi_import_notify_progress(progress_cb, progress_user, progress_done, progress_total);
        }
    }

    if (result == MULTI_SAMPLE_IMPORT_OK)
    {
        multi_sample_index_source_t src;
        memset(&src, 0, sizeof(src));
        src.instrument_name = instrument_name;
        src.sample_count = g_import_sample_count;
        src.zone_count = g_import_zone_count;
        src.zones = g_import_zones;

        for (uint16_t i = 0U; i < g_import_sample_count; ++i)
        {
            g_import_source_samples[i] = g_import_samples[i].sample;
        }
        src.samples = g_import_source_samples;

        result = multi_import_index_write_result(multi_sample_index_write(g_import_index_path,
                                                                          &src));
        if (result == MULTI_SAMPLE_IMPORT_OK)
        {
            multi_import_notify_progress(progress_cb, progress_user, progress_total, progress_total);
        }
    }

    g_import_last_result = result;
    return result;
}

multi_sample_import_result_t multi_sample_import_folder(const char *instrument_dir)
{
    return multi_sample_import_folder_with_progress(instrument_dir, 0, 0);
}

multi_sample_import_result_t multi_sample_import_get_last_result(void)
{
    return g_import_last_result;
}

const char *multi_sample_import_get_last_diagnostic(void)
{
    return g_import_last_diag;
}

uint16_t multi_sample_import_get_last_sample_count(void)
{
    return g_import_sample_count;
}

uint16_t multi_sample_import_get_last_zone_count(void)
{
    return g_import_zone_count;
}
