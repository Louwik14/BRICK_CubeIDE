#include "sd_multitrack_recorder.h"

#include <stdio.h>
#include <string.h>

#include "audio_float.h"
#include "Storage/memory_layout.h"
#include "ff.h"
#include "stm32h7xx_hal.h"

typedef enum
{
    CMD_NONE = 0,
    CMD_START,
    CMD_STOP,
    CMD_ARM_STEM,
    CMD_DISARM_STEM
} recorder_cmd_type_t;

typedef struct
{
    recorder_cmd_type_t type;
    uint8_t stem_id;
    sd_recorder_stem_cfg_t cfg;
} recorder_cmd_t;

typedef struct
{
    volatile uint32_t write_idx;
    volatile uint32_t read_idx;
    uint32_t slot_count;
    uint32_t slot_words;
    float *data;
} recorder_ring_t;

typedef struct
{
    uint8_t armed;
    sd_recorder_stem_cfg_t cfg;
    recorder_ring_t ring;
    uint32_t blocks_captured;
    uint32_t blocks_dropped_overflow;
    uint32_t ring_high_watermark;
    uint32_t bytes_written;
    uint32_t write_calls;
    uint32_t last_write_error;
    FIL file;
    uint8_t file_open;
    uint32_t data_bytes;
    uint32_t wav_limit_bytes;
} recorder_stem_t;

typedef struct
{
    uint8_t riff[4];
    uint32_t riff_size;
    uint8_t wave[4];
    uint8_t fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint8_t data[4];
    uint32_t data_size;
} recorder_wav_header_t;

#define REC_CMD_Q_LEN 16U
#define REC_RING_SLOT_COUNT 128U
#define REC_RING_MAX_SLOT_WORDS (AUDIO_BLOCK_SIZE * 2U)
#define REC_SAMPLE_RATE 48000U
#define REC_BITS_PER_SAMPLE 24U
#define REC_WAV_MAX_DATA_BYTES (0xFFFFFFFFUL - 44UL)
#define REC_DEFAULT_WRITER_BUDGET_BYTES (32U * 1024U)
#define REC_FILE_NAME_LEN 64U

typedef struct
{
    recorder_cmd_t q[REC_CMD_Q_LEN];
    volatile uint32_t write_idx;
    volatile uint32_t read_idx;

    sd_recorder_state_t state;
    recorder_stem_t stems[SD_RECORDER_MAX_STEMS];

    uint32_t start_requests;
    uint32_t stop_requests;
    uint32_t arm_requests;
    uint32_t disarm_requests;
    uint32_t rejected_config_changes;
    uint32_t rejected_state_requests;
    uint32_t block_boundary_calls;
    uint32_t transition_count;

    uint32_t writer_calls;
    uint32_t writer_bytes_total;

    FATFS fs;
    uint8_t fs_mounted;
    uint8_t files_prepared;
    uint32_t session_index;
    uint32_t writer_budget_bytes;
} recorder_ctx_t;

static recorder_ctx_t g_rec;

static AUDIO_COLD_SDRAM float g_rec_ring_storage[SD_RECORDER_MAX_STEMS][REC_RING_SLOT_COUNT][REC_RING_MAX_SLOT_WORDS];
static ALIGN32 float g_writer_float_block[REC_RING_MAX_SLOT_WORDS];
static ALIGN32 uint8_t g_writer_pcm24_block[REC_RING_MAX_SLOT_WORDS * 3U];

_Static_assert(sizeof(recorder_wav_header_t) == 44U, "WAV header must be 44 bytes");

static uint32_t recorder_ring_count(const recorder_ring_t *ring)
{
    const uint32_t wr = ring->write_idx;
    const uint32_t rd = ring->read_idx;
    return (wr >= rd) ? (wr - rd) : (ring->slot_count - (rd - wr));
}

static void recorder_ring_reset(recorder_ring_t *ring, uint32_t channels)
{
    ring->write_idx = 0U;
    ring->read_idx = 0U;
    ring->slot_count = REC_RING_SLOT_COUNT;
    ring->slot_words = AUDIO_BLOCK_SIZE * ((channels == 1U) ? 1U : 2U);
}

