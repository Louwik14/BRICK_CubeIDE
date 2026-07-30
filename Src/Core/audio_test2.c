#include "Core/audio_test2.h"

#if BRICK_TEST_BUILD

#include "Audio/audio_float.h"
#include "Board/board_audio.h"
#include "Core/cpu_load.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"
#include "stm32h7xx_hal.h"

#include <stdio.h>
#include <string.h>

#define AT2_DIR "0:/AUDIO_TEST2"
#define AT2_REFERENCE_PATH AT2_DIR "/REFERENCE.WAV"
#define AT2_INTERNAL_PATH AT2_DIR "/INTERNAL.WAV"
#define AT2_MANIFEST_PATH AT2_DIR "/MANIFEST.CSV"
#define AT2_RUN_PATH AT2_DIR "/RUN.CSV"
#define AT2_CHANNELS 2U
#define AT2_BYTES_PER_FRAME 6U
#define AT2_RING_FRAMES 16385U
#define AT2_IO_FRAMES 1024U
#define AT2_FADE_FRAMES 480U
#define AT2_CRC_INIT 0xFFFFFFFFUL
#define AT2_CRC_POLY 0xEDB88320UL
#define AT2_SWEEP_MUL_Q32 4296450981ULL

typedef enum
{
    AT2_SILENCE = 0,
    AT2_SYNC,
    AT2_TONE,
    AT2_SWEEP,
    AT2_MULTITONE,
    AT2_IMPULSES,
    AT2_WHITE,
    AT2_PINK,
    AT2_STEPS,
    AT2_MUTES
} at2_kind_t;

typedef enum
{
    AT2_STEREO = 0,
    AT2_LEFT,
    AT2_RIGHT,
    AT2_IDENTICAL,
    AT2_ANTIPHASE
} at2_channels_t;

typedef struct
{
    const char *name;
    uint8_t seconds;
    uint8_t kind;
    uint8_t channels;
    int32_t level_mdB;
    uint32_t frequency;
} at2_section_t;

#define S(name_, sec_, kind_, hz_, db_, ch_) \
    {name_, sec_, kind_, ch_, db_, hz_}

static const at2_section_t g_sections[] = {
    S("INITIAL SILENCE", 5, AT2_SILENCE, 0, 0, AT2_STEREO),
    S("SYNC IMPULSES", 5, AT2_SYNC, 0, -12000, AT2_STEREO),
    S("MARKER 20", 1, AT2_SILENCE, 0, 0, AT2_STEREO),
    S("SINE 20", 4, AT2_TONE, 20, -12000, AT2_STEREO),
    S("MARKER 50", 1, AT2_SILENCE, 0, 0, AT2_STEREO),
    S("SINE 50", 4, AT2_TONE, 50, -12000, AT2_STEREO),
    S("MARKER 100", 1, AT2_SILENCE, 0, 0, AT2_STEREO),
    S("SINE 100", 4, AT2_TONE, 100, -12000, AT2_STEREO),
    S("MARKER 500", 1, AT2_SILENCE, 0, 0, AT2_STEREO),
    S("SINE 500", 4, AT2_TONE, 500, -12000, AT2_STEREO),
    S("MARKER 1K", 1, AT2_SILENCE, 0, 0, AT2_STEREO),
    S("SINE 1K", 4, AT2_TONE, 1000, -12000, AT2_STEREO),
    S("MARKER 5K", 1, AT2_SILENCE, 0, 0, AT2_STEREO),
    S("SINE 5K", 4, AT2_TONE, 5000, -12000, AT2_STEREO),
    S("MARKER 10K", 1, AT2_SILENCE, 0, 0, AT2_STEREO),
    S("SINE 10K", 4, AT2_TONE, 10000, -12000, AT2_STEREO),
    S("MARKER 15K", 1, AT2_SILENCE, 0, 0, AT2_STEREO),
    S("SINE 15K", 4, AT2_TONE, 15000, -12000, AT2_STEREO),
    S("MARKER 20K", 1, AT2_SILENCE, 0, 0, AT2_STEREO),
    S("SINE 20K", 4, AT2_TONE, 20000, -12000, AT2_STEREO),
    S("SWEEP MARKER", 2, AT2_SILENCE, 0, 0, AT2_STEREO),
    S("LOG SWEEP", 20, AT2_SWEEP, 20, -12000, AT2_STEREO),
    S("MULTI MARKER", 2, AT2_SILENCE, 0, 0, AT2_STEREO),
    S("MULTITONE", 10, AT2_MULTITONE, 0, -12000, AT2_STEREO),
    S("LEVEL MARKER", 2, AT2_SILENCE, 0, 0, AT2_STEREO),
    S("1K -1DBFS", 6, AT2_TONE, 1000, -1000, AT2_STEREO),
    S("1K -6DBFS", 6, AT2_TONE, 1000, -6000, AT2_STEREO),
    S("1K -20DBFS", 6, AT2_TONE, 1000, -20000, AT2_STEREO),
    S("1K -60DBFS", 6, AT2_TONE, 1000, -60000, AT2_STEREO),
    S("1K -100DBFS", 6, AT2_TONE, 1000, -100000, AT2_STEREO),
    S("IMPULSE MARKER", 2, AT2_SILENCE, 0, 0, AT2_STEREO),
    S("SAFE IMPULSES", 5, AT2_IMPULSES, 0, -6000, AT2_STEREO),
    S("NOISE MARKER", 2, AT2_SILENCE, 0, 0, AT2_STEREO),
    S("WHITE NOISE", 10, AT2_WHITE, 0, -20000, AT2_STEREO),
    S("PINK NOISE", 10, AT2_PINK, 0, -20000, AT2_STEREO),
    S("CHANNEL MARKER", 2, AT2_SILENCE, 0, 0, AT2_STEREO),
    S("LEFT ONLY", 8, AT2_TONE, 1000, -12000, AT2_LEFT),
    S("RIGHT ONLY", 8, AT2_TONE, 1000, -12000, AT2_RIGHT),
    S("IDENTICAL", 8, AT2_TONE, 1000, -12000, AT2_IDENTICAL),
    S("OPPOSITE PHASE", 8, AT2_TONE, 1000, -12000, AT2_ANTIPHASE),
    S("STEP MARKER", 2, AT2_SILENCE, 0, 0, AT2_STEREO),
    S("LEVEL STEPS", 12, AT2_STEPS, 1000, -3000, AT2_STEREO),
    S("MUTE SEQUENCE", 15, AT2_MUTES, 1000, -12000, AT2_STEREO),
    S("FINAL SYNC", 5, AT2_SYNC, 0, -12000, AT2_STEREO),
    S("FINAL SILENCE", 30, AT2_SILENCE, 0, 0, AT2_STEREO),
};

