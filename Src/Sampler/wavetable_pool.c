#include "Sampler/wavetable_pool.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_page_cache_port.h"
#include "Audio/audio_shared_memory.h"
#include "Audio/audio_wavetable_registry.h"
#include "Platform/cache_maintenance.h"
#include "IPC/control_audio_publication.h"
#include "IPC/audio_wave_table_projection.h"
#include "Core/live_clock.h"
#include "Storage/sd_access_gate.h"
#include "SD/sd_scheduler_runtime.h"
#include "Platform/memory_layout.h"
#include "Storage/wav_audio_codec.h"
#include "Storage/wav_parser.h"
#include "stm32h7xx.h"

#define WAVETABLE_POOL_IO_BYTES (8192U)
#define WAVETABLE_POOL_CACHE_DIR "0:/WAVETABLES/.CACHE"
#define WAVETABLE_SOURCE_2048_SAMPLE_COUNT (2048U)
#define WAVETABLE_MIPMAP_INITIAL_CYCLE_MAGNITUDE (10U)
#define WAVETABLE_MIPMAP_MIN_CYCLE_MAGNITUDE (3U)

/* Preparation-quality controls.  Ratios are relative to the positive-spectrum
 * Nyquist bin of each generated cycle. */
#define WAVETABLE_PREP_BASE_PASS_RATIO       (0.875f)
#define WAVETABLE_PREP_BASE_STOP_RATIO       (0.96875f)
#define WAVETABLE_PREP_MIP_PASS_RATIO        (0.80f)
#define WAVETABLE_PREP_MIP_STOP_RATIO        (0.90f)
#define WAVETABLE_PREP_RESAMPLE_PASS_RATIO   (0.95f)
#define WAVETABLE_PREP_RESAMPLE_STOP_RATIO   (1.0f)
#define WAVETABLE_PREP_PHASE_LIMIT_RATIO     (0.95)
#define WAVETABLE_DEFAULT_SOURCE_GEOMETRY    WAVETABLE_SOURCE_GEOMETRY_2048

typedef struct
{
    wavetable_slot_t slots[WAVETABLE_POOL_MAX_SLOTS];
    wavetable_result_t last_result;
    uint32_t generation_counter;
} wavetable_pool_state_t;

STORAGE_STATE_SDRAM static wavetable_pool_state_t g_wavetable_pool;
STORAGE_STATE_SDRAM static wavetable_slot_t g_wavetable_candidate;
STORAGE_STATE_SDRAM static wavetable_slot_t g_wavetable_old_commit_snapshot;
STORAGE_STATE_SDRAM static FIL g_wavetable_transaction_files[2];
STORAGE_STATE_SDRAM static char
    g_wavetable_transaction_paths[2][WAVETABLE_POOL_PATH_MAX];
AUDIO_WARM ALIGN32 static uint8_t g_wavetable_pool_io[WAVETABLE_POOL_IO_BYTES];
STORAGE_STATE_SDRAM ALIGN32 static float
    g_wavetable_fft_real[WAVETABLE_SOURCE_2048_SAMPLE_COUNT];
STORAGE_STATE_SDRAM ALIGN32 static float
    g_wavetable_fft_imag[WAVETABLE_SOURCE_2048_SAMPLE_COUNT];
STORAGE_STATE_SDRAM ALIGN32 static float
    g_wavetable_fft_work_real[WAVETABLE_FRAME_SAMPLE_COUNT];
STORAGE_STATE_SDRAM ALIGN32 static float
    g_wavetable_fft_work_imag[WAVETABLE_FRAME_SAMPLE_COUNT];
static const uint8_t g_wavetable_mipmap_upstream_commit_sha1[20] = {
    0x0dU, 0x9cU, 0xbfU, 0x04U, 0x40U, 0xf0U, 0x55U, 0x5eU, 0x25U, 0x44U,
    0xccU, 0x1eU, 0xb0U, 0x19U, 0xb3U, 0x16U, 0x75U, 0x63U, 0x70U, 0x08U
};

typedef enum
{
    WAVETABLE_LOAD_IDLE = 0,
    WAVETABLE_LOAD_MOUNT,
    WAVETABLE_LOAD_STAT,
    WAVETABLE_LOAD_OPEN,
    WAVETABLE_LOAD_CRC_SEEK,
    WAVETABLE_LOAD_CRC_READ,
    WAVETABLE_LOAD_PARSE,
    WAVETABLE_LOAD_ALLOCATE,
    WAVETABLE_LOAD_DATA_SEEK,
    WAVETABLE_LOAD_DATA_READ,
    WAVETABLE_LOAD_DECODE,
    WAVETABLE_LOAD_MIP_INIT,
    WAVETABLE_LOAD_MIP_FORWARD_PREP,
    WAVETABLE_LOAD_MIP_FORWARD_FFT,
    WAVETABLE_LOAD_MIP_BAND,
    WAVETABLE_LOAD_BASE_CRC,
    WAVETABLE_LOAD_PAYLOAD_CRC,
    WAVETABLE_LOAD_CACHE_DIR,
    WAVETABLE_LOAD_CACHE_SUBDIR,
    WAVETABLE_LOAD_CACHE_CLEAN,
    WAVETABLE_LOAD_CACHE_OPEN,
    WAVETABLE_LOAD_CACHE_HEADER,
    WAVETABLE_LOAD_CACHE_BAND,
    WAVETABLE_LOAD_CACHE_PAYLOAD,
    WAVETABLE_LOAD_CACHE_SYNC,
    WAVETABLE_LOAD_CACHE_CLOSE,
    WAVETABLE_LOAD_CACHE_FINAL_CLEAN,
    WAVETABLE_LOAD_CACHE_RENAME,
    WAVETABLE_LOAD_SOURCE_CLOSE,
    WAVETABLE_LOAD_PREVIEW_INIT,
    WAVETABLE_LOAD_PREVIEW_FRAME,
    WAVETABLE_LOAD_WAIT_RETIRE,
    WAVETABLE_LOAD_PUBLISH,
    WAVETABLE_LOAD_CLEANUP,
    WAVETABLE_LOAD_DONE
} wavetable_load_state_t;

typedef struct
{
    wavetable_load_state_t state;
    FIL source;
    FIL cache;
    FILINFO source_info;
    wav_info_t wav_info;
    char path[WAVETABLE_POOL_PATH_MAX];
    char cache_path[WAVETABLE_POOL_PATH_MAX];
    char temp_path[WAVETABLE_POOL_PATH_MAX];
    uint32_t media_epoch;
    uint32_t source_crc;
    uint32_t base_crc;
    uint32_t payload_crc;
    uint32_t io_offset;
    uint32_t buffered_samples;
    uint32_t converted_samples;
    uint32_t source_cycle_sample_count;
    uint32_t source_cycle_offset;
    uint32_t cycle;
    uint16_t band;
    uint16_t wavetable_slot;
    uint16_t global_slot;
    wavetable_result_t result;
    uint8_t source_open;
    uint8_t cache_open;
    uint8_t cleanup_phase;
    wavetable_source_geometry_t source_geometry;
} wavetable_load_job_t;

STORAGE_STATE_SDRAM static wavetable_load_job_t g_wavetable_load_job;
static CTRL_STATE uint32_t
    g_wavetable_retire_fence[WAVETABLE_POOL_MAX_SLOTS];

static uint16_t wavetable_pool_mipmap_band_count(void);
static uint32_t wavetable_pool_mipmap_samples_per_table_cycle(void);
static float wavetable_pool_finite_float(float value);

static uint32_t wavetable_pool_mipmap_cycle_stride(uint32_t cycle_sample_count)
{
    return (cycle_sample_count == 8U) ? 16U : cycle_sample_count;
}

static uint32_t wavetable_pool_source_cycle_sample_count(
    wavetable_source_geometry_t source_geometry)
{
    return (source_geometry == WAVETABLE_SOURCE_GEOMETRY_2048)
        ? WAVETABLE_SOURCE_2048_SAMPLE_COUNT
        : WAVETABLE_FRAME_SAMPLE_COUNT;
}

static void wavetable_pool_source_2048_store(uint32_t index, float value)
{
    if (index < WAVETABLE_SOURCE_2048_SAMPLE_COUNT)
        g_wavetable_fft_real[index] = value;
}

static float wavetable_pool_spectral_gain(uint32_t bin,
                                          uint32_t nyquist,
                                          float pass_ratio,
                                          float stop_ratio)
{
    const float position = (nyquist != 0U)
        ? ((float)bin / (float)nyquist) : 1.0f;
    if (position <= pass_ratio) return 1.0f;
    if ((position >= stop_ratio) || (stop_ratio <= pass_ratio)) return 0.0f;
    const float phase = (position - pass_ratio) / (stop_ratio - pass_ratio);
    return 0.5f * (1.0f + cosf(3.14159265358979323846f * phase));
}

static void wavetable_pool_set_last(wavetable_result_t result)
{
    g_wavetable_pool.last_result = result;
}

static uint32_t wavetable_pool_next_generation(void)
{
    g_wavetable_pool.generation_counter++;
    if (g_wavetable_pool.generation_counter == 0U)
    {
        g_wavetable_pool.generation_counter = 1U;
    }
    return g_wavetable_pool.generation_counter;
}