static uint8_t recorder_ring_push_block(recorder_stem_t *stem,
                                        const float *src_l,
                                        const float *src_r,
                                        uint32_t frames)
{
    recorder_ring_t *ring = &stem->ring;
    const uint32_t next = (ring->write_idx + 1U) % ring->slot_count;

    if(next == ring->read_idx)
    {
        stem->blocks_dropped_overflow++;
        return 0U;
    }

    float *dst = &ring->data[ring->write_idx * ring->slot_words];
    const uint32_t channels = stem->cfg.channels;

    if(channels == 1U)
    {
        memcpy(dst, src_l, sizeof(float) * frames);
    }
    else
    {
        for(uint32_t i = 0U; i < frames; i++)
        {
            const uint32_t o = i * 2U;
            dst[o] = src_l[i];
            dst[o + 1U] = src_r[i];
        }
    }

    __DMB();
    ring->write_idx = next;

    const uint32_t fill = recorder_ring_count(ring);
    if(fill > stem->ring_high_watermark)
    {
        stem->ring_high_watermark = fill;
    }

    stem->blocks_captured++;
    return 1U;
}

static uint8_t recorder_ring_pop_block(recorder_stem_t *stem,
                                       float *dst_interleaved,
                                       uint32_t dst_words)
{
    recorder_ring_t *ring = &stem->ring;

    if(ring->read_idx == ring->write_idx)
        return 0U;

    if((dst_interleaved == 0) || (dst_words < ring->slot_words))
        return 0U;

    const float *src = &ring->data[ring->read_idx * ring->slot_words];
    memcpy(dst_interleaved, src, sizeof(float) * ring->slot_words);

    __DMB();
    ring->read_idx = (ring->read_idx + 1U) % ring->slot_count;
    return 1U;
}

static uint8_t recorder_cmd_push(const recorder_cmd_t *cmd)
{
    if(cmd == 0)
        return 0U;

    uint8_t accepted = 0U;

    __disable_irq();

    const uint32_t next = (g_rec.write_idx + 1U) & (REC_CMD_Q_LEN - 1U);
    if(next != g_rec.read_idx)
    {
        g_rec.q[g_rec.write_idx] = *cmd;
        __DMB();
        g_rec.write_idx = next;
        accepted = 1U;
    }

    __enable_irq();

    return accepted;
}

static uint8_t recorder_cmd_pop(recorder_cmd_t *cmd)
{
    if(cmd == 0)
        return 0U;

    if(g_rec.read_idx == g_rec.write_idx)
        return 0U;

    *cmd = g_rec.q[g_rec.read_idx];
    g_rec.read_idx = (g_rec.read_idx + 1U) & (REC_CMD_Q_LEN - 1U);
    return 1U;
}

static void recorder_set_state(sd_recorder_state_t next)
{
    if(g_rec.state != next)
    {
        g_rec.state = next;
        g_rec.transition_count++;
    }
}

static uint8_t recorder_has_armed_stems(void)
{
    for(uint32_t i = 0U; i < SD_RECORDER_MAX_STEMS; i++)
    {
        if(g_rec.stems[i].armed != 0U)
            return 1U;
    }
    return 0U;
}

static uint32_t recorder_count_armed_stems(void)
{
    uint32_t c = 0U;
    for(uint32_t i = 0U; i < SD_RECORDER_MAX_STEMS; i++)
    {
        if(g_rec.stems[i].armed != 0U)
            c++;
    }
    return c;
}

static void recorder_pcm24_pack(const float *src, uint32_t words, uint8_t *dst)
{
    for(uint32_t i = 0U; i < words; i++)
    {
        float x = src[i];
        if(x > 1.0f)
            x = 1.0f;
        else if(x < -1.0f)
            x = -1.0f;

        int32_t v = (int32_t)(x * 8388607.0f);
        if(v > 8388607)
            v = 8388607;
        else if(v < -8388608)
            v = -8388608;

        dst[i * 3U] = (uint8_t)(v & 0xFF);
        dst[(i * 3U) + 1U] = (uint8_t)((v >> 8) & 0xFF);
        dst[(i * 3U) + 2U] = (uint8_t)((v >> 16) & 0xFF);
    }
}