#define AT2_SECTION_COUNT ((uint32_t)(sizeof(g_sections) / sizeof(g_sections[0])))

typedef struct
{
    uint32_t frame;
    uint32_t section_frame;
    uint32_t phase[3];
    uint32_t noise;
    uint32_t pink_counter;
    int32_t pink_rows[16];
    uint64_t sweep_freq_q16;
    uint8_t section;
} at2_generator_t;

typedef struct
{
    volatile uint32_t write_index;
    volatile uint32_t read_index;
    volatile uint32_t frame;
    volatile uint8_t render_active;
    volatile uint8_t render_done;
    volatile uint8_t internal_capture;
    volatile uint8_t ring_failed;
    audio_test2_state_t state;
    at2_generator_t irq_gen;
    at2_generator_t file_gen;
    FIL file;
    uint8_t file_open;
    uint8_t cancel_requested;
    uint8_t restore_transport;
    uint8_t countdown;
    uint8_t f_sync_ok;
    uint8_t f_close_ok;
    uint8_t sd_quiet_lock;
    uint8_t snapshot_valid;
    uint32_t countdown_until;
    uint32_t reference_crc;
    uint32_t internal_crc;
    uint32_t frames_written;
    uint32_t sd_errors;
    uint32_t underruns;
    uint32_t overruns;
    uint32_t clips;
    uint32_t nonfinite;
    uint32_t irq_peak_permille;
    seq_step_id_t playhead[SEQ_TRACK_COUNT];
    char status[24];
} at2_state_t;

STORAGE_STATE_SDRAM static at2_state_t g_at2;
SDRAM_RECORDER static int32_t g_at2_ring[AT2_RING_FRAMES * AT2_CHANNELS];
RECORDER_SCRATCH_SDRAM static uint8_t g_at2_io[AT2_IO_FRAMES * AT2_BYTES_PER_FRAME];
AUDIO_HOT static float g_at2_l[AUDIO_BLOCK_SIZE];
AUDIO_HOT static float g_at2_r[AUDIO_BLOCK_SIZE];
AUDIO_HOT static int32_t g_at2_lr[AUDIO_BLOCK_SIZE * AT2_CHANNELS];

static const uint32_t g_cordic_atan_turns[24] = {
    0x20000000U, 0x12E4051EU, 0x09FB385BU, 0x051111D4U,
    0x028B0D43U, 0x0145D7E1U, 0x00A2F61EU, 0x00517C55U,
    0x0028BE53U, 0x00145F2FU, 0x000A2F98U, 0x000517CCU,
    0x00028BE6U, 0x000145F3U, 0x0000A2FAU, 0x0000517DU,
    0x000028BEU, 0x0000145FU, 0x00000A30U, 0x00000518U,
    0x0000028CU, 0x00000146U, 0x000000A3U, 0x00000051U
};

static uint32_t at2_crc_byte(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (uint8_t i = 0U; i < 8U; ++i)
    {
        crc = (crc >> 1U) ^ (AT2_CRC_POLY & (uint32_t)-(int32_t)(crc & 1U));
    }
    return crc;
}

static int32_t at2_sine_q30(uint32_t phase)
{
    const uint32_t quadrant = phase >> 30U;
    uint32_t angle = phase & 0x3FFFFFFFU;
    int32_t sign = 1;
    if ((quadrant == 1U) || (quadrant == 3U))
    {
        angle = 0x40000000U - angle;
    }
    if (quadrant >= 2U)
    {
        sign = -1;
    }

    int32_t x = 652032874;
    int32_t y = 0;
    int32_t z = (int32_t)angle;
    for (uint8_t i = 0U; i < 24U; ++i)
    {
        const int32_t old_x = x;
        if (z >= 0)
        {
            x -= y >> i;
            y += old_x >> i;
            z -= (int32_t)g_cordic_atan_turns[i];
        }
        else
        {
            x += y >> i;
            y -= old_x >> i;
            z += (int32_t)g_cordic_atan_turns[i];
        }
    }
    return (sign > 0) ? y : -y;
}

