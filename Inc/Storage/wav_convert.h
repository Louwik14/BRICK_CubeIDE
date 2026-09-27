#pragma once

#include <stdint.h>

#include "wav_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    WAV_CONVERT_STATE_IDLE = 0,
    WAV_CONVERT_STATE_ACTIVE,
    WAV_CONVERT_STATE_DONE,
    WAV_CONVERT_STATE_FAILED
} wav_convert_state_t;

typedef enum
{
    WAV_CONVERT_ERROR_NONE = 0,
    WAV_CONVERT_ERROR_INVALID_ARG,
    WAV_CONVERT_ERROR_BUSY,
    WAV_CONVERT_ERROR_UNSUPPORTED,
    WAV_CONVERT_ERROR_MOUNT_FAIL,
    WAV_CONVERT_ERROR_OPEN_FAIL,
    WAV_CONVERT_ERROR_READ_FAIL,
    WAV_CONVERT_ERROR_WRITE_FAIL,
    WAV_CONVERT_ERROR_SYNC_FAIL,
    WAV_CONVERT_ERROR_CLOSE_FAIL,
    WAV_CONVERT_ERROR_VERIFY_FAIL,
    WAV_CONVERT_ERROR_REPLACE_FAIL,
    WAV_CONVERT_ERROR_NO_SPACE
} wav_convert_error_t;

typedef enum
{
    WAV_CONVERT_PATH_INVALID = 0,
    WAV_CONVERT_PATH_CANONICAL,
    WAV_CONVERT_PATH_NEEDS_CANONICAL,
    WAV_CONVERT_PATH_BUSY
} wav_convert_path_status_t;

#define WAV_CONVERT_BENCH_MAGIC   0x5743424DU /* "WCBM" */
#define WAV_CONVERT_BENCH_VERSION 1U

typedef struct
{
    uint32_t magic;                    /* 0x000 */
    uint32_t version;                  /* 0x004 */
    uint32_t struct_size;              /* 0x008 */
    uint32_t status;                   /* 0x00C: 0 empty, 1 active, 2 done, 3 failed */
    uint32_t core_clock_hz;            /* 0x010 */
    uint32_t total_ms;                 /* 0x014: wall clock */
    uint32_t active_ms;                /* 0x018: inside wav_convert_service */
    uint32_t open_parse_ms;            /* 0x01C: phase less profiled reads */
    uint32_t read_ms;                  /* 0x020: all profiled f_read calls */
    uint32_t decode_ms;                /* 0x024: non-SRC decode CPU */
    uint32_t src_ms;                   /* 0x028: SRC CPU including its decode, less reads */
    uint32_t write_ms;                 /* 0x02C: all profiled f_write calls */
    uint32_t sync_close_ms;            /* 0x030 */
    uint32_t verify_ms;                /* 0x034: phase less profiled read */
    uint32_t replace_ms;               /* 0x038 */
    uint32_t other_active_ms;           /* 0x03C */
    uint32_t service_calls;             /* 0x040 */
    uint32_t bytes_read;                /* 0x044: actual bytes returned */
    uint32_t bytes_written;             /* 0x048: actual bytes written */
    uint32_t read_calls;                /* 0x04C */
    uint32_t write_calls;               /* 0x050 */
    uint32_t min_read_size;             /* 0x054: requested bytes */
    uint32_t max_read_size;             /* 0x058 */
    uint32_t min_write_size;            /* 0x05C: requested bytes */
    uint32_t max_write_size;            /* 0x060 */
    uint32_t max_service_gap_ms;         /* 0x064 */
    uint32_t total_service_gap_ms;       /* 0x068 */
    uint32_t source_encoding;            /* 0x06C */
    uint32_t source_bits_per_sample;     /* 0x070 */
    uint32_t source_channels;            /* 0x074 */
    uint32_t source_sample_rate;         /* 0x078 */
    uint32_t source_frames;              /* 0x07C */
    uint32_t copy_mode;                  /* 0x080: 0 raw, 1 decode, 2 SRC */
    uint32_t has_src;                    /* 0x084 */
    uint32_t target_frames;              /* 0x088 */
    uint32_t result_error;               /* 0x08C: wav_convert_error_t */
    uint32_t wall_start_ms;              /* 0x090 */
    uint32_t wall_end_ms;                /* 0x094 */
    uint32_t wall_minus_active_ms;        /* 0x098 */
    uint32_t reserved0;                  /* 0x09C */
    uint64_t active_cycles;               /* 0x0A0 */
    uint64_t open_parse_cycles;           /* 0x0A8 */
    uint64_t read_cycles;                 /* 0x0B0 */
    uint64_t decode_cycles;               /* 0x0B8 */
    uint64_t src_cycles;                  /* 0x0C0 */
    uint64_t write_cycles;                /* 0x0C8 */
    uint64_t sync_close_cycles;           /* 0x0D0 */
    uint64_t verify_cycles;               /* 0x0D8 */
    uint64_t replace_cycles;              /* 0x0E0 */
    uint32_t open_calls;                  /* 0x0E8 */
    uint32_t close_calls;                 /* 0x0EC */
    uint32_t seek_calls;                  /* 0x0F0 */
    uint32_t sync_calls;                  /* 0x0F4 */
    uint32_t source_path_hash;            /* 0x0F8: FNV-1a */
    uint32_t reserved1;                   /* 0x0FC */
} wav_convert_bench_t;

extern volatile wav_convert_bench_t g_wav_convert_bench;

void wav_convert_init(void);
wav_convert_path_status_t wav_convert_path_canonical_status(
    const char *path, wav_info_t *out_info);
uint8_t wav_convert_path_needs_canonical(const char *path, wav_info_t *out_info);
uint8_t wav_convert_start_destructive_canonical(const char *path);
/* Project restore owns the closed mutation ingress while canonicalizing refs. */
uint8_t wav_convert_start_destructive_canonical_project(const char *path);
/* Caller owns the SD gate; conversion itself remains cooperative. */
uint8_t wav_convert_start_destructive_canonical_locked(const char *path);
void wav_convert_service(uint32_t byte_budget);
uint8_t wav_convert_cancel(void);
uint8_t wav_convert_is_active(void);
wav_convert_state_t wav_convert_get_state(void);
wav_convert_error_t wav_convert_get_last_error(void);
uint8_t wav_convert_get_progress_percent(void);
uint32_t wav_convert_get_output_bytes_done(void);
uint32_t wav_convert_get_output_bytes_total(void);
void wav_convert_clear_finished(void);

#ifdef __cplusplus
}
#endif