static void recorder_build_wav_header(recorder_wav_header_t *hdr,
                                      uint16_t channels,
                                      uint32_t data_bytes)
{
    const uint32_t block_align = (uint32_t)channels * (REC_BITS_PER_SAMPLE / 8U);

    memcpy(hdr->riff, "RIFF", 4U);
    hdr->riff_size = 36U + data_bytes;
    memcpy(hdr->wave, "WAVE", 4U);
    memcpy(hdr->fmt, "fmt ", 4U);
    hdr->fmt_size = 16U;
    hdr->audio_format = 1U;
    hdr->num_channels = channels;
    hdr->sample_rate = REC_SAMPLE_RATE;
    hdr->byte_rate = REC_SAMPLE_RATE * block_align;
    hdr->block_align = (uint16_t)block_align;
    hdr->bits_per_sample = REC_BITS_PER_SAMPLE;
    memcpy(hdr->data, "data", 4U);
    hdr->data_size = data_bytes;
}

static uint8_t recorder_mount_fs(void)
{
    if(g_rec.fs_mounted != 0U)
        return 1U;

    if(f_mount(&g_rec.fs, "0:", 1U) != FR_OK)
        return 0U;

    g_rec.fs_mounted = 1U;
    return 1U;
}

static uint8_t recorder_get_free_bytes(uint64_t *out_free_bytes)
{
    if(out_free_bytes == 0)
        return 0U;

    DWORD free_clust = 0U;
    FATFS *fs_ptr = 0;

    if(f_getfree("0:", &free_clust, &fs_ptr) != FR_OK)
        return 0U;

    if((fs_ptr == 0) || (fs_ptr->csize == 0U))
        return 0U;

    *out_free_bytes = (uint64_t)free_clust * (uint64_t)fs_ptr->csize * 512ULL;
    return 1U;
}

static uint8_t recorder_prepare_files(void)
{
    if(g_rec.files_prepared != 0U)
        return 1U;

    if(!recorder_has_armed_stems())
        return 0U;

    if(!recorder_mount_fs())
        return 0U;

    uint64_t free_bytes = 0U;
    if(!recorder_get_free_bytes(&free_bytes))
        return 0U;

    uint64_t usable = (free_bytes * 9ULL) / 10ULL;
    const uint32_t active_stems = recorder_count_armed_stems();
    if(active_stems == 0U)
        return 0U;

    uint64_t per_stem_prealloc = usable / (uint64_t)active_stems;
    if(per_stem_prealloc > REC_WAV_MAX_DATA_BYTES)
        per_stem_prealloc = REC_WAV_MAX_DATA_BYTES;

    g_rec.session_index++;

    for(uint32_t stem_id = 0U; stem_id < SD_RECORDER_MAX_STEMS; stem_id++)
    {
        recorder_stem_t *stem = &g_rec.stems[stem_id];

        stem->bytes_written = 0U;
        stem->write_calls = 0U;
        stem->last_write_error = FR_OK;
        stem->file_open = 0U;
        stem->data_bytes = 0U;
        stem->wav_limit_bytes = (uint32_t)per_stem_prealloc;

        if(stem->armed == 0U)
            continue;

        char path[REC_FILE_NAME_LEN];
        (void)snprintf(path,
                       sizeof(path),
                       "0:/REC_%06lu_ST%u.wav",
                       (unsigned long)g_rec.session_index,
                       (unsigned int)stem_id);

        const FRESULT fr_open = f_open(&stem->file,
                                       path,
                                       FA_CREATE_ALWAYS | FA_WRITE | FA_READ);
        if(fr_open != FR_OK)
        {
            stem->last_write_error = (uint32_t)fr_open;
            recorder_set_state(SD_RECORDER_STATE_ERROR);
            return 0U;
        }

        stem->file_open = 1U;

        recorder_wav_header_t hdr;
        recorder_build_wav_header(&hdr, (uint16_t)stem->cfg.channels, 0U);

        UINT bw = 0U;
        const FRESULT fr_wh = f_write(&stem->file, &hdr, sizeof(hdr), &bw);
        if((fr_wh != FR_OK) || (bw != sizeof(hdr)))
        {
            stem->last_write_error = (uint32_t)((fr_wh != FR_OK) ? fr_wh : FR_DISK_ERR);
            recorder_set_state(SD_RECORDER_STATE_ERROR);
            return 0U;
        }

#if (_USE_EXPAND != 0)
        {
            uint32_t target = stem->wav_limit_bytes + sizeof(hdr);
            if(target > 0xFFFFFFF0UL)
                target = 0xFFFFFFF0UL;
            (void)f_expand(&stem->file, target, 1U);
        }
#endif
    }

    g_rec.files_prepared = 1U;
    return 1U;
}