static int32_t at2_level_peak(int32_t mdB)
{
    switch (mdB)
    {
        case -1000: return 7476354;
        case -3000: return 5938679;
        case -6000: return 4204263;
        case -12000: return 2107123;
        case -20000: return 838861;
        case -60000: return 8389;
        case -100000: return 84;
        default: return 0;
    }
}

static uint32_t at2_phase_inc(uint32_t frequency)
{
    return (uint32_t)(((uint64_t)frequency << 32U) / AUDIO_TEST2_SAMPLE_RATE);
}

static uint32_t at2_xorshift(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    *state = x;
    return x;
}

static void at2_generator_reset(at2_generator_t *gen)
{
    memset(gen, 0, sizeof(*gen));
    gen->noise = 0x6D2B79F5U;
    gen->sweep_freq_q16 = 20ULL << 16U;
}

static uint32_t at2_section_frames(const at2_section_t *section)
{
    return (uint32_t)section->seconds * AUDIO_TEST2_SAMPLE_RATE;
}

static uint32_t at2_fade_q16(const at2_generator_t *gen, const at2_section_t *section)
{
    const uint32_t length = at2_section_frames(section);
    uint32_t fade = 65536U;
    if (gen->section_frame < AT2_FADE_FRAMES)
    {
        fade = (gen->section_frame * 65536U) / AT2_FADE_FRAMES;
    }
    const uint32_t remaining = length - gen->section_frame;
    if (remaining < AT2_FADE_FRAMES)
    {
        const uint32_t tail = (remaining * 65536U) / AT2_FADE_FRAMES;
        if (tail < fade)
        {
            fade = tail;
        }
    }
    return fade;
}

static int32_t at2_tone(at2_generator_t *gen, uint8_t osc, uint32_t frequency,
                        int32_t peak)
{
    const int32_t s = at2_sine_q30(gen->phase[osc]);
    gen->phase[osc] += at2_phase_inc(frequency);
    return (int32_t)(((int64_t)s * peak) >> 30U);
}

static void at2_generator_frame(at2_generator_t *gen, int32_t *left, int32_t *right)
{
    if (gen->section >= AT2_SECTION_COUNT)
    {
        *left = 0;
        *right = 0;
        return;
    }

    const at2_section_t *const section = &g_sections[gen->section];
    const int32_t peak = at2_level_peak(section->level_mdB);
    int32_t sample = 0;

    switch ((at2_kind_t)section->kind)
    {
        case AT2_SYNC:
        {
            const uint32_t in_second = gen->section_frame % AUDIO_TEST2_SAMPLE_RATE;
            const uint32_t second = gen->section_frame / AUDIO_TEST2_SAMPLE_RATE;
            if ((in_second < 48U) || ((second == 0U) && (in_second >= 4800U) && (in_second < 4848U)))
            {
                sample = peak;
            }
            break;
        }
        case AT2_TONE:
            sample = at2_tone(gen, 0U, section->frequency, peak);
            sample = (int32_t)(((int64_t)sample * at2_fade_q16(gen, section)) >> 16U);
            break;
        case AT2_SWEEP:
        {
            if ((gen->section_frame != 0U) && ((gen->section_frame % 48U) == 0U))
            {
                gen->sweep_freq_q16 =
                    (gen->sweep_freq_q16 * AT2_SWEEP_MUL_Q32) >> 32U;
            }
            const uint32_t frequency = (uint32_t)(gen->sweep_freq_q16 >> 16U);
            const uint32_t inc = (uint32_t)((gen->sweep_freq_q16 << 16U)
                                            / AUDIO_TEST2_SAMPLE_RATE);
            sample = (int32_t)(((int64_t)at2_sine_q30(gen->phase[0]) * peak) >> 30U);
            gen->phase[0] += inc;
            sample = (int32_t)(((int64_t)sample * at2_fade_q16(gen, section)) >> 16U);
            (void)frequency;
            break;
        }
        case AT2_MULTITONE:
            sample = (at2_tone(gen, 0U, 997U, peak / 3)
                      + at2_tone(gen, 1U, 2003U, peak / 3)
                      + at2_tone(gen, 2U, 5003U, peak / 3));
            sample = (int32_t)(((int64_t)sample * at2_fade_q16(gen, section)) >> 16U);
            break;
        case AT2_IMPULSES:
            if ((gen->section_frame % 12000U) < 24U)
            {
                sample = ((gen->section_frame / 12000U) & 1U) ? -peak : peak;
            }
            break;
        case AT2_WHITE:
            sample = (int32_t)(at2_xorshift(&gen->noise) >> 8U) - 8388608;
            sample = (int32_t)(((int64_t)sample * peak) / 8388608);
            sample = (int32_t)(((int64_t)sample * at2_fade_q16(gen, section)) >> 16U);
            break;
        case AT2_PINK:
        {
            gen->pink_counter++;
            uint32_t changed = gen->pink_counter;
            uint8_t row = 0U;
            while (((changed & 1U) == 0U) && (row < 15U))
            {
                changed >>= 1U;
                row++;
            }
            gen->pink_rows[row] = (int32_t)(at2_xorshift(&gen->noise) >> 16U) - 32768;
            int32_t sum = 0;
            for (uint8_t i = 0U; i < 16U; ++i)
            {
                sum += gen->pink_rows[i];
            }
            sample = (int32_t)(((int64_t)sum * peak) / (16 * 32768));
            sample = (int32_t)(((int64_t)sample * at2_fade_q16(gen, section)) >> 16U);
            break;
        }
        case AT2_STEPS:
        {
            const uint32_t half_second = gen->section_frame / 24000U;
            const int32_t step_peak = ((half_second & 1U) != 0U) ? 838861 : peak;
            sample = at2_tone(gen, 0U, section->frequency, step_peak);
            break;
        }
        case AT2_MUTES:
            if (((gen->section_frame / AUDIO_TEST2_SAMPLE_RATE) & 1U) == 0U)
            {
                sample = at2_tone(gen, 0U, section->frequency, peak);
            }
            break;
        case AT2_SILENCE:
        default:
            break;
    }

    switch ((at2_channels_t)section->channels)
    {
        case AT2_LEFT:
            *left = sample;
            *right = 0;
            break;
        case AT2_RIGHT:
            *left = 0;
            *right = sample;
            break;
        case AT2_ANTIPHASE:
            *left = sample;
            *right = -sample;
            break;
        case AT2_IDENTICAL:
        case AT2_STEREO:
        default:
            *left = sample;
            *right = sample;
            break;
    }

    gen->frame++;
    gen->section_frame++;
    if (gen->section_frame >= at2_section_frames(section))
    {
        gen->section++;
        gen->section_frame = 0U;
        gen->phase[0] = 0U;
        gen->phase[1] = 0U;
        gen->phase[2] = 0U;
        gen->sweep_freq_q16 = 20ULL << 16U;
    }
}