static uint32_t wavetable_pool_crc32_update(uint32_t crc,
                                            const uint8_t *data,
                                            uint32_t size)
{
    crc = ~crc;
    for (uint32_t i = 0U; i < size; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

static uint32_t wavetable_pool_crc32_memory(const void *data, uint32_t size)
{
    return (data != 0)
        ? wavetable_pool_crc32_update(0U, (const uint8_t *)data, size)
        : 0U;
}

static void wavetable_pool_copy_float_exact(float *dst,
                                            const float *src,
                                            uint32_t sample_count)
{
    volatile float *const dst_float = (volatile float *)dst;
    const volatile float *const src_float = (const volatile float *)src;
    for (uint32_t i = 0U; i < sample_count; ++i)
    {
        dst_float[i] = src_float[i];
    }
}

static uint8_t wavetable_pool_copy_path(char *dst, uint32_t dst_size, const char *src)
{
    if ((dst == 0) || (dst_size == 0U) || (src == 0) || (src[0] == '\0'))
    {
        return 0U;
    }

    uint32_t i = 0U;
    while ((i + 1U) < dst_size)
    {
        dst[i] = src[i];
        if (src[i] == '\0')
        {
            return 1U;
        }
        i++;
    }

    dst[i] = '\0';
    return (src[i] == '\0') ? 1U : 0U;
}

static uint16_t wavetable_pool_read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t wavetable_pool_read_u32(const uint8_t *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static void wavetable_pool_write_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void wavetable_pool_write_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)((value >> 8) & 0xFFU);
    p[2] = (uint8_t)((value >> 16) & 0xFFU);
    p[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void wavetable_pool_write_i32(uint8_t *p, int32_t value)
{
    wavetable_pool_write_u32(p, (uint32_t)value);
}

static uint16_t wavetable_pool_preview_abs_i16(int16_t value)
{
    return (value < 0) ? (uint16_t)(-value) : (uint16_t)value;
}

static int16_t wavetable_pool_preview_float_to_i16(float value)
{
    if (value >= 1.0f) return INT16_MAX;
    if (value <= -1.0f) return INT16_MIN;
    return (int16_t)lrintf(value * 32767.0f);
}

static float wavetable_pool_finite_float(float value)
{
    return isfinite(value) ? value : 0.0f;
}

static void wavetable_pool_preview_clear(wavetable_preview_t *preview)
{
    if (preview == 0)
    {
        return;
    }
    memset(preview, 0, sizeof(*preview));
    preview->state = WAVETABLE_PREVIEW_EMPTY;
}

static void wavetable_pool_preview_build(wavetable_slot_t *slot)
{
    if ((slot == 0)
            || (slot->data == 0)
            || (slot->frame_count == 0U)
            || (slot->frame_sample_count != WAVETABLE_FRAME_SAMPLE_COUNT))
    {
        return;
    }

    wavetable_preview_t *const preview = &slot->preview;
    memset(preview, 0, sizeof(*preview));
    preview->state = WAVETABLE_PREVIEW_READY;
    preview->generation = slot->generation;
    preview->frame_count = slot->frame_count;
    preview->columns = WAVETABLE_PREVIEW_COLUMNS;
    for (uint16_t col = 0U; col < WAVETABLE_PREVIEW_COLUMNS; ++col)
    {
        preview->min[col] = 32767;
        preview->max[col] = -32768;
    }

    for (uint32_t frame = 0U; frame < slot->frame_count; ++frame)
    {
        uint32_t col = (uint32_t)(((uint64_t)frame * WAVETABLE_PREVIEW_COLUMNS) / slot->frame_count);
        if (col >= WAVETABLE_PREVIEW_COLUMNS)
        {
            col = WAVETABLE_PREVIEW_COLUMNS - 1U;
        }

        const float *const src = &slot->data[frame * WAVETABLE_FRAME_SAMPLE_COUNT];
        float frame_min = 1.0f;
        float frame_max = -1.0f;
        for (uint32_t i = 0U; i < WAVETABLE_FRAME_SAMPLE_COUNT; ++i)
        {
            const float s = src[i];
            if (s < frame_min)
            {
                frame_min = s;
            }
            if (s > frame_max)
            {
                frame_max = s;
            }
        }

        const int16_t frame_min_s16 =
            wavetable_pool_preview_float_to_i16(frame_min);
        const int16_t frame_max_s16 =
            wavetable_pool_preview_float_to_i16(frame_max);
        if (frame_min_s16 < preview->min[col])
        {
            preview->min[col] = frame_min_s16;
        }
        if (frame_max_s16 > preview->max[col])
        {
            preview->max[col] = frame_max_s16;
        }
        const uint16_t min_peak = wavetable_pool_preview_abs_i16(frame_min_s16);
        const uint16_t max_peak = wavetable_pool_preview_abs_i16(frame_max_s16);
        if (min_peak > preview->global_peak)
        {
            preview->global_peak = min_peak;
        }
        if (max_peak > preview->global_peak)
        {
            preview->global_peak = max_peak;
        }
    }

    for (uint16_t col = 0U; col < WAVETABLE_PREVIEW_COLUMNS; ++col)
    {
        if (preview->min[col] > preview->max[col])
        {
            preview->min[col] = 0;
            preview->max[col] = 0;
        }
    }
}

static uint32_t wavetable_pool_path_hash(const char *path)
{
    uint32_t hash = 2166136261UL;
    if (path == 0)
    {
        return hash;
    }
    while (*path != '\0')
    {
        hash ^= (uint8_t)*path++;
        hash *= 16777619UL;
    }
    return hash;
}

static uint8_t wavetable_pool_make_cache_path_with_extension(char *out,
                                                             uint32_t out_size,
                                                             const char *source_path,
                                                             const FILINFO *source_info,
                                                             const char *extension)
{
    if ((out == 0) || (out_size == 0U) || (source_path == 0)
        || (source_info == 0) || (extension == 0))
    {
        return 0U;
    }
    const int written = snprintf(out,
                                 out_size,
                                 "%s/H%08lX_%08lX_%04X%04X.%s",
                                 WAVETABLE_POOL_CACHE_DIR,
                                 (unsigned long)wavetable_pool_path_hash(source_path),
                                 (unsigned long)((uint32_t)source_info->fsize),
                                 (unsigned)source_info->fdate,
                                 (unsigned)source_info->ftime,
                                 extension);
    return (uint8_t)((written >= 0) && ((uint32_t)written < out_size));
}

static uint8_t wavetable_pool_make_cache_path(char *out,
                                              uint32_t out_size,
                                              const char *source_path,
                                              const FILINFO *source_info)
{
    return wavetable_pool_make_cache_path_with_extension(out,
                                                         out_size,
                                                         source_path,
                                                         source_info,
                                                         "B6WT");
}

static uint8_t wavetable_pool_path_ext_is_wav(const char *path)
{
    const size_t len = (path != 0) ? strlen(path) : 0U;
    if (len < 4U)
    {
        return 0U;
    }
    return (uint8_t)((path[len - 4U] == '.')
                     && ((path[len - 3U] == 'w') || (path[len - 3U] == 'W'))
                     && ((path[len - 2U] == 'a') || (path[len - 2U] == 'A'))
                     && ((path[len - 1U] == 'v') || (path[len - 1U] == 'V')));
}

static void wavetable_pool_slot_error_at(uint16_t wavetable_slot,
                                         wavetable_result_t result,
                                         uint16_t forced_global_slot)
{
    if (wavetable_slot < WAVETABLE_POOL_MAX_SLOTS)
    {
        uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        g_wavetable_pool.slots[wavetable_slot].generation = wavetable_pool_next_generation();
        if (g_wavetable_pool.slots[wavetable_slot].page_count != 0U)
        {
            sample_page_cache_port_release_shared(
                g_wavetable_pool.slots[wavetable_slot].first_page_slot,
                g_wavetable_pool.slots[wavetable_slot].page_count);
        }
        if (g_wavetable_pool.slots[wavetable_slot].mipmap.page_count != 0U)
        {
            sample_page_cache_port_release_shared(
                g_wavetable_pool.slots[wavetable_slot].mipmap.first_page_slot,
                g_wavetable_pool.slots[wavetable_slot].mipmap.page_count);
        }
        g_wavetable_pool.slots[wavetable_slot].format = WAVETABLE_FORMAT_NONE;
        g_wavetable_pool.slots[wavetable_slot].data = 0;
        g_wavetable_pool.slots[wavetable_slot].data_offset = 0U;
        g_wavetable_pool.slots[wavetable_slot].first_page_slot = UINT16_MAX;
        g_wavetable_pool.slots[wavetable_slot].page_count = 0U;
        g_wavetable_pool.slots[wavetable_slot].data_bytes = 0U;
        g_wavetable_pool.slots[wavetable_slot].cost_bytes_aligned = 0U;
        memset(&g_wavetable_pool.slots[wavetable_slot].mipmap,
               0,
               sizeof(g_wavetable_pool.slots[wavetable_slot].mipmap));
        g_wavetable_pool.slots[wavetable_slot].mipmap.first_page_slot = UINT16_MAX;
        wavetable_pool_preview_clear(&g_wavetable_pool.slots[wavetable_slot].preview);
        g_wavetable_pool.slots[wavetable_slot].state = WAVETABLE_SLOT_ERROR;
        g_wavetable_pool.slots[wavetable_slot].error = result;
        const uint8_t registered =
            (forced_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
                ? sample_global_pool_register_wavetable_error_at(
                    forced_global_slot,
                    wavetable_slot,
                    g_wavetable_pool.slots[wavetable_slot].path)
                : sample_global_pool_register_wavetable_error(
                    wavetable_slot,
                    g_wavetable_pool.slots[wavetable_slot].path,
                    &global_slot);
        if (registered != 0U)
        {
            g_wavetable_pool.slots[wavetable_slot].global_slot =
                (forced_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
                    ? forced_global_slot
                    : global_slot;
        }
    }
    wavetable_pool_set_last(result);
}

void wavetable_pool_init(void)
{
    wavetable_pool_reset();
}

void wavetable_pool_reset(void)
{
    memset(g_wavetable_retire_fence, 0, sizeof(g_wavetable_retire_fence));
    uint32_t generation_seed = g_wavetable_pool.generation_counter;
    for (uint16_t i = 0U; i < WAVETABLE_POOL_MAX_SLOTS; ++i)
    {
        g_wavetable_pool.slots[i].generation = wavetable_pool_next_generation();
        if (g_wavetable_pool.slots[i].page_count != 0U)
        {
            sample_page_cache_port_release_shared(
                g_wavetable_pool.slots[i].first_page_slot,
                g_wavetable_pool.slots[i].page_count);
        }
        if (g_wavetable_pool.slots[i].mipmap.page_count != 0U)
        {
            sample_page_cache_port_release_shared(
                g_wavetable_pool.slots[i].mipmap.first_page_slot,
                g_wavetable_pool.slots[i].mipmap.page_count);
        }
        sample_global_pool_clear_backend(SAMPLE_GLOBAL_KIND_WAVETABLE, i);
    }
    generation_seed = g_wavetable_pool.generation_counter;
    memset(&g_wavetable_pool, 0, sizeof(g_wavetable_pool));
    g_wavetable_pool.generation_counter = (generation_seed == 0U) ? 1U : generation_seed;
    for (uint16_t i = 0U; i < WAVETABLE_POOL_MAX_SLOTS; ++i)
    {
        g_wavetable_pool.slots[i].state = WAVETABLE_SLOT_EMPTY;
        g_wavetable_pool.slots[i].global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        g_wavetable_pool.slots[i].first_page_slot = UINT16_MAX;
        g_wavetable_pool.slots[i].mipmap.first_page_slot = UINT16_MAX;
        g_wavetable_pool.slots[i].error = WAVETABLE_RESULT_OK;
        g_wavetable_pool.slots[i].generation = wavetable_pool_next_generation();
    }
    wavetable_pool_set_last(WAVETABLE_RESULT_OK);
}

uint16_t wavetable_pool_find_free_slot(void)
{
    for (uint16_t i = 0U; i < WAVETABLE_POOL_MAX_SLOTS; ++i)
    {
        if (g_wavetable_pool.slots[i].state == WAVETABLE_SLOT_EMPTY)
        {
            return i;
        }
    }
    return WAVETABLE_POOL_INVALID_SLOT;
}

wavetable_result_t wavetable_pool_load_file_auto(const char *path,
                                                 uint16_t *out_wavetable_slot,
                                                 uint16_t *out_global_slot)
{
    const uint16_t wavetable_slot = wavetable_pool_find_free_slot();
    if (out_wavetable_slot != 0)
    {
        *out_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    }
    if (wavetable_slot == WAVETABLE_POOL_INVALID_SLOT)
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_POOL_FULL);
        return WAVETABLE_RESULT_POOL_FULL;
    }

    const wavetable_result_t result =
        wavetable_pool_load_file(wavetable_slot, path, out_global_slot);
    if ((result == WAVETABLE_RESULT_OK) && (out_wavetable_slot != 0))
    {
        *out_wavetable_slot = wavetable_slot;
    }
    return result;
}

wavetable_result_t wavetable_pool_load_file(uint16_t wavetable_slot,
                                            const char *path,
                                            uint16_t *out_global_slot)
{
    return wavetable_pool_load_wav(wavetable_slot, path, out_global_slot);
}

static uint8_t wavetable_pool_wav_info_valid(const wav_info_t *info)
{
    if ((info == 0)
        || ((info->channels != 1U) && (info->channels != 2U))
        || ((info->bits_per_sample != 16U)
            && (info->bits_per_sample != 24U)
            && (info->bits_per_sample != 32U))
        || (info->block_align == 0U)
        || (info->data_size < info->block_align))
    {
        return 0U;
    }

    const uint16_t bytes_per_sample = (uint16_t)(info->bits_per_sample / 8U);
    if ((bytes_per_sample == 0U)
        || (info->block_align != (uint16_t)(info->channels * bytes_per_sample))
        || ((info->data_size % info->block_align) != 0U))
    {
        return 0U;
    }

    const uint32_t source_frames = info->data_size / info->block_align;
    return (uint8_t)((source_frames != 0U)
                     && ((source_frames % WAVETABLE_FRAME_SAMPLE_COUNT) == 0U));
}

static uint8_t wavetable_pool_mipmap_transition_magnitude(uint32_t cycle_count)
{
    if (cycle_count <= 1U)
    {
        return 0U;
    }

    uint32_t transitions = cycle_count - 1U;
    uint8_t magnitude = 0U;
    while (transitions != 0U)
    {
        magnitude++;
        transitions >>= 1U;
    }
    return magnitude;
}

static void wavetable_pool_encode_prepared_header(uint8_t *dst,
                                                const wavetable_slot_t *slot,
                                                const FILINFO *source_info,
                                                uint32_t source_crc32,
                                                uint32_t base_crc32,
                                                uint32_t payload_crc32)
{
    const wavetable_mipmap_view_t *const view = &slot->mipmap;

    memset(dst, 0, WAVETABLE_PREPARED_HEADER_SIZE);
    wavetable_pool_write_u32(&dst[0], WAVETABLE_PREPARED_FILE_MAGIC);
    wavetable_pool_write_u16(&dst[4], WAVETABLE_PREPARED_FILE_VERSION);
    wavetable_pool_write_u16(&dst[6], WAVETABLE_PREPARED_HEADER_SIZE);
    wavetable_pool_write_u32(&dst[8], WAVETABLE_MIPMAP_FLAG_MULTIBAND);
    wavetable_pool_write_u16(&dst[12], (uint16_t)WAVETABLE_FILE_SAMPLE_FLOAT32);
    wavetable_pool_write_u16(&dst[14], 0U);
    wavetable_pool_write_u32(&dst[16], WAVETABLE_FRAME_SAMPLE_COUNT);
    wavetable_pool_write_u32(&dst[20], view->cycle_count);
    wavetable_pool_write_u16(&dst[24], view->band_count);
    wavetable_pool_write_u16(&dst[26], WAVETABLE_PREPARED_BAND_ENTRY_SIZE);
    wavetable_pool_write_u32(&dst[28], WAVETABLE_PREPARED_HEADER_SIZE);
    wavetable_pool_write_u32(&dst[32],
                             WAVETABLE_PREPARED_HEADER_SIZE
                                 + ((uint32_t)view->band_count
                                    * WAVETABLE_PREPARED_BAND_ENTRY_SIZE));
    wavetable_pool_write_u32(&dst[36], view->data_bytes);
    dst[40] = view->cycle_transition_magnitude;
    wavetable_pool_write_i32(&dst[44], view->wave_index_multiplier);
    wavetable_pool_write_u32(&dst[48], wavetable_pool_path_hash(slot->path));
    wavetable_pool_write_u32(&dst[52], (uint32_t)source_info->fsize);
    wavetable_pool_write_u16(&dst[56], source_info->fdate);
    wavetable_pool_write_u16(&dst[58], source_info->ftime);
    wavetable_pool_write_u32(&dst[60], WAVETABLE_MIPMAP_PREP_REVISION);
    memcpy(&dst[64],
           g_wavetable_mipmap_upstream_commit_sha1,
           sizeof(g_wavetable_mipmap_upstream_commit_sha1));
    wavetable_pool_write_u32(&dst[WAVETABLE_PREPARED_SOURCE_CRC_OFFSET],
                             source_crc32);
    wavetable_pool_write_u32(&dst[WAVETABLE_PREPARED_BASE_CRC_OFFSET],
                             base_crc32);
    wavetable_pool_write_u32(&dst[WAVETABLE_PREPARED_PAYLOAD_CRC_OFFSET],
                             payload_crc32);
    wavetable_pool_write_u32(
        &dst[WAVETABLE_PREPARED_TOTAL_SIZE_OFFSET],
        WAVETABLE_PREPARED_HEADER_SIZE
            + ((uint32_t)view->band_count * WAVETABLE_PREPARED_BAND_ENTRY_SIZE)
            + view->data_bytes);
}

static void wavetable_pool_encode_prepared_band(uint8_t *dst,
                                              const wavetable_mipmap_band_t *band,
                                              uint32_t data_offset_bytes)
{
    memset(dst, 0, WAVETABLE_PREPARED_BAND_ENTRY_SIZE);
    wavetable_pool_write_u32(&dst[0], band->max_phase_increment);
    wavetable_pool_write_u32(&dst[4], band->from_cycle);
    wavetable_pool_write_u32(&dst[8], band->to_cycle);
    wavetable_pool_write_u32(&dst[12], band->cycle_sample_count);
    wavetable_pool_write_u16(&dst[16], band->cycle_magnitude);
    wavetable_pool_write_u16(&dst[18], band->flags);
    wavetable_pool_write_u32(&dst[20], data_offset_bytes);
    wavetable_pool_write_u32(&dst[24], band->sample_count);
    wavetable_pool_write_u32(
        &dst[28], wavetable_pool_mipmap_cycle_stride(band->cycle_sample_count));
}

static uint8_t wavetable_pool_write_prepared_cache_file(const char *cache_path,
                                                      const wavetable_slot_t *slot,
                                                      const FILINFO *source_info,
                                                      uint32_t source_crc32,
                                                      uint32_t base_crc32,
                                                      uint32_t payload_crc32)
{
    FIL *const fp = &g_wavetable_transaction_files[1];
    UINT bw = 0U;
    if ((cache_path == 0) || (slot == 0) || (source_info == 0)
        || (slot->mipmap.band_count != wavetable_pool_mipmap_band_count())
        || (slot->mipmap.data == 0))
    {
        return 0U;
    }

    (void)f_mkdir("0:/WAVETABLES");
    (void)f_mkdir(WAVETABLE_POOL_CACHE_DIR);
    if (f_open(fp, cache_path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
    {
        return 0U;
    }

    wavetable_pool_encode_prepared_header(g_wavetable_pool_io,
                                        slot,
                                        source_info,
                                        source_crc32,
                                        base_crc32,
                                        payload_crc32);
    if ((f_write(fp, g_wavetable_pool_io, WAVETABLE_PREPARED_HEADER_SIZE, &bw) != FR_OK)
        || (bw != WAVETABLE_PREPARED_HEADER_SIZE))
    {
        (void)f_close(fp);
        return 0U;
    }

    uint32_t band_data_offset = 0U;
    for (uint16_t band_index = 0U;
         band_index < slot->mipmap.band_count;
         ++band_index)
    {
        const wavetable_mipmap_band_t *const band =
            &slot->mipmap.bands[band_index];
        wavetable_pool_encode_prepared_band(g_wavetable_pool_io,
                                          band,
                                          band_data_offset);
        if ((f_write(fp,
                     g_wavetable_pool_io,
                     WAVETABLE_PREPARED_BAND_ENTRY_SIZE,
                     &bw) != FR_OK)
            || (bw != WAVETABLE_PREPARED_BAND_ENTRY_SIZE))
        {
            (void)f_close(fp);
            return 0U;
        }
        band_data_offset += band->sample_count * sizeof(float);
    }

    const uint8_t *const src = (const uint8_t *)slot->mipmap.data;
    uint32_t done = 0U;
    while (done < slot->mipmap.data_bytes)
    {
        uint32_t chunk = slot->mipmap.data_bytes - done;
        if (chunk > WAVETABLE_POOL_IO_BYTES)
        {
            chunk = WAVETABLE_POOL_IO_BYTES;
        }
        if ((f_write(fp, &src[done], chunk, &bw) != FR_OK) || (bw != chunk))
        {
            (void)f_close(fp);
            return 0U;
        }
        done += chunk;
    }

    const FRESULT sync_result = f_sync(fp);
    const FRESULT close_result = f_close(fp);
    return (uint8_t)((sync_result == FR_OK) && (close_result == FR_OK));
}

typedef struct
{
    uint32_t flags;
    uint16_t sample_format;
    uint16_t duplicate_sample_count;
    uint32_t cycle_sample_count;
    uint32_t cycle_count;
    uint16_t band_count;
    uint16_t band_entry_size;
    uint32_t directory_offset;
    uint32_t data_offset;
    uint32_t data_bytes;
    uint8_t transition_magnitude;
    int32_t wave_index_multiplier;
    uint32_t path_hash;
    uint32_t source_size;
    uint16_t source_date;
    uint16_t source_time;
    uint32_t prep_revision;
    uint32_t source_crc32;
    uint32_t base_crc32;
    uint32_t payload_crc32;
    uint32_t total_file_size;
} wavetable_prepared_cache_header_t;

static void wavetable_pool_decode_prepared_header(
    const uint8_t *src,
    wavetable_prepared_cache_header_t *header)
{
    memset(header, 0, sizeof(*header));
    header->flags = wavetable_pool_read_u32(&src[8]);
    header->sample_format = wavetable_pool_read_u16(&src[12]);
    header->duplicate_sample_count = wavetable_pool_read_u16(&src[14]);
    header->cycle_sample_count = wavetable_pool_read_u32(&src[16]);
    header->cycle_count = wavetable_pool_read_u32(&src[20]);
    header->band_count = wavetable_pool_read_u16(&src[24]);
    header->band_entry_size = wavetable_pool_read_u16(&src[26]);
    header->directory_offset = wavetable_pool_read_u32(&src[28]);
    header->data_offset = wavetable_pool_read_u32(&src[32]);
    header->data_bytes = wavetable_pool_read_u32(&src[36]);
    header->transition_magnitude = src[40];
    header->wave_index_multiplier =
        (int32_t)wavetable_pool_read_u32(&src[44]);
    header->path_hash = wavetable_pool_read_u32(&src[48]);
    header->source_size = wavetable_pool_read_u32(&src[52]);
    header->source_date = wavetable_pool_read_u16(&src[56]);
    header->source_time = wavetable_pool_read_u16(&src[58]);
    header->prep_revision = wavetable_pool_read_u32(&src[60]);
    header->source_crc32 =
        wavetable_pool_read_u32(&src[WAVETABLE_PREPARED_SOURCE_CRC_OFFSET]);
    header->base_crc32 =
        wavetable_pool_read_u32(&src[WAVETABLE_PREPARED_BASE_CRC_OFFSET]);
    header->payload_crc32 =
        wavetable_pool_read_u32(&src[WAVETABLE_PREPARED_PAYLOAD_CRC_OFFSET]);
    header->total_file_size =
        wavetable_pool_read_u32(&src[WAVETABLE_PREPARED_TOTAL_SIZE_OFFSET]);
}

static uint8_t wavetable_pool_source_crc32(FIL *fp, uint32_t *out_crc32)
{
    if ((fp == 0) || (out_crc32 == 0) || (f_lseek(fp, 0U) != FR_OK))
    {
        return 0U;
    }

    uint32_t crc = 0U;
    for (;;)
    {
        UINT br = 0U;
        if (f_read(fp, g_wavetable_pool_io, WAVETABLE_POOL_IO_BYTES, &br) != FR_OK)
        {
            return 0U;
        }
        if (br == 0U)
        {
            break;
        }
        crc = wavetable_pool_crc32_update(crc, g_wavetable_pool_io, br);
    }
    *out_crc32 = crc;
    return (f_lseek(fp, 0U) == FR_OK) ? 1U : 0U;
}

static void wavetable_pool_candidate_init(wavetable_slot_t *candidate)
{
    memset(candidate, 0, sizeof(*candidate));
    candidate->state = WAVETABLE_SLOT_LOADING;
    candidate->global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    candidate->first_page_slot = UINT16_MAX;
    candidate->mipmap.first_page_slot = UINT16_MAX;
    candidate->error = WAVETABLE_RESULT_OK;
    wavetable_pool_preview_clear(&candidate->preview);
}

static void wavetable_pool_candidate_release(wavetable_slot_t *candidate)
{
    if (candidate == 0)
    {
        return;
    }
    if (candidate->page_count != 0U)
    {
        sample_page_cache_port_release_shared(candidate->first_page_slot,
                                                       candidate->page_count);
    }
    if (candidate->mipmap.page_count != 0U)
    {
        sample_page_cache_port_release_shared(
            candidate->mipmap.first_page_slot,
            candidate->mipmap.page_count);
    }
    wavetable_pool_candidate_init(candidate);
}

static uint16_t wavetable_pool_mipmap_band_count(void)
{
    return (uint16_t)(WAVETABLE_MIPMAP_INITIAL_CYCLE_MAGNITUDE
                      - WAVETABLE_MIPMAP_MIN_CYCLE_MAGNITUDE + 1U);
}

static uint32_t wavetable_pool_mipmap_samples_per_table_cycle(void)
{
    uint32_t samples = 0U;
    for (uint32_t magnitude = WAVETABLE_MIPMAP_INITIAL_CYCLE_MAGNITUDE;
         magnitude >= WAVETABLE_MIPMAP_MIN_CYCLE_MAGNITUDE;
         --magnitude)
    {
        samples += wavetable_pool_mipmap_cycle_stride(1UL << magnitude);
    }
    return samples;
}

static void wavetable_pool_fft(float *real, float *imag, uint32_t count, uint8_t inverse)
{
    for (uint32_t i = 1U, j = 0U; i < count; ++i)
    {
        uint32_t bit = count >> 1U;
        while ((j & bit) != 0U)
        {
            j ^= bit;
            bit >>= 1U;
        }
        j ^= bit;
        if (i < j)
        {
            const float real_tmp = real[i];
            const float imag_tmp = imag[i];
            real[i] = real[j];
            imag[i] = imag[j];
            real[j] = real_tmp;
            imag[j] = imag_tmp;
        }
    }

    for (uint32_t length = 2U; length <= count; length <<= 1U)
    {
        const float angle = ((inverse != 0U) ? 2.0f : -2.0f)
            * 3.14159265358979323846f / (float)length;
        const float step_real = cosf(angle);
        const float step_imag = sinf(angle);
        for (uint32_t base = 0U; base < count; base += length)
        {
            float twiddle_real = 1.0f;
            float twiddle_imag = 0.0f;
            const uint32_t half = length >> 1U;
            for (uint32_t offset = 0U; offset < half; ++offset)
            {
                const uint32_t even = base + offset;
                const uint32_t odd = even + half;
                const float odd_real = real[odd] * twiddle_real
                    - imag[odd] * twiddle_imag;
                const float odd_imag = real[odd] * twiddle_imag
                    + imag[odd] * twiddle_real;
                const float even_real = real[even];
                const float even_imag = imag[even];
                real[even] = even_real + odd_real;
                imag[even] = even_imag + odd_imag;
                real[odd] = even_real - odd_real;
                imag[odd] = even_imag - odd_imag;
                const float next_real = twiddle_real * step_real
                    - twiddle_imag * step_imag;
                twiddle_imag = twiddle_real * step_imag
                    + twiddle_imag * step_real;
                twiddle_real = next_real;
            }
        }
    }

    if (inverse != 0U)
    {
        const float scale = 1.0f / (float)count;
        for (uint32_t i = 0U; i < count; ++i)
        {
            real[i] *= scale;
            imag[i] *= scale;
        }
    }
}

static void wavetable_pool_resample_2048_cycle_frequency(float *dst)
{
    for (uint32_t i = 0U; i < WAVETABLE_SOURCE_2048_SAMPLE_COUNT; ++i)
    {
        g_wavetable_fft_imag[i] = 0.0f;
    }
    wavetable_pool_fft(g_wavetable_fft_real,
                       g_wavetable_fft_imag,
                       WAVETABLE_SOURCE_2048_SAMPLE_COUNT,
                       0U);

    memset(g_wavetable_fft_work_real, 0, sizeof(g_wavetable_fft_work_real));
    memset(g_wavetable_fft_work_imag, 0, sizeof(g_wavetable_fft_work_imag));
    const float scale = (float)WAVETABLE_FRAME_SAMPLE_COUNT
        / (float)WAVETABLE_SOURCE_2048_SAMPLE_COUNT;
    g_wavetable_fft_work_real[0] = g_wavetable_fft_real[0] * scale;
    const uint32_t nyquist = WAVETABLE_FRAME_SAMPLE_COUNT >> 1U;
    for (uint32_t bin = 1U; bin < nyquist; ++bin)
    {
        const float gain = wavetable_pool_spectral_gain(
            bin,
            nyquist,
            WAVETABLE_PREP_RESAMPLE_PASS_RATIO,
            WAVETABLE_PREP_RESAMPLE_STOP_RATIO);
        const float real = g_wavetable_fft_real[bin] * scale * gain;
        const float imag = g_wavetable_fft_imag[bin] * scale * gain;
        g_wavetable_fft_work_real[bin] = real;
        g_wavetable_fft_work_imag[bin] = imag;
        g_wavetable_fft_work_real[WAVETABLE_FRAME_SAMPLE_COUNT - bin] = real;
        g_wavetable_fft_work_imag[WAVETABLE_FRAME_SAMPLE_COUNT - bin] = -imag;
    }
    /* Nyquist has no phase-preserving representation after decimation. */
    g_wavetable_fft_work_real[nyquist] = 0.0f;
    g_wavetable_fft_work_imag[nyquist] = 0.0f;
    wavetable_pool_fft(g_wavetable_fft_work_real,
                       g_wavetable_fft_work_imag,
                       WAVETABLE_FRAME_SAMPLE_COUNT,
                       1U);
    for (uint32_t i = 0U; i < WAVETABLE_FRAME_SAMPLE_COUNT; ++i)
    {
        dst[i] = wavetable_pool_finite_float(g_wavetable_fft_work_real[i]);
    }
}

static void wavetable_pool_generate_band_from_fft(float *dst,
                                                   uint32_t count,
                                                   uint16_t band_index)
{
    memset(g_wavetable_fft_work_real, 0, count * sizeof(float));
    memset(g_wavetable_fft_work_imag, 0, count * sizeof(float));
    const float spectrum_scale =
        (float)count / (float)WAVETABLE_FRAME_SAMPLE_COUNT;
    const float pass_ratio = (band_index == 0U)
        ? WAVETABLE_PREP_BASE_PASS_RATIO : WAVETABLE_PREP_MIP_PASS_RATIO;
    const float stop_ratio = (band_index == 0U)
        ? WAVETABLE_PREP_BASE_STOP_RATIO : WAVETABLE_PREP_MIP_STOP_RATIO;
    g_wavetable_fft_work_real[0] = g_wavetable_fft_real[0] * spectrum_scale;
    const uint32_t nyquist = count >> 1U;
    for (uint32_t bin = 1U; bin < nyquist; ++bin)
    {
        const float gain = wavetable_pool_spectral_gain(
            bin, nyquist, pass_ratio, stop_ratio);
        const float real = g_wavetable_fft_real[bin] * spectrum_scale * gain;
        const float imag = g_wavetable_fft_imag[bin] * spectrum_scale * gain;
        g_wavetable_fft_work_real[bin] = real;
        g_wavetable_fft_work_imag[bin] = imag;
        g_wavetable_fft_work_real[count - bin] = real;
        g_wavetable_fft_work_imag[count - bin] = -imag;
    }
    g_wavetable_fft_work_real[nyquist] = 0.0f;
    g_wavetable_fft_work_imag[nyquist] = 0.0f;
    wavetable_pool_fft(g_wavetable_fft_work_real,
                       g_wavetable_fft_work_imag,
                       count,
                       1U);
    for (uint32_t i = 0U; i < count; ++i)
    {
        dst[i] = wavetable_pool_finite_float(g_wavetable_fft_work_real[i]);
    }
}

static wavetable_result_t wavetable_pool_candidate_allocate(
    uint16_t wavetable_slot,
    const char *path,
    uint32_t frame_count,
    wavetable_slot_t *candidate)
{
    if ((path == 0) || (candidate == 0) || (frame_count == 0U)
        || (frame_count > (UINT32_MAX / WAVETABLE_FRAME_SAMPLE_COUNT)))
    {
        return WAVETABLE_RESULT_INVALID_ARG;
    }

    const uint32_t brick_samples =
        frame_count * WAVETABLE_FRAME_SAMPLE_COUNT;
    const uint32_t mipmap_samples_per_cycle =
        wavetable_pool_mipmap_samples_per_table_cycle();
    if (frame_count > (UINT32_MAX / mipmap_samples_per_cycle)
        || brick_samples > (UINT32_MAX / sizeof(float)))
    {
        return WAVETABLE_RESULT_TOO_LARGE;
    }
    const uint32_t mipmap_samples = frame_count * mipmap_samples_per_cycle;
    if (mipmap_samples > (UINT32_MAX / sizeof(float)))
    {
        return WAVETABLE_RESULT_TOO_LARGE;
    }

    const uint32_t brick_bytes = brick_samples * sizeof(float);
    const uint32_t mipmap_bytes = mipmap_samples * sizeof(float);
    const uint32_t brick_pages =
        (brick_bytes + SAMPLE_PAGE_BYTES - 1U) / SAMPLE_PAGE_BYTES;
    const uint32_t mipmap_pages =
        (mipmap_bytes + SAMPLE_PAGE_BYTES - 1U) / SAMPLE_PAGE_BYTES;
    if ((brick_pages == 0U) || (mipmap_pages == 0U)
        || (brick_pages > (UINT32_MAX / SAMPLE_PAGE_BYTES))
        || (mipmap_pages > (UINT32_MAX / SAMPLE_PAGE_BYTES)))
    {
        return WAVETABLE_RESULT_TOO_LARGE;
    }
    const uint32_t brick_cost = brick_pages * SAMPLE_PAGE_BYTES;
    const uint32_t mipmap_cost = mipmap_pages * SAMPLE_PAGE_BYTES;
    if ((brick_cost > sample_page_cache_port_shared_total_bytes())
        || (mipmap_cost > sample_page_cache_port_shared_total_bytes())
        || (brick_cost > (UINT32_MAX - mipmap_cost)))
    {
        return WAVETABLE_RESULT_TOO_LARGE;
    }
    if (sample_global_pool_validate_budget(SAMPLE_GLOBAL_KIND_WAVETABLE,
                                           wavetable_slot,
                                           brick_cost + mipmap_cost) == 0U)
    {
        return WAVETABLE_RESULT_GLOBAL_BUDGET_FULL;
    }

    sample_page_loader_allocation_t brick_allocation;
    sample_page_loader_allocation_t mipmap_allocation;
    memset(&brick_allocation, 0, sizeof(brick_allocation));
    memset(&mipmap_allocation, 0, sizeof(mipmap_allocation));
    if (sample_page_cache_port_alloc_shared(brick_bytes,
                                                &brick_allocation) == 0U)
    {
        return WAVETABLE_RESULT_RAM_POOL_FULL;
    }
    if (sample_page_cache_port_alloc_shared(mipmap_bytes,
                                                &mipmap_allocation) == 0U)
    {
        sample_page_cache_port_release_shared(
            brick_allocation.first_slot,
            brick_allocation.page_count);
        return WAVETABLE_RESULT_RAM_POOL_FULL;
    }

    wavetable_pool_candidate_init(candidate);
    if (wavetable_pool_copy_path(candidate->path,
                                 sizeof(candidate->path),
                                 path) == 0U)
    {
        sample_page_cache_port_release_shared(
            brick_allocation.first_slot,
            brick_allocation.page_count);
        sample_page_cache_port_release_shared(
            mipmap_allocation.first_slot,
            mipmap_allocation.page_count);
        return WAVETABLE_RESULT_PATH_TOO_LONG;
    }
    candidate->format = WAVETABLE_FORMAT_FLOAT32_MONO;
    candidate->frame_sample_count = WAVETABLE_FRAME_SAMPLE_COUNT;
    candidate->frame_count = frame_count;
    candidate->data_offset =
        (uint32_t)brick_allocation.first_slot * SAMPLE_PAGE_BYTES;
    candidate->first_page_slot = brick_allocation.first_slot;
    candidate->page_count = brick_allocation.page_count;
    candidate->data_bytes = brick_bytes;
    candidate->data = (float *)sample_page_cache_port_resolve_shared(
        &brick_allocation);
    candidate->cost_bytes_aligned =
        brick_allocation.capacity_bytes + mipmap_allocation.capacity_bytes;
    candidate->mipmap.data = (float *)sample_page_cache_port_resolve_shared(
        &mipmap_allocation);
    candidate->mipmap.data_bytes = mipmap_bytes;
    candidate->mipmap.first_page_slot = mipmap_allocation.first_slot;
    candidate->mipmap.page_count = mipmap_allocation.page_count;
    candidate->mipmap.cost_bytes_aligned = mipmap_allocation.capacity_bytes;
    return WAVETABLE_RESULT_OK;
}

static void wavetable_pool_candidate_mipmap_init(wavetable_slot_t *candidate)
{
    wavetable_mipmap_view_t *const view = &candidate->mipmap;
    float *const data = view->data;
    const uint32_t data_bytes = view->data_bytes;
    const uint16_t first_page_slot = view->first_page_slot;
    const uint16_t page_count = view->page_count;
    const uint32_t cost_bytes_aligned = view->cost_bytes_aligned;

    memset(view, 0, sizeof(*view));
    view->band_count = wavetable_pool_mipmap_band_count();
    view->cycle_count = candidate->frame_count;
    view->cycle_transition_magnitude =
        wavetable_pool_mipmap_transition_magnitude(candidate->frame_count);
    if (candidate->frame_count > 1U)
    {
        const uint32_t transitions = candidate->frame_count - 1U;
        view->wave_index_multiplier =
            (int32_t)(transitions << (31U - view->cycle_transition_magnitude));
    }
    view->data = data;
    view->data_bytes = data_bytes;
    view->first_page_slot = first_page_slot;
    view->page_count = page_count;
    view->cost_bytes_aligned = cost_bytes_aligned;

    uint32_t data_offset_samples = 0U;
    for (uint16_t band_index = 0U;
         band_index < view->band_count;
         ++band_index)
    {
        wavetable_mipmap_band_t *const band = &view->bands[band_index];
        const uint32_t magnitude =
            WAVETABLE_MIPMAP_INITIAL_CYCLE_MAGNITUDE - band_index;
        const uint32_t cycle_samples = 1UL << magnitude;
        band->max_phase_increment =
            (uint32_t)((double)(UINT32_MAX >> magnitude)
                * WAVETABLE_PREP_PHASE_LIMIT_RATIO);
        band->from_cycle = 0U;
        band->to_cycle = candidate->frame_count;
        band->cycle_sample_count = cycle_samples;
        band->cycle_magnitude = (uint16_t)magnitude;
        band->flags = (uint16_t)WAVETABLE_MIPMAP_FLAG_MULTIBAND;
        band->data = &view->data[data_offset_samples];
        band->sample_count = candidate->frame_count
            * wavetable_pool_mipmap_cycle_stride(cycle_samples);
        data_offset_samples += band->sample_count;
    }

}

static void wavetable_pool_candidate_prepare_mipmap(wavetable_slot_t *candidate)
{
    wavetable_pool_candidate_mipmap_init(candidate);
    wavetable_mipmap_view_t *const view = &candidate->mipmap;
    for (uint32_t cycle = 0U; cycle < candidate->frame_count; ++cycle)
    {
        const float *const src =
            &candidate->data[cycle * WAVETABLE_FRAME_SAMPLE_COUNT];
        for (uint32_t i = 0U; i < WAVETABLE_FRAME_SAMPLE_COUNT; ++i)
        {
            g_wavetable_fft_real[i] = src[i];
            g_wavetable_fft_imag[i] = 0.0f;
        }
        wavetable_pool_fft(g_wavetable_fft_real,
                           g_wavetable_fft_imag,
                           WAVETABLE_FRAME_SAMPLE_COUNT,
                           0U);

        for (uint16_t band_index = 0U;
             band_index < view->band_count;
             ++band_index)
        {
            wavetable_mipmap_band_t *const band = &view->bands[band_index];
            const uint32_t count = band->cycle_sample_count;
            const uint32_t cycle_stride =
                wavetable_pool_mipmap_cycle_stride(count);
            float *const dst = &band->data[cycle * cycle_stride];
            wavetable_pool_generate_band_from_fft(dst, count, band_index);
            if (cycle_stride > count)
            {
                memset(&dst[count], 0,
                       (cycle_stride - count) * sizeof(float));
            }
        }
        wavetable_pool_copy_float_exact(
            &candidate->data[cycle * WAVETABLE_FRAME_SAMPLE_COUNT],
            &view->bands[0].data[cycle * WAVETABLE_FRAME_SAMPLE_COUNT],
            WAVETABLE_FRAME_SAMPLE_COUNT);
    }
}

static wavetable_result_t wavetable_pool_candidate_decode_wav(
    FIL *fp,
    const wav_info_t *wav_info,
    wavetable_source_geometry_t source_geometry,
    wavetable_slot_t *candidate)
{
    if (f_lseek(fp, wav_info->data_offset) != FR_OK)
    {
        return WAVETABLE_RESULT_READ_FAIL;
    }

    const uint32_t source_cycle_sample_count =
        wavetable_pool_source_cycle_sample_count(source_geometry);
    const uint32_t sample_count = candidate->frame_count
        * source_cycle_sample_count;
    uint32_t frames_done = 0U;
    uint32_t cycle = 0U;
    uint32_t cycle_offset = 0U;
    while (frames_done < sample_count)
    {
        uint32_t frames_chunk = WAVETABLE_POOL_IO_BYTES / wav_info->block_align;
        if (frames_chunk > (sample_count - frames_done))
        {
            frames_chunk = sample_count - frames_done;
        }
        if (frames_chunk == 0U)
        {
            return WAVETABLE_RESULT_READ_FAIL;
        }

        const UINT bytes_to_read = (UINT)(frames_chunk * wav_info->block_align);
        UINT br = 0U;
        if ((f_read(fp, g_wavetable_pool_io, bytes_to_read, &br) != FR_OK)
            || (br != bytes_to_read))
        {
            return WAVETABLE_RESULT_READ_FAIL;
        }

        const uint8_t *src = g_wavetable_pool_io;
        for (uint32_t i = 0U; i < frames_chunk; ++i)
        {
            float left = 0.0f;
            float right = 0.0f;
            wav_audio_codec_decode_stereo_frame(src,
                                                wav_info->channels,
                                                wav_info->bits_per_sample,
                                                &left,
                                                &right);
            const float mono = (wav_info->channels == 1U)
                ? left : ((left + right) * 0.5f);
            if (source_cycle_sample_count == WAVETABLE_FRAME_SAMPLE_COUNT)
            {
                candidate->data[frames_done + i] =
                    wavetable_pool_finite_float(mono);
            }
            else
            {
                wavetable_pool_source_2048_store(cycle_offset++, mono);
                if (cycle_offset == WAVETABLE_SOURCE_2048_SAMPLE_COUNT)
                {
                    wavetable_pool_resample_2048_cycle_frequency(
                        &candidate->data[cycle * WAVETABLE_FRAME_SAMPLE_COUNT]);
                    cycle++;
                    cycle_offset = 0U;
                }
            }
            src += wav_info->block_align;
        }
        frames_done += frames_chunk;
    }
    wavetable_pool_candidate_prepare_mipmap(candidate);
    return WAVETABLE_RESULT_OK;
}

static uint8_t wavetable_pool_make_temp_path(char *out,
                                             uint32_t out_size,
                                             const char *final_path)
{
    const int written = snprintf(out, out_size, "%s.TMP", final_path);
    return (uint8_t)((written >= 0) && ((uint32_t)written < out_size));
}

static uint8_t wavetable_pool_write_cache_transactional(
    const char *cache_path,
    const wavetable_slot_t *candidate,
    const FILINFO *source_info,
    uint32_t source_crc32,
    uint32_t base_crc32,
    uint32_t payload_crc32)
{
    char *const temp_path = g_wavetable_transaction_paths[1];
    if (wavetable_pool_make_temp_path(temp_path,
                                      WAVETABLE_POOL_PATH_MAX,
                                      cache_path) == 0U)
    {
        return 0U;
    }

    (void)f_unlink(temp_path);
    if (wavetable_pool_write_prepared_cache_file(temp_path,
                                                 candidate,
                                                 source_info,
                                                 source_crc32,
                                                 base_crc32,
                                                 payload_crc32) == 0U)
    {
        (void)f_unlink(temp_path);
        return 0U;
    }

    (void)f_unlink(cache_path);
    if (f_rename(temp_path, cache_path) != FR_OK)
    {
        (void)f_unlink(temp_path);
        return 0U;
    }
    return 1U;
}

static uint8_t wavetable_pool_read_exact(FIL *fp,
                                         uint8_t *dst,
                                         uint32_t bytes,
                                         uint32_t *out_crc32)
{
    uint32_t done = 0U;
    uint32_t crc = 0U;
    while (done < bytes)
    {
        uint32_t chunk = bytes - done;
        if (chunk > WAVETABLE_POOL_IO_BYTES)
        {
            chunk = WAVETABLE_POOL_IO_BYTES;
        }
        UINT br = 0U;
        if ((f_read(fp, g_wavetable_pool_io, chunk, &br) != FR_OK)
            || (br != chunk))
        {
            return 0U;
        }
        memcpy(&dst[done], g_wavetable_pool_io, chunk);
        crc = wavetable_pool_crc32_update(crc, g_wavetable_pool_io, chunk);
        done += chunk;
    }
    if (out_crc32 != 0)
    {
        *out_crc32 = crc;
    }
    return 1U;
}

static uint8_t wavetable_pool_load_prepared_cache(
    const char *cache_path,
    const char *source_path,
    const FILINFO *source_info,
    uint32_t source_crc32,
    wavetable_slot_t *candidate)
{
    FIL *const fp = &g_wavetable_transaction_files[1];
    UINT br = 0U;
    wavetable_prepared_cache_header_t header;
    if ((f_open(fp, cache_path, FA_READ) != FR_OK)
        || (f_read(fp, g_wavetable_pool_io,
                   WAVETABLE_PREPARED_HEADER_SIZE, &br) != FR_OK)
        || (br != WAVETABLE_PREPARED_HEADER_SIZE)
        || (wavetable_pool_read_u32(&g_wavetable_pool_io[0])
            != WAVETABLE_PREPARED_FILE_MAGIC)
        || (wavetable_pool_read_u16(&g_wavetable_pool_io[4])
            != WAVETABLE_PREPARED_FILE_VERSION)
        || (wavetable_pool_read_u16(&g_wavetable_pool_io[6])
            != WAVETABLE_PREPARED_HEADER_SIZE))
    {
        (void)f_close(fp);
        return 0U;
    }

    wavetable_pool_decode_prepared_header(g_wavetable_pool_io, &header);
    const uint16_t band_count = wavetable_pool_mipmap_band_count();
    const uint32_t directory_bytes =
        (uint32_t)band_count * WAVETABLE_PREPARED_BAND_ENTRY_SIZE;
    const uint32_t expected_data_offset =
        WAVETABLE_PREPARED_HEADER_SIZE + directory_bytes;
    const uint32_t expected_total_size =
        expected_data_offset + candidate->mipmap.data_bytes;
    const uint8_t transition =
        wavetable_pool_mipmap_transition_magnitude(candidate->frame_count);
    int32_t wave_index_multiplier = 0;
    if (candidate->frame_count > 1U)
    {
        wave_index_multiplier = (int32_t)(
            (candidate->frame_count - 1U) << (31U - transition));
    }

    if ((header.flags != WAVETABLE_MIPMAP_FLAG_MULTIBAND)
        || (header.sample_format != (uint16_t)WAVETABLE_FILE_SAMPLE_FLOAT32)
        || (header.duplicate_sample_count != 0U)
        || (header.cycle_sample_count != WAVETABLE_FRAME_SAMPLE_COUNT)
        || (header.cycle_count != candidate->frame_count)
        || (header.band_count != band_count)
        || (header.band_entry_size != WAVETABLE_PREPARED_BAND_ENTRY_SIZE)
        || (header.directory_offset != WAVETABLE_PREPARED_HEADER_SIZE)
        || (header.data_offset != expected_data_offset)
        || (header.data_bytes != candidate->mipmap.data_bytes)
        || (header.transition_magnitude != transition)
        || (header.wave_index_multiplier != wave_index_multiplier)
        || (header.path_hash != wavetable_pool_path_hash(source_path))
        || (header.source_size != (uint32_t)source_info->fsize)
        || (header.source_date != source_info->fdate)
        || (header.source_time != source_info->ftime)
        || (header.prep_revision != WAVETABLE_MIPMAP_PREP_REVISION)
        || (memcmp(&g_wavetable_pool_io[64],
                   g_wavetable_mipmap_upstream_commit_sha1,
                   sizeof(g_wavetable_mipmap_upstream_commit_sha1)) != 0)
        || (header.source_crc32 != source_crc32)
        || (header.total_file_size != expected_total_size)
        || (f_size(fp) != (FSIZE_t)expected_total_size)
        || (f_lseek(fp, header.directory_offset) != FR_OK))
    {
        (void)f_close(fp);
        return 0U;
    }

    wavetable_mipmap_view_t *const view = &candidate->mipmap;
    view->band_count = band_count;
    view->cycle_count = candidate->frame_count;
    view->cycle_transition_magnitude = transition;
    view->wave_index_multiplier = wave_index_multiplier;
    uint32_t payload_offset = 0U;
    for (uint16_t i = 0U; i < band_count; ++i)
    {
        if ((f_read(fp, g_wavetable_pool_io,
                    WAVETABLE_PREPARED_BAND_ENTRY_SIZE, &br) != FR_OK)
            || (br != WAVETABLE_PREPARED_BAND_ENTRY_SIZE))
        {
            (void)f_close(fp);
            return 0U;
        }
        wavetable_mipmap_band_t *const band = &view->bands[i];
        band->max_phase_increment = wavetable_pool_read_u32(&g_wavetable_pool_io[0]);
        band->from_cycle = wavetable_pool_read_u32(&g_wavetable_pool_io[4]);
        band->to_cycle = wavetable_pool_read_u32(&g_wavetable_pool_io[8]);
        band->cycle_sample_count = wavetable_pool_read_u32(&g_wavetable_pool_io[12]);
        band->cycle_magnitude = wavetable_pool_read_u16(&g_wavetable_pool_io[16]);
        band->flags = wavetable_pool_read_u16(&g_wavetable_pool_io[18]);
        const uint32_t file_offset = wavetable_pool_read_u32(&g_wavetable_pool_io[20]);
        band->sample_count = wavetable_pool_read_u32(&g_wavetable_pool_io[24]);
        const uint32_t file_cycle_stride =
            wavetable_pool_read_u32(&g_wavetable_pool_io[28]);
        const uint32_t magnitude = WAVETABLE_MIPMAP_INITIAL_CYCLE_MAGNITUDE - i;
        const uint32_t cycle_samples = 1UL << magnitude;
        const uint32_t cycle_stride =
            wavetable_pool_mipmap_cycle_stride(cycle_samples);
        const uint32_t sample_count = candidate->frame_count * cycle_stride;
        const uint32_t max_inc =
            (uint32_t)((double)(UINT32_MAX >> magnitude)
                * WAVETABLE_PREP_PHASE_LIMIT_RATIO);
        const uint32_t bytes = sample_count * sizeof(float);
        if ((band->from_cycle != 0U)
            || (band->to_cycle != candidate->frame_count)
            || (band->cycle_sample_count != cycle_samples)
            || (file_cycle_stride != cycle_stride)
            || (band->cycle_magnitude != magnitude)
            || (band->max_phase_increment != max_inc)
            || (band->flags != WAVETABLE_MIPMAP_FLAG_MULTIBAND)
            || (band->sample_count != sample_count)
            || (file_offset != payload_offset)
            || (payload_offset > view->data_bytes)
            || (bytes > (view->data_bytes - payload_offset)))
        {
            (void)f_close(fp);
            return 0U;
        }
        band->data = &view->data[payload_offset / sizeof(float)];
        payload_offset += bytes;
    }
    if ((payload_offset != view->data_bytes)
        || (f_lseek(fp, header.data_offset) != FR_OK))
    {
        (void)f_close(fp);
        return 0U;
    }

    uint32_t payload_crc = 0U;
    const uint8_t payload_ok = wavetable_pool_read_exact(
        fp, (uint8_t *)view->data, view->data_bytes, &payload_crc);
    (void)f_close(fp);
    if ((payload_ok == 0U) || (payload_crc != header.payload_crc32))
    {
        return 0U;
    }

    const wavetable_mipmap_band_t *const base = &view->bands[0];
    const uint32_t stride =
        wavetable_pool_mipmap_cycle_stride(base->cycle_sample_count);
    for (uint32_t frame = 0U; frame < candidate->frame_count; ++frame)
    {
        wavetable_pool_copy_float_exact(
            &candidate->data[frame * WAVETABLE_FRAME_SAMPLE_COUNT],
            &base->data[frame * stride],
            WAVETABLE_FRAME_SAMPLE_COUNT);
    }
    return (wavetable_pool_crc32_memory(candidate->data, candidate->data_bytes)
            == header.base_crc32) ? 1U : 0U;
}


static void wavetable_pool_release_slot_allocations(
    const wavetable_slot_t *slot)
{
    if (slot->page_count != 0U)
    {
        sample_page_cache_port_release_shared(slot->first_page_slot,
                                                       slot->page_count);
    }
    if (slot->mipmap.page_count != 0U)
    {
        sample_page_cache_port_release_shared(
            slot->mipmap.first_page_slot,
            slot->mipmap.page_count);
    }
}

static uint8_t wavetable_pool_publish_audio_descriptor(
    uint16_t wavetable_slot, const wavetable_slot_t *slot)
{
    audio_wavetable_descriptor_t descriptor;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.generation = slot->generation;
    descriptor.frame_count = slot->frame_count;
    descriptor.frame_sample_count = slot->frame_sample_count;
    descriptor.wavetable_slot = wavetable_slot;
    descriptor.global_slot = slot->global_slot;
    descriptor.band_count = slot->mipmap.band_count;
    if (audio_shared_memory_page_ref(slot->first_page_slot, 0U,
                                     slot->data_bytes, &descriptor.base_data) == 0U)
        return 0U;
    dcache_clean_by_addr_aligned(slot->data, slot->data_bytes);
    uint32_t mip_offset = 0U;
    for (uint16_t i = 0U; i < descriptor.band_count; ++i)
    {
        const wavetable_mipmap_band_t *const src = &slot->mipmap.bands[i];
        audio_wavetable_band_t *const dst = &descriptor.bands[i];
        const uint32_t bytes = src->sample_count * sizeof(float);
        dst->max_phase_increment = src->max_phase_increment;
        dst->cycle_sample_count = src->cycle_sample_count;
        dst->sample_count = src->sample_count;
        dst->cycle_magnitude = src->cycle_magnitude;
        dst->flags = src->flags;
        if (audio_shared_memory_page_ref(slot->mipmap.first_page_slot,
                                         mip_offset, bytes, &dst->data) == 0U)
            return 0U;
        mip_offset += bytes;
    }
    dcache_clean_by_addr_aligned(slot->mipmap.data, slot->mipmap.data_bytes);
    return audio_wavetable_registry_transport_install(&descriptor);
}

static wavetable_result_t wavetable_pool_commit_candidate(
    uint16_t wavetable_slot,
    uint16_t forced_global_slot,
    wavetable_slot_t *candidate,
    uint16_t *out_global_slot)
{
    g_wavetable_old_commit_snapshot = g_wavetable_pool.slots[wavetable_slot];
    const wavetable_slot_t *const old = &g_wavetable_old_commit_snapshot;
    if (old->state == WAVETABLE_SLOT_READY)
    {
        wavetable_pool_clear(wavetable_slot);
        return WAVETABLE_RESULT_SD_BUSY;
    }
    if (old->state == WAVETABLE_SLOT_RETIRING)
        return WAVETABLE_RESULT_SD_BUSY;
    uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (forced_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
    {
        global_slot = forced_global_slot;
    }
    else if (old->global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
    {
        global_slot = old->global_slot;
    }

    const uint8_t registered =
        (global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
            ? sample_global_pool_register_wavetable_at(global_slot,
                                                       wavetable_slot,
                                                       candidate->path,
                                                       candidate->cost_bytes_aligned)
            : sample_global_pool_register_wavetable(wavetable_slot,
                                                    candidate->path,
                                                    candidate->cost_bytes_aligned,
                                                    &global_slot);
    if (registered == 0U)
    {
        return WAVETABLE_RESULT_REGISTER_FAIL;
    }

    candidate->global_slot = global_slot;
    candidate->generation = wavetable_pool_next_generation();
    candidate->state = WAVETABLE_SLOT_READY;
    candidate->error = WAVETABLE_RESULT_OK;
    if (candidate->preview.state != WAVETABLE_PREVIEW_READY)
    {
        wavetable_pool_preview_build(candidate);
    }
    else
    {
        candidate->preview.generation = candidate->generation;
    }

    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    g_wavetable_pool.slots[wavetable_slot] = *candidate;
    __DMB();
    if (primask == 0U)
    {
        __enable_irq();
    }

    candidate->page_count = 0U;
    candidate->mipmap.page_count = 0U;
    if (wavetable_pool_publish_audio_descriptor(
            wavetable_slot, &g_wavetable_pool.slots[wavetable_slot]) == 0U)
        return WAVETABLE_RESULT_REGISTER_FAIL;
    wavetable_pool_release_slot_allocations(old);
    if (out_global_slot != 0)
    {
        *out_global_slot = global_slot;
    }
    return WAVETABLE_RESULT_OK;
}

static sd_scheduler_background_admission_t wavetable_load_sd_begin(
    sd_scheduler_background_kind_t kind, uint32_t bytes)
{
    const sd_scheduler_background_request_t request = {
        .byte_count = bytes,
        .media_epoch = g_wavetable_load_job.media_epoch,
        .kind = kind
    };
    return sd_scheduler_runtime_background_try_begin(&request);
}

static void wavetable_load_fail(wavetable_result_t result)
{
    g_wavetable_load_job.result = result;
    g_wavetable_load_job.state = WAVETABLE_LOAD_CLEANUP;
    wavetable_pool_set_last(result);
}

uint8_t wavetable_pool_load_async_begin_with_geometry(
    uint16_t wavetable_slot,
    const char *path,
    wavetable_source_geometry_t source_geometry)
{
    wavetable_load_job_t *const job = &g_wavetable_load_job;
    if ((job->state != WAVETABLE_LOAD_IDLE)
        || (wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
        || (path == 0) || (path[0] == '\0')
        || (wavetable_pool_path_ext_is_wav(path) == 0U)
        || ((source_geometry != WAVETABLE_SOURCE_GEOMETRY_1024)
            && (source_geometry != WAVETABLE_SOURCE_GEOMETRY_2048)))
    {
        return 0U;
    }
    memset(job, 0, sizeof(*job));
    wavetable_pool_candidate_init(&g_wavetable_candidate);
    if (wavetable_pool_copy_path(job->path, sizeof(job->path), path) == 0U)
    {
        return 0U;
    }
    job->wavetable_slot = wavetable_slot;
    job->global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    job->media_epoch = sd_access_media_epoch();
    job->source_geometry = source_geometry;
    job->result = WAVETABLE_RESULT_OK;
    job->state = WAVETABLE_LOAD_MOUNT;
    return 1U;
}

uint8_t wavetable_pool_load_async_begin(uint16_t wavetable_slot, const char *path)
{
    return wavetable_pool_load_async_begin_with_geometry(
        wavetable_slot, path, WAVETABLE_DEFAULT_SOURCE_GEOMETRY);
}

uint8_t wavetable_pool_load_async_busy(void)
{
    return ((g_wavetable_load_job.state != WAVETABLE_LOAD_IDLE)
            && (g_wavetable_load_job.state != WAVETABLE_LOAD_DONE)) ? 1U : 0U;
}

static void wavetable_load_process_band(wavetable_load_job_t *job)
{
    wavetable_slot_t *const candidate = &g_wavetable_candidate;
    wavetable_mipmap_view_t *const view = &candidate->mipmap;
    wavetable_mipmap_band_t *const band = &view->bands[job->band];
    const uint32_t count = band->cycle_sample_count;
    const uint32_t cycle_stride = wavetable_pool_mipmap_cycle_stride(count);
    float *const dst = &band->data[job->cycle * cycle_stride];
    wavetable_pool_generate_band_from_fft(dst, count, job->band);
    if (cycle_stride > count)
    {
        memset(&dst[count], 0,
               (cycle_stride - count) * sizeof(float));
    }
    job->band++;
    if (job->band >= view->band_count)
    {
        job->band = 0U;
        wavetable_pool_copy_float_exact(
            &candidate->data[job->cycle * WAVETABLE_FRAME_SAMPLE_COUNT],
            &view->bands[0].data[job->cycle * WAVETABLE_FRAME_SAMPLE_COUNT],
            WAVETABLE_FRAME_SAMPLE_COUNT);
        job->cycle++;
        job->state = (job->cycle >= candidate->frame_count)
            ? WAVETABLE_LOAD_BASE_CRC : WAVETABLE_LOAD_MIP_FORWARD_PREP;
        job->io_offset = 0U;
    }
}

void wavetable_pool_load_async_service(void)
{
    wavetable_load_job_t *const job = &g_wavetable_load_job;
    wavetable_slot_t *const candidate = &g_wavetable_candidate;
    if ((job->state == WAVETABLE_LOAD_IDLE) || (job->state == WAVETABLE_LOAD_DONE)) return;

    switch (job->state)
    {
        case WAVETABLE_LOAD_ALLOCATE:
        {
            const uint32_t samples = job->wav_info.data_size / job->wav_info.block_align;
            job->source_cycle_sample_count =
                wavetable_pool_source_cycle_sample_count(job->source_geometry);
            if ((samples % job->source_cycle_sample_count) != 0U)
            {
                wavetable_load_fail(WAVETABLE_RESULT_UNSUPPORTED);
                return;
            }
            const wavetable_result_t result = wavetable_pool_candidate_allocate(
                job->wavetable_slot, job->path,
                samples / job->source_cycle_sample_count, candidate);
            if (result != WAVETABLE_RESULT_OK) wavetable_load_fail(result);
            else job->state = WAVETABLE_LOAD_DATA_SEEK;
            return;
        }
        case WAVETABLE_LOAD_DECODE:
        {
            uint32_t count = job->buffered_samples - job->converted_samples;
            if (count > 256U) count = 256U;
            const uint8_t *src = &g_wavetable_pool_io[
                job->converted_samples * job->wav_info.block_align];
            for (uint32_t i = 0U; i < count; ++i)
            {
                float left = 0.0f, right = 0.0f;
                wav_audio_codec_decode_stereo_frame(src, job->wav_info.channels,
                                                    job->wav_info.bits_per_sample,
                                                    &left, &right);
                const float mono = (job->wav_info.channels == 1U)
                    ? left : ((left + right) * 0.5f);
                if (job->source_cycle_sample_count == WAVETABLE_FRAME_SAMPLE_COUNT)
                {
                    candidate->data[job->io_offset
                        + job->converted_samples + i] =
                        wavetable_pool_finite_float(mono);
                }
                else
                {
                    wavetable_pool_source_2048_store(
                        job->source_cycle_offset++, mono);
                    if (job->source_cycle_offset
                            == WAVETABLE_SOURCE_2048_SAMPLE_COUNT)
                    {
                        wavetable_pool_resample_2048_cycle_frequency(
                            &candidate->data[job->cycle
                                * WAVETABLE_FRAME_SAMPLE_COUNT]);
                        job->cycle++;
                        job->source_cycle_offset = 0U;
                    }
                }
                src += job->wav_info.block_align;
            }
            job->converted_samples += count;
            if (job->converted_samples >= job->buffered_samples)
            {
                job->io_offset += job->buffered_samples;
                job->buffered_samples = 0U;
                job->converted_samples = 0U;
                job->state = (job->io_offset >= candidate->frame_count
                        * job->source_cycle_sample_count)
                    ? WAVETABLE_LOAD_MIP_INIT : WAVETABLE_LOAD_DATA_READ;
            }
            return;
        }
        case WAVETABLE_LOAD_MIP_INIT:
            wavetable_pool_candidate_mipmap_init(candidate);
            job->cycle = 0U;
            job->band = 0U;
            job->state = WAVETABLE_LOAD_MIP_FORWARD_PREP;
            return;
        case WAVETABLE_LOAD_MIP_FORWARD_PREP:
        {
            const float *src = &candidate->data[job->cycle * WAVETABLE_FRAME_SAMPLE_COUNT];
            for (uint32_t i = 0U; i < WAVETABLE_FRAME_SAMPLE_COUNT; ++i)
            {
                g_wavetable_fft_real[i] = src[i];
                g_wavetable_fft_imag[i] = 0.0f;
            }
            job->state = WAVETABLE_LOAD_MIP_FORWARD_FFT;
            return;
        }
        case WAVETABLE_LOAD_MIP_FORWARD_FFT:
            wavetable_pool_fft(g_wavetable_fft_real, g_wavetable_fft_imag,
                               WAVETABLE_FRAME_SAMPLE_COUNT, 0U);
            job->state = WAVETABLE_LOAD_MIP_BAND;
            return;
        case WAVETABLE_LOAD_MIP_BAND:
            wavetable_load_process_band(job);
            return;
        case WAVETABLE_LOAD_BASE_CRC:
        case WAVETABLE_LOAD_PAYLOAD_CRC:
        {
            const uint8_t *data = (job->state == WAVETABLE_LOAD_BASE_CRC)
                ? (const uint8_t *)candidate->data : (const uint8_t *)candidate->mipmap.data;
            const uint32_t total = (job->state == WAVETABLE_LOAD_BASE_CRC)
                ? candidate->data_bytes : candidate->mipmap.data_bytes;
            uint32_t chunk = total - job->io_offset;
            if (chunk > SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES) chunk = SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES;
            if (job->state == WAVETABLE_LOAD_BASE_CRC)
                job->base_crc = wavetable_pool_crc32_update(job->base_crc, &data[job->io_offset], chunk);
            else
                job->payload_crc = wavetable_pool_crc32_update(job->payload_crc, &data[job->io_offset], chunk);
            job->io_offset += chunk;
            if (job->io_offset >= total)
            {
                job->io_offset = 0U;
                job->state = (job->state == WAVETABLE_LOAD_BASE_CRC)
                    ? WAVETABLE_LOAD_PAYLOAD_CRC : WAVETABLE_LOAD_CACHE_DIR;
            }
            return;
        }
        case WAVETABLE_LOAD_PREVIEW_INIT:
            memset(&candidate->preview, 0, sizeof(candidate->preview));
            candidate->preview.state = WAVETABLE_PREVIEW_READY;
            candidate->preview.generation = candidate->generation;
            candidate->preview.frame_count = candidate->frame_count;
            candidate->preview.columns = WAVETABLE_PREVIEW_COLUMNS;
            for (uint16_t col = 0U; col < WAVETABLE_PREVIEW_COLUMNS; ++col)
            {
                candidate->preview.min[col] = 32767;
                candidate->preview.max[col] = -32768;
            }
            job->cycle = 0U;
            job->state = WAVETABLE_LOAD_PREVIEW_FRAME;
            return;
        case WAVETABLE_LOAD_PREVIEW_FRAME:
        {
            uint32_t col = (uint32_t)(((uint64_t)job->cycle * WAVETABLE_PREVIEW_COLUMNS)
                                      / candidate->frame_count);
            if (col >= WAVETABLE_PREVIEW_COLUMNS) col = WAVETABLE_PREVIEW_COLUMNS - 1U;
            const float *src = &candidate->data[job->cycle * WAVETABLE_FRAME_SAMPLE_COUNT];
            float min = 1.0f, max = -1.0f;
            for (uint32_t i = 0U; i < WAVETABLE_FRAME_SAMPLE_COUNT; ++i)
            {
                if (src[i] < min) min = src[i];
                if (src[i] > max) max = src[i];
            }
            const int16_t min_s16 = wavetable_pool_preview_float_to_i16(min);
            const int16_t max_s16 = wavetable_pool_preview_float_to_i16(max);
            if (min_s16 < candidate->preview.min[col]) candidate->preview.min[col] = min_s16;
            if (max_s16 > candidate->preview.max[col]) candidate->preview.max[col] = max_s16;
            const uint16_t min_peak = wavetable_pool_preview_abs_i16(min_s16);
            const uint16_t max_peak = wavetable_pool_preview_abs_i16(max_s16);
            if (min_peak > candidate->preview.global_peak) candidate->preview.global_peak = min_peak;
            if (max_peak > candidate->preview.global_peak) candidate->preview.global_peak = max_peak;
            job->cycle++;
            if (job->cycle >= candidate->frame_count)
            {
                for (uint16_t i = 0U; i < WAVETABLE_PREVIEW_COLUMNS; ++i)
                    if (candidate->preview.min[i] > candidate->preview.max[i])
                        candidate->preview.min[i] = candidate->preview.max[i] = 0;
                job->state = WAVETABLE_LOAD_PUBLISH;
            }
            return;
        }
        case WAVETABLE_LOAD_PUBLISH:
        {
            const wavetable_slot_state_t state =
                g_wavetable_pool.slots[job->wavetable_slot].state;
            if (state == WAVETABLE_SLOT_READY)
            {
                wavetable_pool_clear(job->wavetable_slot);
                job->state = WAVETABLE_LOAD_WAIT_RETIRE;
                return;
            }
            if (state == WAVETABLE_SLOT_RETIRING)
            {
                job->state = WAVETABLE_LOAD_WAIT_RETIRE;
                return;
            }
            candidate->generation = wavetable_pool_next_generation();
            candidate->preview.generation = candidate->generation;
            const wavetable_result_t result = wavetable_pool_commit_candidate(
                job->wavetable_slot, SAMPLE_GLOBAL_POOL_INVALID_INDEX,
                candidate, &job->global_slot);
            if (result != WAVETABLE_RESULT_OK) wavetable_load_fail(result);
            else
            {
                job->result = WAVETABLE_RESULT_OK;
                job->state = WAVETABLE_LOAD_DONE;
                wavetable_pool_set_last(WAVETABLE_RESULT_OK);
            }
            return;
        }
        case WAVETABLE_LOAD_WAIT_RETIRE:
            wavetable_pool_service_retire();
            if (g_wavetable_pool.slots[job->wavetable_slot].state
                == WAVETABLE_SLOT_EMPTY)
                job->state = WAVETABLE_LOAD_PUBLISH;
            return;
        default:
            break;
    }

    sd_scheduler_background_kind_t kind = SD_SCHEDULER_BACKGROUND_METADATA;
    uint32_t bytes = 0U;
    if ((job->state == WAVETABLE_LOAD_CRC_READ)
        || (job->state == WAVETABLE_LOAD_PARSE)
        || (job->state == WAVETABLE_LOAD_DATA_READ)
        || (job->state == WAVETABLE_LOAD_CACHE_HEADER)
        || (job->state == WAVETABLE_LOAD_CACHE_BAND)
        || (job->state == WAVETABLE_LOAD_CACHE_PAYLOAD))
    {
        kind = SD_SCHEDULER_BACKGROUND_DATA;
        if (job->state == WAVETABLE_LOAD_PARSE) bytes = SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES;
        else if (job->state == WAVETABLE_LOAD_CACHE_HEADER) bytes = WAVETABLE_PREPARED_HEADER_SIZE;
        else if (job->state == WAVETABLE_LOAD_CACHE_BAND) bytes = WAVETABLE_PREPARED_BAND_ENTRY_SIZE;
        else
        {
            uint32_t remaining = 0U;
            if (job->state == WAVETABLE_LOAD_CRC_READ)
                remaining = (uint32_t)job->source_info.fsize - job->io_offset;
            else if (job->state == WAVETABLE_LOAD_DATA_READ)
                remaining = (candidate->frame_count * job->source_cycle_sample_count - job->io_offset)
                            * job->wav_info.block_align;
            else remaining = candidate->mipmap.data_bytes - job->io_offset;
            bytes = (remaining > SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES)
                ? SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES : remaining;
        }
    }
    const sd_scheduler_background_admission_t admission = wavetable_load_sd_begin(kind, bytes);
    if (admission == SD_SCHEDULER_BACKGROUND_NOT_NOW) return;
    if (admission != SD_SCHEDULER_BACKGROUND_GO)
    {
        job->source_open = 0U;
        job->cache_open = 0U;
        wavetable_load_fail(WAVETABLE_RESULT_SD_MOUNT_FAIL);
        wavetable_pool_candidate_release(candidate);
        job->state = WAVETABLE_LOAD_DONE;
        return;
    }

    switch (job->state)
    {
        case WAVETABLE_LOAD_MOUNT:
            if (sd_access_fs_mount_if_needed() == 0U) wavetable_load_fail(WAVETABLE_RESULT_SD_MOUNT_FAIL);
            else job->state = WAVETABLE_LOAD_STAT;
            break;
        case WAVETABLE_LOAD_STAT:
            if (f_stat(job->path, &job->source_info) != FR_OK) wavetable_load_fail(WAVETABLE_RESULT_OPEN_FAIL);
            else job->state = WAVETABLE_LOAD_OPEN;
            break;
        case WAVETABLE_LOAD_OPEN:
            if (f_open(&job->source, job->path, FA_READ) != FR_OK) wavetable_load_fail(WAVETABLE_RESULT_OPEN_FAIL);
            else { job->source_open = 1U; job->state = WAVETABLE_LOAD_CRC_SEEK; }
            break;
        case WAVETABLE_LOAD_CRC_SEEK:
            if (f_lseek(&job->source, 0U) != FR_OK) wavetable_load_fail(WAVETABLE_RESULT_READ_FAIL);
            else { job->io_offset = 0U; job->source_crc = 0U; job->state = WAVETABLE_LOAD_CRC_READ; }
            break;
        case WAVETABLE_LOAD_CRC_READ:
        {
            UINT read = 0U;
            if ((f_read(&job->source, g_wavetable_pool_io, bytes, &read) != FR_OK) || (read != bytes))
                wavetable_load_fail(WAVETABLE_RESULT_READ_FAIL);
            else
            {
                job->source_crc = wavetable_pool_crc32_update(job->source_crc, g_wavetable_pool_io, read);
                job->io_offset += read;
                if (job->io_offset >= (uint32_t)job->source_info.fsize) job->state = WAVETABLE_LOAD_PARSE;
            }
            break;
        }
        case WAVETABLE_LOAD_PARSE:
            if ((wav_parser_parse_info(&job->source, &job->wav_info) == 0)
                || (wavetable_pool_wav_info_valid(&job->wav_info) == 0U))
                wavetable_load_fail(WAVETABLE_RESULT_UNSUPPORTED);
            else job->state = WAVETABLE_LOAD_ALLOCATE;
            break;
        case WAVETABLE_LOAD_DATA_SEEK:
            if (f_lseek(&job->source, job->wav_info.data_offset) != FR_OK) wavetable_load_fail(WAVETABLE_RESULT_READ_FAIL);
            else { job->io_offset = 0U; job->state = WAVETABLE_LOAD_DATA_READ; }
            break;
        case WAVETABLE_LOAD_DATA_READ:
        {
            uint32_t samples = bytes / job->wav_info.block_align;
            UINT wanted = (UINT)(samples * job->wav_info.block_align), read = 0U;
            if ((f_read(&job->source, g_wavetable_pool_io, wanted, &read) != FR_OK) || (read != wanted))
                wavetable_load_fail(WAVETABLE_RESULT_READ_FAIL);
            else { job->buffered_samples = samples; job->converted_samples = 0U; job->state = WAVETABLE_LOAD_DECODE; }
            break;
        }
        case WAVETABLE_LOAD_CACHE_DIR:
            (void)f_mkdir("0:/WAVETABLES");
            job->state = WAVETABLE_LOAD_CACHE_SUBDIR;
            break;
        case WAVETABLE_LOAD_CACHE_SUBDIR:
            (void)f_mkdir(WAVETABLE_POOL_CACHE_DIR);
            if ((wavetable_pool_make_cache_path(job->cache_path, sizeof(job->cache_path),
                                                job->path, &job->source_info) == 0U)
                || (wavetable_pool_make_temp_path(job->temp_path, sizeof(job->temp_path),
                                                  job->cache_path) == 0U))
                wavetable_load_fail(WAVETABLE_RESULT_PATH_TOO_LONG);
            else job->state = WAVETABLE_LOAD_CACHE_CLEAN;
            break;
        case WAVETABLE_LOAD_CACHE_CLEAN:
            (void)f_unlink(job->temp_path);
            job->state = WAVETABLE_LOAD_CACHE_OPEN;
            break;
        case WAVETABLE_LOAD_CACHE_OPEN:
            if (f_open(&job->cache, job->temp_path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
                wavetable_load_fail(WAVETABLE_RESULT_WRITE_FAIL);
            else { job->cache_open = 1U; job->state = WAVETABLE_LOAD_CACHE_HEADER; }
            break;
        case WAVETABLE_LOAD_CACHE_HEADER:
        {
            wavetable_pool_encode_prepared_header(g_wavetable_pool_io, candidate,
                                                  &job->source_info, job->source_crc,
                                                  job->base_crc, job->payload_crc);
            UINT written = 0U;
            if ((f_write(&job->cache, g_wavetable_pool_io, bytes, &written) != FR_OK) || (written != bytes))
                wavetable_load_fail(WAVETABLE_RESULT_WRITE_FAIL);
            else { job->band = 0U; job->io_offset = 0U; job->state = WAVETABLE_LOAD_CACHE_BAND; }
            break;
        }
        case WAVETABLE_LOAD_CACHE_BAND:
        {
            const wavetable_mipmap_band_t *band = &candidate->mipmap.bands[job->band];
            wavetable_pool_encode_prepared_band(g_wavetable_pool_io, band, job->io_offset);
            UINT written = 0U;
            if ((f_write(&job->cache, g_wavetable_pool_io, bytes, &written) != FR_OK) || (written != bytes))
                wavetable_load_fail(WAVETABLE_RESULT_WRITE_FAIL);
            else
            {
                job->io_offset += band->sample_count * sizeof(float);
                job->band++;
                if (job->band >= candidate->mipmap.band_count) { job->io_offset = 0U; job->state = WAVETABLE_LOAD_CACHE_PAYLOAD; }
            }
            break;
        }
        case WAVETABLE_LOAD_CACHE_PAYLOAD:
        {
            UINT written = 0U;
            const uint8_t *src = (const uint8_t *)candidate->mipmap.data;
            if ((f_write(&job->cache, &src[job->io_offset], bytes, &written) != FR_OK) || (written != bytes))
                wavetable_load_fail(WAVETABLE_RESULT_WRITE_FAIL);
            else { job->io_offset += bytes; if (job->io_offset >= candidate->mipmap.data_bytes) job->state = WAVETABLE_LOAD_CACHE_SYNC; }
            break;
        }
        case WAVETABLE_LOAD_CACHE_SYNC:
            if (f_sync(&job->cache) != FR_OK) wavetable_load_fail(WAVETABLE_RESULT_WRITE_FAIL);
            else job->state = WAVETABLE_LOAD_CACHE_CLOSE;
            break;
        case WAVETABLE_LOAD_CACHE_CLOSE:
            if (f_close(&job->cache) != FR_OK) wavetable_load_fail(WAVETABLE_RESULT_WRITE_FAIL);
            else { job->cache_open = 0U; job->state = WAVETABLE_LOAD_CACHE_FINAL_CLEAN; }
            break;
        case WAVETABLE_LOAD_CACHE_FINAL_CLEAN:
            (void)f_unlink(job->cache_path);
            job->state = WAVETABLE_LOAD_CACHE_RENAME;
            break;
        case WAVETABLE_LOAD_CACHE_RENAME:
            if (f_rename(job->temp_path, job->cache_path) != FR_OK) wavetable_load_fail(WAVETABLE_RESULT_WRITE_FAIL);
            else job->state = WAVETABLE_LOAD_SOURCE_CLOSE;
            break;
        case WAVETABLE_LOAD_SOURCE_CLOSE:
            if (f_close(&job->source) != FR_OK) wavetable_load_fail(WAVETABLE_RESULT_READ_FAIL);
            else { job->source_open = 0U; job->state = WAVETABLE_LOAD_PREVIEW_INIT; }
            break;
        case WAVETABLE_LOAD_CLEANUP:
            if ((job->cleanup_phase == 0U) && (job->cache_open != 0U))
            { (void)f_close(&job->cache); job->cache_open = 0U; job->cleanup_phase++; }
            else if ((job->cleanup_phase <= 1U) && (job->source_open != 0U))
            { (void)f_close(&job->source); job->source_open = 0U; job->cleanup_phase = 2U; }
            else if ((job->cleanup_phase <= 2U) && (job->temp_path[0] != '\0'))
            { (void)f_unlink(job->temp_path); job->cleanup_phase = 3U; }
            else
            { wavetable_pool_candidate_release(candidate); job->state = WAVETABLE_LOAD_DONE; }
            break;
        default:
            break;
    }
    sd_scheduler_runtime_background_end();
}

uint8_t wavetable_pool_load_async_take_result(wavetable_result_t *out_result,
                                              uint16_t *out_wavetable_slot,
                                              uint16_t *out_global_slot,
                                              const char **out_path)
{
    wavetable_load_job_t *const job = &g_wavetable_load_job;
    if (job->state != WAVETABLE_LOAD_DONE) return 0U;
    if (out_result != 0) *out_result = job->result;
    if (out_wavetable_slot != 0) *out_wavetable_slot = job->wavetable_slot;
    if (out_global_slot != 0) *out_global_slot = job->global_slot;
    if (out_path != 0) *out_path = job->path;
    job->state = WAVETABLE_LOAD_IDLE;
    return 1U;
}

static wavetable_result_t wavetable_pool_load_wav_transactional(
    uint16_t wavetable_slot,
    uint16_t forced_global_slot,
    const char *path,
    wavetable_source_geometry_t source_geometry,
    uint16_t *out_global_slot)
{
    FIL *const source_fp = &g_wavetable_transaction_files[0];
    FILINFO source_info;
    wav_info_t wav_info;
    wavetable_slot_t *const candidate = &g_wavetable_candidate;
    wavetable_pool_candidate_init(candidate);
    if (out_global_slot != 0)
    {
        *out_global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }
    if ((wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
        || ((forced_global_slot != SAMPLE_GLOBAL_POOL_INVALID_INDEX)
            && (forced_global_slot >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS))
        || (path == 0) || (path[0] == '\0')
        || ((source_geometry != WAVETABLE_SOURCE_GEOMETRY_1024)
            && (source_geometry != WAVETABLE_SOURCE_GEOMETRY_2048)))
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_INVALID_ARG);
        return WAVETABLE_RESULT_INVALID_ARG;
    }
    if (sample_global_pool_validate_entries(SAMPLE_GLOBAL_KIND_WAVETABLE,
                                            wavetable_slot,
                                            1U) == 0U)
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_GLOBAL_SLOT_FULL);
        return WAVETABLE_RESULT_GLOBAL_SLOT_FULL;
    }

    char *const cache_path = g_wavetable_transaction_paths[0];
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_SD_BUSY);
        return WAVETABLE_RESULT_SD_BUSY;
    }
    wavetable_result_t result = WAVETABLE_RESULT_OK;
    uint8_t source_open = 0U;
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        result = WAVETABLE_RESULT_SD_MOUNT_FAIL;
        goto done;
    }
    if ((f_stat(path, &source_info) != FR_OK)
        || (f_open(source_fp, path, FA_READ) != FR_OK))
    {
        result = WAVETABLE_RESULT_OPEN_FAIL;
        goto done;
    }
    source_open = 1U;

    uint32_t source_crc32 = 0U;
    if (wavetable_pool_source_crc32(source_fp, &source_crc32) == 0U)
    {
        result = WAVETABLE_RESULT_READ_FAIL;
        goto done;
    }
    if ((wav_parser_parse_info(source_fp, &wav_info) == 0)
        || (wavetable_pool_wav_info_valid(&wav_info) == 0U))
    {
        result = WAVETABLE_RESULT_UNSUPPORTED;
        goto done;
    }
    const uint32_t source_samples = wav_info.data_size / wav_info.block_align;
    const uint32_t source_cycle_sample_count =
        wavetable_pool_source_cycle_sample_count(source_geometry);
    if ((source_samples % source_cycle_sample_count) != 0U)
    {
        result = WAVETABLE_RESULT_UNSUPPORTED;
        goto done;
    }
    result = wavetable_pool_candidate_allocate(
        wavetable_slot,
        path,
        source_samples / source_cycle_sample_count,
        candidate);
    if (result != WAVETABLE_RESULT_OK)
    {
        goto done;
    }
    if (wavetable_pool_make_cache_path(cache_path,
                                       WAVETABLE_POOL_PATH_MAX,
                                       path,
                                       &source_info) == 0U)
    {
        result = WAVETABLE_RESULT_PATH_TOO_LONG;
        goto done;
    }
    char *const temp_path = g_wavetable_transaction_paths[1];
    if (wavetable_pool_make_temp_path(temp_path,
                                      WAVETABLE_POOL_PATH_MAX,
                                      cache_path) == 0U)
    {
        result = WAVETABLE_RESULT_PATH_TOO_LONG;
        goto done;
    }
    (void)f_unlink(temp_path);

    if (wavetable_pool_load_prepared_cache(cache_path,
                                           path,
                                           &source_info,
                                           source_crc32,
                                           candidate) == 0U)
    {
        (void)f_unlink(cache_path);
        result = wavetable_pool_candidate_decode_wav(source_fp,
                                                     &wav_info,
                                                     source_geometry,
                                                     candidate);
        if (result != WAVETABLE_RESULT_OK)
        {
            goto done;
        }
        const uint32_t base_crc32 =
            wavetable_pool_crc32_memory(candidate->data, candidate->data_bytes);
        const uint32_t payload_crc32 =
            wavetable_pool_crc32_memory(candidate->mipmap.data,
                                        candidate->mipmap.data_bytes);
        if (wavetable_pool_write_cache_transactional(cache_path,
                                                     candidate,
                                                     &source_info,
                                                     source_crc32,
                                                     base_crc32,
                                                     payload_crc32) == 0U)
        {
            result = WAVETABLE_RESULT_WRITE_FAIL;
            goto done;
        }
    }

done:
    if (source_open != 0U)
    {
        (void)f_close(source_fp);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
    if (result == WAVETABLE_RESULT_OK)
    {
        result = wavetable_pool_commit_candidate(wavetable_slot,
                                                 forced_global_slot,
                                                 candidate,
                                                 out_global_slot);
    }
    if (result != WAVETABLE_RESULT_OK)
    {
        wavetable_pool_candidate_release(candidate);
    }
    wavetable_pool_set_last(result);
    return result;
}

wavetable_result_t wavetable_pool_load_wav(uint16_t wavetable_slot,
                                           const char *path,
                                           uint16_t *out_global_slot)
{
    return wavetable_pool_load_wav_with_geometry(
        wavetable_slot,
        path,
        WAVETABLE_DEFAULT_SOURCE_GEOMETRY,
        out_global_slot);
}

wavetable_result_t wavetable_pool_load_wav_with_geometry(
    uint16_t wavetable_slot,
    const char *path,
    wavetable_source_geometry_t source_geometry,
    uint16_t *out_global_slot)
{
    return wavetable_pool_load_wav_transactional(
        wavetable_slot,
        SAMPLE_GLOBAL_POOL_INVALID_INDEX,
        path,
        source_geometry,
        out_global_slot);
}

wavetable_result_t wavetable_pool_load_file_at(uint16_t wavetable_slot,
                                               uint16_t global_slot,
                                               const char *path)
{
    if (global_slot >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_INVALID_ARG);
        return WAVETABLE_RESULT_INVALID_ARG;
    }
    if (wavetable_pool_path_ext_is_wav(path) != 0U)
    {
        uint16_t loaded_global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        const wavetable_result_t result =
            wavetable_pool_load_wav_transactional(wavetable_slot,
                                                  global_slot,
                                                  path,
                                                  WAVETABLE_DEFAULT_SOURCE_GEOMETRY,
                                                  &loaded_global);
        if ((result != WAVETABLE_RESULT_OK)
            && (wavetable_slot < WAVETABLE_POOL_MAX_SLOTS)
            && (g_wavetable_pool.slots[wavetable_slot].state
                != WAVETABLE_SLOT_READY)
            && (path != 0)
            && (path[0] != '\0'))
        {
            (void)wavetable_pool_copy_path(g_wavetable_pool.slots[wavetable_slot].path,
                                           sizeof(g_wavetable_pool.slots[wavetable_slot].path),
                                           path);
            wavetable_pool_slot_error_at(wavetable_slot, result, global_slot);
        }
        return result;
    }

    const wavetable_result_t result = WAVETABLE_RESULT_UNSUPPORTED;
    if ((result != WAVETABLE_RESULT_OK)
        && (wavetable_slot < WAVETABLE_POOL_MAX_SLOTS)
        && (path != 0)
        && (path[0] != '\0'))
    {
        (void)wavetable_pool_copy_path(g_wavetable_pool.slots[wavetable_slot].path,
                                       sizeof(g_wavetable_pool.slots[wavetable_slot].path),
                                       path);
        wavetable_pool_slot_error_at(wavetable_slot, result, global_slot);
    }
    return result;
}

static void wavetable_pool_finalize_clear(uint16_t wavetable_slot)
{
    if (wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
    {
        return;
    }

    wavetable_slot_t *const slot = &g_wavetable_pool.slots[wavetable_slot];
    const wavetable_slot_t old = *slot;
    const uint32_t generation = wavetable_pool_next_generation();
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    slot->state = WAVETABLE_SLOT_EMPTY;
    slot->generation = generation;
    __DMB();
    if (primask == 0U)
    {
        __enable_irq();
    }
    if (old.page_count != 0U)
    {
        sample_page_cache_port_release_shared(old.first_page_slot,
                                                       old.page_count);
    }
    if (old.mipmap.page_count != 0U)
    {
        sample_page_cache_port_release_shared(old.mipmap.first_page_slot,
                                                       old.mipmap.page_count);
    }
    sample_global_pool_clear_backend(SAMPLE_GLOBAL_KIND_WAVETABLE, wavetable_slot);
    memset(&g_wavetable_pool.slots[wavetable_slot], 0, sizeof(g_wavetable_pool.slots[wavetable_slot]));
    g_wavetable_pool.slots[wavetable_slot].state = WAVETABLE_SLOT_EMPTY;
    g_wavetable_pool.slots[wavetable_slot].global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    g_wavetable_pool.slots[wavetable_slot].first_page_slot = UINT16_MAX;
    g_wavetable_pool.slots[wavetable_slot].mipmap.first_page_slot = UINT16_MAX;
    g_wavetable_pool.slots[wavetable_slot].generation = generation;
    wavetable_pool_preview_clear(&g_wavetable_pool.slots[wavetable_slot].preview);
}

void wavetable_pool_clear(uint16_t wavetable_slot)
{
    if (wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS) return;
    wavetable_slot_t *const slot = &g_wavetable_pool.slots[wavetable_slot];
    if (slot->state == WAVETABLE_SLOT_RETIRING) return;
    if (slot->state != WAVETABLE_SLOT_READY)
    {
        wavetable_pool_finalize_clear(wavetable_slot);
        return;
    }
    uint64_t due_sample = 0U;
    if (!live_clock_read_audio_sample(&due_sample)) return;
    const uint32_t generation = slot->generation;
    if (control_audio_publish_param_fenced((uint8_t)wavetable_slot, 0xFFF7U,
                                           generation, 0U, due_sample,
                                           &g_wavetable_retire_fence[wavetable_slot]) == 0U)
        return;
    audio_wave_table_projection_withdraw_slot(wavetable_slot, generation);
    slot->state = WAVETABLE_SLOT_RETIRING;
    __DMB();
}

void wavetable_pool_service_retire(void)
{
    for (uint16_t i = 0U; i < WAVETABLE_POOL_MAX_SLOTS; ++i)
    {
        wavetable_slot_t *const slot = &g_wavetable_pool.slots[i];
        if ((slot->state == WAVETABLE_SLOT_RETIRING)
            && (control_audio_consumer_fence_consumed(
                    g_wavetable_retire_fence[i]) != 0U))
        {
            g_wavetable_retire_fence[i] = 0U;
            wavetable_pool_finalize_clear(i);
        }
    }
}

const wavetable_slot_t *wavetable_pool_get_slot(uint16_t wavetable_slot)
{
    if (wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
    {
        return 0;
    }
    return &g_wavetable_pool.slots[wavetable_slot];
}

wavetable_slot_state_t wavetable_pool_get_state(uint16_t wavetable_slot)
{
    const wavetable_slot_t *const slot = wavetable_pool_get_slot(wavetable_slot);
    return (slot != 0) ? slot->state : WAVETABLE_SLOT_ERROR;
}

const float *wavetable_pool_get_data(uint16_t wavetable_slot)
{
    const wavetable_slot_t *const slot = wavetable_pool_get_slot(wavetable_slot);
    return ((slot != 0) && (slot->state == WAVETABLE_SLOT_READY)) ? slot->data : 0;
}

const wavetable_mipmap_view_t *wavetable_pool_get_mipmap_view(uint16_t wavetable_slot)
{
    const wavetable_slot_t *const slot = wavetable_pool_get_slot(wavetable_slot);
    if ((slot == 0) || (slot->state != WAVETABLE_SLOT_READY)
        || (slot->mipmap.band_count == 0U) || (slot->mipmap.data == 0))
    {
        return 0;
    }
    return &slot->mipmap;
}

const wavetable_preview_t *wavetable_pool_get_preview(uint16_t wavetable_slot)
{
    const wavetable_slot_t *const slot = wavetable_pool_get_slot(wavetable_slot);
    if ((slot == 0)
            || (slot->state != WAVETABLE_SLOT_READY)
            || (slot->preview.state != WAVETABLE_PREVIEW_READY)
            || (slot->preview.generation != slot->generation))
    {
        return 0;
    }
    return &slot->preview;
}

const wavetable_preview_t *wavetable_pool_get_preview_for_global(uint16_t global_slot)
{
    uint16_t wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    if (sample_global_pool_resolve_backend(global_slot,
                                           SAMPLE_GLOBAL_KIND_WAVETABLE,
                                           &wavetable_slot) == 0U)
    {
        return 0;
    }
    return wavetable_pool_get_preview(wavetable_slot);
}

uint32_t wavetable_pool_get_used_bytes(void)
{
    const uint32_t total = sample_page_cache_port_shared_total_bytes();
    const uint32_t free_bytes = sample_page_cache_port_shared_free_bytes();
    return (free_bytes >= total) ? 0U : (total - free_bytes);
}

uint32_t wavetable_pool_get_free_bytes(void)
{
    return sample_page_cache_port_shared_free_bytes();
}

wavetable_result_t wavetable_pool_get_last_result(void)
{
    return g_wavetable_pool.last_result;
}

const char *wavetable_pool_result_label(wavetable_result_t result)
{
    switch (result)
    {
        case WAVETABLE_RESULT_OK:
            return "LOAD OK";
        case WAVETABLE_RESULT_POOL_FULL:
        case WAVETABLE_RESULT_GLOBAL_SLOT_FULL:
            return "SLOT FULL";
        case WAVETABLE_RESULT_GLOBAL_BUDGET_FULL:
        case WAVETABLE_RESULT_RAM_POOL_FULL:
            return "MEM FULL";
        case WAVETABLE_RESULT_TOO_LARGE:
            return "TOO LARGE";
        case WAVETABLE_RESULT_PATH_TOO_LONG:
            return "PATH LONG";
        case WAVETABLE_RESULT_SD_BUSY:
            return "SD BUSY";
        case WAVETABLE_RESULT_SD_MOUNT_FAIL:
            return "SD UNAVAILABLE";
        case WAVETABLE_RESULT_OPEN_FAIL:
            return "OPEN FAIL";
        case WAVETABLE_RESULT_BAD_FILE:
            return "BAD WT";
        case WAVETABLE_RESULT_UNSUPPORTED:
            return "UNSUPPORTED";
        case WAVETABLE_RESULT_READ_FAIL:
            return "SD READ FAIL";
        case WAVETABLE_RESULT_WRITE_FAIL:
            return "SD WRITE FAIL";
        case WAVETABLE_RESULT_REGISTER_FAIL:
            return "REGISTER FAIL";
        case WAVETABLE_RESULT_INVALID_ARG:
        default:
            return "LOAD FAIL";
    }
}