static void recorder_close_all_files(void)
{
    for(uint32_t stem_id = 0U; stem_id < SD_RECORDER_MAX_STEMS; stem_id++)
    {
        recorder_stem_t *stem = &g_rec.stems[stem_id];
        if(stem->file_open != 0U)
        {
            (void)f_close(&stem->file);
            stem->file_open = 0U;
        }
    }
    g_rec.files_prepared = 0U;
}

static uint8_t recorder_patch_headers_and_close(void)
{
    for(uint32_t stem_id = 0U; stem_id < SD_RECORDER_MAX_STEMS; stem_id++)
    {
        recorder_stem_t *stem = &g_rec.stems[stem_id];
        if(stem->file_open == 0U)
            continue;

        recorder_wav_header_t hdr;
        recorder_build_wav_header(&hdr, (uint16_t)stem->cfg.channels, stem->data_bytes);

        if(f_lseek(&stem->file, 0U) != FR_OK)
        {
            stem->last_write_error = FR_INT_ERR;
            return 0U;
        }

        UINT bw = 0U;
        const FRESULT fr_wr = f_write(&stem->file, &hdr, sizeof(hdr), &bw);
        if((fr_wr != FR_OK) || (bw != sizeof(hdr)))
        {
            stem->last_write_error = (uint32_t)((fr_wr != FR_OK) ? fr_wr : FR_DISK_ERR);
            return 0U;
        }

        if(f_sync(&stem->file) != FR_OK)
        {
            stem->last_write_error = FR_INT_ERR;
            return 0U;
        }

        (void)f_close(&stem->file);
        stem->file_open = 0U;
    }

    g_rec.files_prepared = 0U;
    return 1U;
}

static int32_t recorder_pick_most_filled_stem(void)
{
    int32_t best = -1;
    uint32_t best_fill = 0U;

    for(uint32_t i = 0U; i < SD_RECORDER_MAX_STEMS; i++)
    {
        recorder_stem_t *stem = &g_rec.stems[i];
        if((stem->armed == 0U) || (stem->file_open == 0U))
            continue;

        const uint32_t fill = recorder_ring_count(&stem->ring);
        if(fill > best_fill)
        {
            best_fill = fill;
            best = (int32_t)i;
        }
    }

    return best;
}

static uint8_t recorder_drain_one_block(recorder_stem_t *stem, uint32_t *io_budget)
{
    if((stem == 0) || (io_budget == 0) || (*io_budget == 0U))
        return 0U;

    const uint32_t words = stem->ring.slot_words;
    const uint32_t bytes = words * 3U;

    if(bytes > *io_budget)
        return 0U;

    if(!recorder_ring_pop_block(stem, g_writer_float_block, REC_RING_MAX_SLOT_WORDS))
        return 0U;

    if((stem->wav_limit_bytes > 0U) &&
       (stem->data_bytes >= stem->wav_limit_bytes ||
        (stem->wav_limit_bytes - stem->data_bytes) < bytes))
    {
        recorder_set_state(SD_RECORDER_STATE_FINALIZING);
        return 0U;
    }

    if(stem->data_bytes > (REC_WAV_MAX_DATA_BYTES - bytes))
    {
        recorder_set_state(SD_RECORDER_STATE_FINALIZING);
        return 0U;
    }

    recorder_pcm24_pack(g_writer_float_block, words, g_writer_pcm24_block);

    UINT bw = 0U;
    const FRESULT fr = f_write(&stem->file, g_writer_pcm24_block, bytes, &bw);
    stem->write_calls++;

    if((fr != FR_OK) || (bw != bytes))
    {
        stem->last_write_error = (uint32_t)((fr != FR_OK) ? fr : FR_DISK_ERR);
        recorder_set_state((fr == FR_DISK_ERR || fr == FR_DENIED) ?
                           SD_RECORDER_STATE_FINALIZING :
                           SD_RECORDER_STATE_ERROR);
        return 0U;
    }

    stem->data_bytes += bytes;
    stem->bytes_written += bytes;
    stem->last_write_error = FR_OK;

    *io_budget -= bytes;
    g_rec.writer_bytes_total += bytes;
    return 1U;
}