static void at2_pack_frame(uint8_t *dst, int32_t left, int32_t right)
{
    dst[0] = (uint8_t)left;
    dst[1] = (uint8_t)(left >> 8U);
    dst[2] = (uint8_t)(left >> 16U);
    dst[3] = (uint8_t)right;
    dst[4] = (uint8_t)(right >> 8U);
    dst[5] = (uint8_t)(right >> 16U);
}

static void at2_write_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8U);
}

static void at2_write_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8U);
    dst[2] = (uint8_t)(value >> 16U);
    dst[3] = (uint8_t)(value >> 24U);
}

static void at2_wav_header(uint8_t *header, uint32_t frames)
{
    const uint32_t data_bytes = frames * AT2_BYTES_PER_FRAME;
    memset(header, 0, 44U);
    memcpy(&header[0], "RIFF", 4U);
    at2_write_le32(&header[4], 36U + data_bytes);
    memcpy(&header[8], "WAVEfmt ", 8U);
    at2_write_le32(&header[16], 16U);
    at2_write_le16(&header[20], 1U);
    at2_write_le16(&header[22], 2U);
    at2_write_le32(&header[24], AUDIO_TEST2_SAMPLE_RATE);
    at2_write_le32(&header[28], AUDIO_TEST2_SAMPLE_RATE * AT2_BYTES_PER_FRAME);
    at2_write_le16(&header[32], AT2_BYTES_PER_FRAME);
    at2_write_le16(&header[34], 24U);
    memcpy(&header[36], "data", 4U);
    at2_write_le32(&header[40], data_bytes);
}

static uint8_t at2_sd_begin(void)
{
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_AUDIO_TEST) == 0U)
    {
        return 0U;
    }
    return 1U;
}

static void at2_sd_end(void)
{
    sd_access_gate_release(SD_ACCESS_CLIENT_AUDIO_TEST);
}

static uint8_t at2_open_wav(const char *path)
{
    uint8_t header[44];
    UINT written = 0U;
    at2_wav_header(header, 0U);
    if (at2_sd_begin() == 0U)
    {
        return 0U;
    }
    (void)f_mkdir(AT2_DIR);
    FRESULT fr = f_open(&g_at2.file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr == FR_OK)
    {
        fr = f_write(&g_at2.file, header, sizeof(header), &written);
    }
    at2_sd_end();
    if ((fr != FR_OK) || (written != sizeof(header)))
    {
        g_at2.sd_errors++;
        return 0U;
    }
    g_at2.file_open = 1U;
    g_at2.frames_written = 0U;
    return 1U;
}

static uint8_t at2_finalize_wav(const char *path, uint32_t frames)
{
    uint8_t header[44];
    UINT written = 0U;
    FILINFO info;
    at2_wav_header(header, frames);
    if ((g_at2.file_open == 0U) || (at2_sd_begin() == 0U))
    {
        return 0U;
    }
    FRESULT fr = f_lseek(&g_at2.file, 0U);
    if (fr == FR_OK)
    {
        fr = f_write(&g_at2.file, header, sizeof(header), &written);
    }
    g_at2.f_sync_ok = ((fr == FR_OK) && (written == sizeof(header))
                       && (f_sync(&g_at2.file) == FR_OK)) ? 1U : 0U;
    g_at2.f_close_ok = (f_close(&g_at2.file) == FR_OK) ? 1U : 0U;
    g_at2.file_open = 0U;
    const FRESULT stat_fr = f_stat(path, &info);
    at2_sd_end();
    const uint32_t expected = 44U + (frames * AT2_BYTES_PER_FRAME);
    if ((g_at2.f_sync_ok == 0U) || (g_at2.f_close_ok == 0U)
        || (stat_fr != FR_OK) || (info.fsize != expected))
    {
        g_at2.sd_errors++;
        return 0U;
    }
    return 1U;
}

static const char *at2_channel_label(uint8_t channels)
{
    switch ((at2_channels_t)channels)
    {
        case AT2_LEFT: return "L";
        case AT2_RIGHT: return "R";
        case AT2_ANTIPHASE: return "L=-R";
        case AT2_IDENTICAL: return "L=R";
        default: return "STEREO";
    }
}

static uint8_t at2_write_manifest(void)
{
    FIL file;
    if (at2_sd_begin() == 0U)
    {
        return 0U;
    }
    FRESULT fr = f_open(&file, AT2_MANIFEST_PATH, FA_CREATE_ALWAYS | FA_WRITE);
    uint32_t start = 0U;
    if (fr == FR_OK)
    {
        (void)f_printf(&file, "name,start_frame,start_s,duration_frames,duration_s,frequency_hz,level_dbfs,channels\r\n");
        for (uint32_t i = 0U; i < AT2_SECTION_COUNT; ++i)
        {
            const at2_section_t *const s = &g_sections[i];
            (void)f_printf(&file, "%s,%lu,%lu,%lu,%u,%lu,%d.%03d,%s\r\n",
                           s->name, (unsigned long)start,
                           (unsigned long)(start / AUDIO_TEST2_SAMPLE_RATE),
                           (unsigned long)at2_section_frames(s),
                           (unsigned)s->seconds, (unsigned long)s->frequency,
                           (int)(s->level_mdB / 1000),
                           (int)((s->level_mdB < 0 ? -s->level_mdB : s->level_mdB) % 1000),
                           at2_channel_label(s->channels));
            start += at2_section_frames(s);
        }
        if (f_sync(&file) != FR_OK)
        {
            fr = FR_DISK_ERR;
        }
        if (f_close(&file) != FR_OK)
        {
            fr = FR_DISK_ERR;
        }
    }
    at2_sd_end();
    if ((fr != FR_OK) || (start != AUDIO_TEST2_DURATION_FRAMES))
    {
        g_at2.sd_errors++;
        return 0U;
    }
    return 1U;
}

static const char *at2_board_label(void)
{
#if defined(BRICK6_VARIANT_PREMIUM)
    return "PREMIUM";
#else
    return "LOW";
#endif
}

static uint8_t at2_write_run(void)
{
    FIL file;
    cpu_load_metrics_t cpu;
    cpu_load_get_metrics(&cpu);
    if (cpu.peak_permille > g_at2.irq_peak_permille)
    {
        g_at2.irq_peak_permille = cpu.peak_permille;
    }
    g_at2.underruns = cpu.over_100_count;
    if (at2_sd_begin() == 0U)
    {
        return 0U;
    }
    FRESULT fr = f_open(&file, AT2_RUN_PATH, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr == FR_OK)
    {
        (void)f_printf(&file,
            "schema,firmware,board,sample_rate,frames,reference_crc32,internal_crc32,sd_errors,underruns,overruns,clips,nan_inf,irq_peak_permille,f_sync,f_close,phase\r\n");
        (void)f_printf(&file,
            "2,main_doublemcu_monocore,%s,48000,%lu,%08lX,%08lX,%lu,%lu,%lu,%lu,%lu,%lu,%u,%u,INTERNAL_OK\r\n",
            at2_board_label(), (unsigned long)AUDIO_TEST2_DURATION_FRAMES,
            (unsigned long)g_at2.reference_crc, (unsigned long)g_at2.internal_crc,
            (unsigned long)g_at2.sd_errors, (unsigned long)g_at2.underruns,
            (unsigned long)g_at2.overruns,
            (unsigned long)g_at2.clips, (unsigned long)g_at2.nonfinite,
            (unsigned long)g_at2.irq_peak_permille,
            (unsigned)g_at2.f_sync_ok, (unsigned)g_at2.f_close_ok);
        if (f_sync(&file) != FR_OK)
        {
            fr = FR_DISK_ERR;
        }
        if (f_close(&file) != FR_OK)
        {
            fr = FR_DISK_ERR;
        }
    }
    at2_sd_end();
    if (fr != FR_OK)
    {
        g_at2.sd_errors++;
        return 0U;
    }
    return 1U;
}

static void at2_restore(void)
{
    g_at2.render_active = 0U;
    g_at2.internal_capture = 0U;
    if (g_at2.sd_quiet_lock != 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_AUDIO_TEST);
        g_at2.sd_quiet_lock = 0U;
    }
    if (g_at2.snapshot_valid == 0U)
    {
        return;
    }
    if (g_at2.restore_transport != 0U)
    {
        seq_runtime_start();
    }
    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        (void)seq_runtime_set_playhead_step(track, g_at2.playhead[track]);
    }
    g_at2.restore_transport = 0U;
    g_at2.snapshot_valid = 0U;
}

static void at2_suspend(void)
{
    g_at2.restore_transport = seq_runtime_is_running();
    g_at2.snapshot_valid = 1U;
    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        (void)seq_runtime_get_playhead_step(track, &g_at2.playhead[track]);
    }
    seq_runtime_stop();
}