void sd_recorder_init(void)
{
    memset(&g_rec, 0, sizeof(g_rec));

    for(uint32_t stem_id = 0U; stem_id < SD_RECORDER_MAX_STEMS; stem_id++)
    {
        g_rec.stems[stem_id].ring.data = &g_rec_ring_storage[stem_id][0U][0U];
        recorder_ring_reset(&g_rec.stems[stem_id].ring, 2U);
    }

    g_rec.writer_budget_bytes = REC_DEFAULT_WRITER_BUDGET_BYTES;
    g_rec.state = SD_RECORDER_STATE_IDLE;
}

void sd_recorder_set_writer_budget(uint32_t max_bytes_per_call)
{
    if(max_bytes_per_call == 0U)
        return;

    g_rec.writer_budget_bytes = max_bytes_per_call;
}

uint8_t sd_recorder_request_start(void)
{
    recorder_cmd_t cmd;
    cmd.type = CMD_START;
    cmd.stem_id = 0U;
    memset(&cmd.cfg, 0, sizeof(cmd.cfg));

    if(!recorder_cmd_push(&cmd))
        return 0U;

    g_rec.start_requests++;
    return 1U;
}

uint8_t sd_recorder_request_stop(void)
{
    recorder_cmd_t cmd;
    cmd.type = CMD_STOP;
    cmd.stem_id = 0U;
    memset(&cmd.cfg, 0, sizeof(cmd.cfg));

    if(!recorder_cmd_push(&cmd))
        return 0U;

    g_rec.stop_requests++;
    return 1U;
}

uint8_t sd_recorder_request_arm_stem(uint8_t stem_id,
                                     const sd_recorder_stem_cfg_t *cfg)
{
    if((cfg == 0) || (stem_id >= SD_RECORDER_MAX_STEMS) ||
       ((cfg->channels != 1U) && (cfg->channels != 2U)))
        return 0U;

    if(g_rec.state != SD_RECORDER_STATE_IDLE)
    {
        g_rec.rejected_config_changes++;
        return 0U;
    }

    recorder_cmd_t cmd;
    cmd.type = CMD_ARM_STEM;
    cmd.stem_id = stem_id;
    cmd.cfg = *cfg;

    if(!recorder_cmd_push(&cmd))
        return 0U;

    g_rec.arm_requests++;
    return 1U;
}

uint8_t sd_recorder_request_disarm_stem(uint8_t stem_id)
{
    if(stem_id >= SD_RECORDER_MAX_STEMS)
        return 0U;

    if(g_rec.state != SD_RECORDER_STATE_IDLE)
    {
        g_rec.rejected_config_changes++;
        return 0U;
    }

    recorder_cmd_t cmd;
    cmd.type = CMD_DISARM_STEM;
    cmd.stem_id = stem_id;
    memset(&cmd.cfg, 0, sizeof(cmd.cfg));

    if(!recorder_cmd_push(&cmd))
        return 0U;

    g_rec.disarm_requests++;
    return 1U;
}