static void at2_fail(const char *status)
{
    g_at2.state = AUDIO_TEST2_ERROR;
    (void)snprintf(g_at2.status, sizeof(g_at2.status), "%s", status);
    at2_restore();
}

static uint32_t at2_ring_pending(void)
{
    const uint32_t wr = g_at2.write_index;
    const uint32_t rd = g_at2.read_index;
    return (wr >= rd) ? (wr - rd) : (AT2_RING_FRAMES - (rd - wr));
}

static uint8_t at2_write_reference_chunk(void)
{
    uint32_t frames = AUDIO_TEST2_DURATION_FRAMES - g_at2.frames_written;
    if (frames > AT2_IO_FRAMES)
    {
        frames = AT2_IO_FRAMES;
    }
    for (uint32_t i = 0U; i < frames; ++i)
    {
        int32_t left;
        int32_t right;
        at2_generator_frame(&g_at2.file_gen, &left, &right);
        at2_pack_frame(&g_at2_io[i * AT2_BYTES_PER_FRAME], left, right);
    }
    UINT written = 0U;
    if (at2_sd_begin() == 0U)
    {
        return 1U;
    }
    const FRESULT fr = f_write(&g_at2.file, g_at2_io,
                               frames * AT2_BYTES_PER_FRAME, &written);
    at2_sd_end();
    if ((fr != FR_OK) || (written != (frames * AT2_BYTES_PER_FRAME)))
    {
        g_at2.sd_errors++;
        return 0U;
    }
    for (uint32_t i = 0U; i < written; ++i)
    {
        g_at2.reference_crc = at2_crc_byte(g_at2.reference_crc, g_at2_io[i]);
    }
    g_at2.frames_written += frames;
    g_at2.frame = g_at2.frames_written;
    return 1U;
}

static uint8_t at2_drain_internal(void)
{
    uint32_t frames = at2_ring_pending();
    if (frames > AT2_IO_FRAMES)
    {
        frames = AT2_IO_FRAMES;
    }
    if (frames == 0U)
    {
        return 1U;
    }
    uint32_t rd = g_at2.read_index;
    for (uint32_t i = 0U; i < frames; ++i)
    {
        const int32_t left = g_at2_ring[rd * 2U];
        const int32_t right = g_at2_ring[(rd * 2U) + 1U];
        at2_pack_frame(&g_at2_io[i * AT2_BYTES_PER_FRAME], left, right);
        rd++;
        if (rd >= AT2_RING_FRAMES)
        {
            rd = 0U;
        }
    }
    UINT written = 0U;
    if (at2_sd_begin() == 0U)
    {
        return 1U;
    }
    const FRESULT fr = f_write(&g_at2.file, g_at2_io,
                               frames * AT2_BYTES_PER_FRAME, &written);
    at2_sd_end();
    if ((fr != FR_OK) || (written != (frames * AT2_BYTES_PER_FRAME)))
    {
        g_at2.sd_errors++;
        return 0U;
    }
    for (uint32_t i = 0U; i < written; ++i)
    {
        g_at2.internal_crc = at2_crc_byte(g_at2.internal_crc, g_at2_io[i]);
    }
    g_at2.read_index = rd;
    g_at2.frames_written += frames;
    return 1U;
}

static void at2_start_render(audio_test2_state_t state)
{
    at2_generator_reset(&g_at2.irq_gen);
    g_at2.frame = 0U;
    g_at2.render_done = 0U;
    g_at2.state = state;
    g_at2.render_active = 1U;
    cpu_load_reset_measurement();
}

void audio_test2_init(void)
{
    memset(&g_at2, 0, sizeof(g_at2));
    g_at2.state = AUDIO_TEST2_READY;
    (void)snprintf(g_at2.status, sizeof(g_at2.status), "P2 START");
}

uint8_t audio_test2_start_internal(void)
{
    if ((g_at2.state != AUDIO_TEST2_READY) && (g_at2.state != AUDIO_TEST2_ERROR))
    {
        return 0U;
    }
    if (g_at2.file_open != 0U)
    {
        return 0U;
    }
    memset(&g_at2, 0, sizeof(g_at2));
    g_at2.state = AUDIO_TEST2_REFERENCE;
    g_at2.reference_crc = AT2_CRC_INIT;
    g_at2.internal_crc = AT2_CRC_INIT;
    at2_suspend();
    at2_generator_reset(&g_at2.file_gen);
    (void)snprintf(g_at2.status, sizeof(g_at2.status), "REFERENCE");
    if ((at2_write_manifest() == 0U) || (at2_open_wav(AT2_REFERENCE_PATH) == 0U))
    {
        at2_fail("SD PREP ERROR");
        return 0U;
    }
    return 1U;
}

uint8_t audio_test2_start_line(void)
{
    if ((g_at2.state != AUDIO_TEST2_LINE_READY) && (g_at2.state != AUDIO_TEST2_DONE))
    {
        return 0U;
    }
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_AUDIO_TEST) == 0U)
    {
        (void)snprintf(g_at2.status, sizeof(g_at2.status), "SD BUSY");
        return 0U;
    }
    g_at2.sd_quiet_lock = 1U;
    at2_suspend();
    g_at2.state = AUDIO_TEST2_COUNTDOWN_LINE;
    g_at2.countdown = 3U;
    g_at2.countdown_until = HAL_GetTick() + 3000U;
    g_at2.frame = 0U;
    (void)snprintf(g_at2.status, sizeof(g_at2.status), "SILENT COUNTDOWN");
    return 1U;
}

uint8_t audio_test2_start_headphone(void)
{
    if ((g_at2.state != AUDIO_TEST2_HEADPHONE_READY) && (g_at2.state != AUDIO_TEST2_DONE))
    {
        return 0U;
    }
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_AUDIO_TEST) == 0U)
    {
        (void)snprintf(g_at2.status, sizeof(g_at2.status), "SD BUSY");
        return 0U;
    }
    g_at2.sd_quiet_lock = 1U;
    at2_suspend();
    g_at2.state = AUDIO_TEST2_COUNTDOWN_HEADPHONE;
    g_at2.countdown = 3U;
    g_at2.countdown_until = HAL_GetTick() + 3000U;
    g_at2.frame = 0U;
    (void)snprintf(g_at2.status, sizeof(g_at2.status), "SET SAFE HP LEVEL");
    return 1U;
}

void audio_test2_cancel(void)
{
    g_at2.cancel_requested = 1U;
    g_at2.render_active = 0U;
}

uint8_t audio_test2_is_active(void)
{
    return ((g_at2.state != AUDIO_TEST2_READY) && (g_at2.state != AUDIO_TEST2_IDLE)
            && (g_at2.state != AUDIO_TEST2_DONE)
            && (g_at2.state != AUDIO_TEST2_ERROR)) ? 1U : 0U;
}

void audio_test2_service(void)
{
    if (g_at2.cancel_requested != 0U)
    {
        if (g_at2.file_open != 0U)
        {
            if (at2_sd_begin() == 0U)
            {
                return;
            }
            const FRESULT sync_fr = f_sync(&g_at2.file);
            const FRESULT close_fr = f_close(&g_at2.file);
            if ((sync_fr != FR_OK) || (close_fr != FR_OK))
            {
                g_at2.sd_errors++;
            }
            g_at2.file_open = 0U;
            at2_sd_end();
        }
        at2_restore();
        g_at2.state = AUDIO_TEST2_READY;
        g_at2.cancel_requested = 0U;
        (void)snprintf(g_at2.status, sizeof(g_at2.status), "CANCELLED");
        return;
    }

    switch (g_at2.state)
    {
        case AUDIO_TEST2_REFERENCE:
            if (at2_write_reference_chunk() == 0U)
            {
                at2_fail("REFERENCE ERROR");
            }
            else if (g_at2.frames_written >= AUDIO_TEST2_DURATION_FRAMES)
            {
                g_at2.reference_crc ^= 0xFFFFFFFFUL;
                if (at2_finalize_wav(AT2_REFERENCE_PATH, AUDIO_TEST2_DURATION_FRAMES) == 0U)
                {
                    at2_fail("REF VERIFY ERROR");
                    break;
                }
                if (at2_open_wav(AT2_INTERNAL_PATH) == 0U)
                {
                    at2_fail("INTERNAL OPEN ERR");
                    break;
                }
                g_at2.write_index = 0U;
                g_at2.read_index = 0U;
                g_at2.frames_written = 0U;
                g_at2.internal_capture = 1U;
                at2_start_render(AUDIO_TEST2_INTERNAL);
                (void)snprintf(g_at2.status, sizeof(g_at2.status), "CAPTURING");
            }
            break;
        case AUDIO_TEST2_INTERNAL:
            if ((g_at2.ring_failed != 0U) || (at2_drain_internal() == 0U))
            {
                at2_fail("INTERNAL OVERRUN");
            }
            else if ((g_at2.render_done != 0U) && (at2_ring_pending() == 0U))
            {
                g_at2.internal_capture = 0U;
                g_at2.internal_crc ^= 0xFFFFFFFFUL;
                g_at2.state = AUDIO_TEST2_VERIFY;
            }
            break;
        case AUDIO_TEST2_VERIFY:
        {
            const uint8_t frames_ok =
                (g_at2.frames_written == AUDIO_TEST2_DURATION_FRAMES) ? 1U : 0U;
            const uint8_t crc_ok =
                (g_at2.reference_crc == g_at2.internal_crc) ? 1U : 0U;
            const uint8_t finalize_ok =
                at2_finalize_wav(AT2_INTERNAL_PATH, g_at2.frames_written);
            const uint8_t run_ok = at2_write_run();
            if ((frames_ok == 0U) || (crc_ok == 0U)
                || (finalize_ok == 0U) || (run_ok == 0U))
            {
                at2_fail("VERIFY ERROR");
                break;
            }
            at2_restore();
            g_at2.state = AUDIO_TEST2_LINE_READY;
            (void)snprintf(g_at2.status, sizeof(g_at2.status), "CONNECT LINE");
            break;
        }
        case AUDIO_TEST2_COUNTDOWN_LINE:
        case AUDIO_TEST2_COUNTDOWN_HEADPHONE:
        {
            const uint32_t now = HAL_GetTick();
            if ((int32_t)(g_at2.countdown_until - now) <= 0)
            {
                g_at2.countdown = 0U;
                at2_start_render((g_at2.state == AUDIO_TEST2_COUNTDOWN_LINE)
                                     ? AUDIO_TEST2_LINE : AUDIO_TEST2_HEADPHONE);
                (void)snprintf(g_at2.status, sizeof(g_at2.status), "PLAYING");
            }
            else
            {
                g_at2.countdown =
                    (uint8_t)(((g_at2.countdown_until - now) + 999U) / 1000U);
            }
            break;
        }
        case AUDIO_TEST2_LINE:
            if (g_at2.render_done != 0U)
            {
                at2_restore();
                g_at2.state = AUDIO_TEST2_HEADPHONE_READY;
                (void)snprintf(g_at2.status, sizeof(g_at2.status), "CONNECT HP SAFE");
            }
            break;
        case AUDIO_TEST2_HEADPHONE:
            if (g_at2.render_done != 0U)
            {
                at2_restore();
                g_at2.state = AUDIO_TEST2_DONE;
                (void)snprintf(g_at2.status, sizeof(g_at2.status), "AUDIO TEST 2 DONE");
            }
            break;
        case AUDIO_TEST2_ERROR:
            if ((g_at2.file_open != 0U) && (at2_sd_begin() != 0U))
            {
                (void)f_sync(&g_at2.file);
                (void)f_close(&g_at2.file);
                g_at2.file_open = 0U;
                at2_sd_end();
            }
            break;
        default:
            break;
    }
}