void sd_recorder_audio_block_begin(uint32_t frames)
{
    (void)frames;

    g_rec.block_boundary_calls++;

    recorder_cmd_t cmd;

    while(recorder_cmd_pop(&cmd))
    {
        switch(cmd.type)
        {
            case CMD_START:
                if(g_rec.state == SD_RECORDER_STATE_IDLE)
                {
                    recorder_set_state(SD_RECORDER_STATE_START_PENDING);
                }
                else
                {
                    g_rec.rejected_state_requests++;
                }
                break;

            case CMD_STOP:
                if(g_rec.state == SD_RECORDER_STATE_RECORDING)
                {
                    recorder_set_state(SD_RECORDER_STATE_STOP_PENDING);
                }
                else
                {
                    g_rec.rejected_state_requests++;
                }
                break;

            case CMD_ARM_STEM:
                if((g_rec.state == SD_RECORDER_STATE_IDLE) &&
                   (cmd.stem_id < SD_RECORDER_MAX_STEMS))
                {
                    g_rec.stems[cmd.stem_id].cfg = cmd.cfg;
                    g_rec.stems[cmd.stem_id].armed = 1U;
                    recorder_ring_reset(&g_rec.stems[cmd.stem_id].ring,
                                        cmd.cfg.channels);
                    g_rec.stems[cmd.stem_id].blocks_captured = 0U;
                    g_rec.stems[cmd.stem_id].blocks_dropped_overflow = 0U;
                    g_rec.stems[cmd.stem_id].ring_high_watermark = 0U;
                }
                else
                {
                    g_rec.rejected_config_changes++;
                }
                break;

            case CMD_DISARM_STEM:
                if((g_rec.state == SD_RECORDER_STATE_IDLE) &&
                   (cmd.stem_id < SD_RECORDER_MAX_STEMS))
                {
                    memset(&g_rec.stems[cmd.stem_id], 0, sizeof(g_rec.stems[cmd.stem_id]));
                    g_rec.stems[cmd.stem_id].ring.data = &g_rec_ring_storage[cmd.stem_id][0U][0U];
                    recorder_ring_reset(&g_rec.stems[cmd.stem_id].ring, 2U);
                }
                else
                {
                    g_rec.rejected_config_changes++;
                }
                break;

            case CMD_NONE:
            default:
                break;
        }
    }

    if(g_rec.state == SD_RECORDER_STATE_START_PENDING)
    {
        recorder_set_state(SD_RECORDER_STATE_RECORDING);

        for(uint32_t stem_id = 0U; stem_id < SD_RECORDER_MAX_STEMS; stem_id++)
        {
            if(g_rec.stems[stem_id].armed != 0U)
            {
                recorder_ring_reset(&g_rec.stems[stem_id].ring,
                                    g_rec.stems[stem_id].cfg.channels);
                g_rec.stems[stem_id].blocks_captured = 0U;
                g_rec.stems[stem_id].blocks_dropped_overflow = 0U;
                g_rec.stems[stem_id].ring_high_watermark = 0U;
            }
        }
    }
    else if(g_rec.state == SD_RECORDER_STATE_STOP_PENDING)
    {
        recorder_set_state(SD_RECORDER_STATE_FINALIZING);
    }
}

void sd_recorder_capture_tap_block(sd_recorder_tap_t tap,
                                   uint8_t bus_id,
                                   const float *src_l,
                                   const float *src_r,
                                   uint32_t frames)
{
    if((src_l == 0) || (src_r == 0) || (frames == 0U) || (frames > AUDIO_BLOCK_SIZE))
        return;

    if(g_rec.state != SD_RECORDER_STATE_RECORDING)
        return;

    for(uint32_t stem_id = 0U; stem_id < SD_RECORDER_MAX_STEMS; stem_id++)
    {
        recorder_stem_t *stem = &g_rec.stems[stem_id];

        if((stem->armed == 0U) || (stem->cfg.tap != tap) || (stem->cfg.bus_id != bus_id))
            continue;

        (void)recorder_ring_push_block(stem, src_l, src_r, frames);
    }
}