void audio_test2_get_view(audio_test2_view_t *out_view)
{
    if (out_view == 0)
    {
        return;
    }
    const uint8_t section = (g_at2.state == AUDIO_TEST2_REFERENCE)
                                ? g_at2.file_gen.section : g_at2.irq_gen.section;
    out_view->state = g_at2.state;
    out_view->frame = g_at2.frame;
    out_view->total_frames = AUDIO_TEST2_DURATION_FRAMES;
    out_view->reference_crc = g_at2.reference_crc;
    out_view->internal_crc = g_at2.internal_crc;
    out_view->sd_errors = g_at2.sd_errors;
    out_view->underruns = g_at2.underruns;
    out_view->overruns = g_at2.overruns;
    out_view->clips = g_at2.clips;
    out_view->nonfinite = g_at2.nonfinite;
    out_view->irq_peak_permille = g_at2.irq_peak_permille;
    out_view->countdown = g_at2.countdown;
    out_view->section = (section < AT2_SECTION_COUNT) ? g_sections[section].name : "COMPLETE";
    out_view->status = g_at2.status;
}

uint8_t audio_test2_process_irq(int32_t *tx, uint32_t frames)
{
    if (g_at2.render_active == 0U)
    {
        if (audio_test2_is_active() == 0U)
        {
            return 0U;
        }
        if (frames > AUDIO_BLOCK_SIZE)
        {
            frames = AUDIO_BLOCK_SIZE;
        }
        memset(g_at2_l, 0, frames * sizeof(g_at2_l[0]));
        memset(g_at2_r, 0, frames * sizeof(g_at2_r[0]));
        board_audio_pack_output(tx, g_at2_l, g_at2_r, g_at2_l, g_at2_r,
                                frames, 1.0f, 1.0f);
        return 1U;
    }
    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }
    uint32_t produced = 0U;
    while ((produced < frames) && (g_at2.irq_gen.frame < AUDIO_TEST2_DURATION_FRAMES))
    {
        int32_t left;
        int32_t right;
        at2_generator_frame(&g_at2.irq_gen, &left, &right);
        g_at2_lr[produced * 2U] = left;
        g_at2_lr[(produced * 2U) + 1U] = right;
        g_at2_l[produced] = (float)left * (1.0f / 8388607.0f);
        g_at2_r[produced] = (float)right * (1.0f / 8388607.0f);
        produced++;
    }
    for (uint32_t i = produced; i < frames; ++i)
    {
        g_at2_lr[i * 2U] = 0;
        g_at2_lr[(i * 2U) + 1U] = 0;
        g_at2_l[i] = 0.0f;
        g_at2_r[i] = 0.0f;
    }

    if (g_at2.internal_capture != 0U)
    {
        uint32_t wr = g_at2.write_index;
        for (uint32_t i = 0U; i < produced; ++i)
        {
            uint32_t next = wr + 1U;
            if (next >= AT2_RING_FRAMES)
            {
                next = 0U;
            }
            if (next == g_at2.read_index)
            {
                g_at2.overruns++;
                g_at2.ring_failed = 1U;
                break;
            }
            g_at2_ring[wr * 2U] = g_at2_lr[i * 2U];
            g_at2_ring[(wr * 2U) + 1U] = g_at2_lr[(i * 2U) + 1U];
            wr = next;
        }
        g_at2.write_index = wr;
    }

    board_audio_pack_output(tx, g_at2_l, g_at2_r, g_at2_l, g_at2_r,
                            frames, 1.0f, 1.0f);
    g_at2.frame = g_at2.irq_gen.frame;
    if (g_at2.irq_gen.frame >= AUDIO_TEST2_DURATION_FRAMES)
    {
        g_at2.render_active = 0U;
        g_at2.render_done = 1U;
    }
    return 1U;
}

_Static_assert(AT2_SECTION_COUNT == 45U, "Audio Test 2 manifest changed");
_Static_assert(AUDIO_TEST2_DURATION_FRAMES == (248U * AUDIO_TEST2_SAMPLE_RATE),
               "Audio Test 2 duration mismatch");

#endif