void sd_recorder_writer_service(void)
{
    g_rec.writer_calls++;

    if((g_rec.state != SD_RECORDER_STATE_RECORDING) &&
       (g_rec.state != SD_RECORDER_STATE_FINALIZING))
        return;

    if((g_rec.state == SD_RECORDER_STATE_RECORDING) && (g_rec.files_prepared == 0U))
    {
        if(!recorder_has_armed_stems())
        {
            recorder_set_state(SD_RECORDER_STATE_FINALIZING);
        }
        else if(!recorder_prepare_files())
        {
            recorder_set_state(SD_RECORDER_STATE_ERROR);
            recorder_close_all_files();
            return;
        }
    }

    uint32_t budget = g_rec.writer_budget_bytes;
    while(budget > 0U)
    {
        const int32_t stem_idx = recorder_pick_most_filled_stem();
        if(stem_idx < 0)
            break;

        recorder_stem_t *stem = &g_rec.stems[(uint32_t)stem_idx];
        if(!recorder_drain_one_block(stem, &budget))
            break;
    }

    if(g_rec.state == SD_RECORDER_STATE_FINALIZING)
    {
        uint8_t drained = 1U;

        for(uint32_t stem_id = 0U; stem_id < SD_RECORDER_MAX_STEMS; stem_id++)
        {
            recorder_stem_t *stem = &g_rec.stems[stem_id];
            if((stem->armed != 0U) && (recorder_ring_count(&stem->ring) != 0U))
            {
                drained = 0U;
                break;
            }
        }

        if(drained != 0U)
        {
            if(recorder_patch_headers_and_close())
            {
                recorder_set_state(SD_RECORDER_STATE_IDLE);
            }
            else
            {
                recorder_set_state(SD_RECORDER_STATE_ERROR);
                recorder_close_all_files();
            }
        }
    }

    if(g_rec.state == SD_RECORDER_STATE_ERROR)
    {
        recorder_close_all_files();
    }
}

void sd_recorder_capture_tap_block(sd_recorder_tap_t tap,
                                   uint8_t bus_id,
                                   const float *src_l,
                                   const float *src_r,
                                   uint32_t frames)
{
    if((src_l == 0) || (src_r == 0) || (frames == 0U) || (frames > AUDIO_BLOCK_SIZE))
        return;

    if(g_rec.state != SD_RECORDER_STATE_RECORDING)
        return;

    for(uint32_t stem_id = 0U; stem_id < SD_RECORDER_MAX_STEMS; stem_id++)
    {
        recorder_stem_t *stem = &g_rec.stems[stem_id];

        if((stem->armed == 0U) || (stem->cfg.tap != tap) || (stem->cfg.bus_id != bus_id))
            continue;

        (void)recorder_ring_push_block(stem, src_l, src_r, frames);
    }
}

sd_recorder_state_t sd_recorder_get_state(void)
{
    return g_rec.state;
}

void sd_recorder_get_debug(sd_recorder_debug_t *out_debug)
{
    if(out_debug == 0)
        return;

    out_debug->state = g_rec.state;
    out_debug->start_requests = g_rec.start_requests;
    out_debug->stop_requests = g_rec.stop_requests;
    out_debug->arm_requests = g_rec.arm_requests;
    out_debug->disarm_requests = g_rec.disarm_requests;
    out_debug->rejected_config_changes = g_rec.rejected_config_changes;
    out_debug->rejected_state_requests = g_rec.rejected_state_requests;
    out_debug->block_boundary_calls = g_rec.block_boundary_calls;
    out_debug->transition_count = g_rec.transition_count;
    out_debug->writer_calls = g_rec.writer_calls;
    out_debug->writer_bytes_total = g_rec.writer_bytes_total;

    for(uint32_t stem_id = 0U; stem_id < SD_RECORDER_MAX_STEMS; stem_id++)
    {
        out_debug->stem_blocks_captured[stem_id] = g_rec.stems[stem_id].blocks_captured;
        out_debug->stem_blocks_dropped_overflow[stem_id] = g_rec.stems[stem_id].blocks_dropped_overflow;
        out_debug->stem_ring_high_watermark[stem_id] = g_rec.stems[stem_id].ring_high_watermark;
        out_debug->stem_bytes_written[stem_id] = g_rec.stems[stem_id].bytes_written;
        out_debug->stem_write_calls[stem_id] = g_rec.stems[stem_id].write_calls;
        out_debug->stem_last_write_error[stem_id] = g_rec.stems[stem_id].last_write_error;
    }
}
