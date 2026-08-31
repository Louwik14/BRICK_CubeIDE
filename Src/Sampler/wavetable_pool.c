#include "Sampler/wavetable_pool.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_page_cache_port.h"
#include "IPC/shared_memory_ref_control.h"
#include "Platform/cache_maintenance.h"
#include "IPC/control_audio_publication.h"
#include "Sampler/audio_wave_table_projection_control.h"
#include "IPC/live_clock_control.h"
#include "IPC/control_audio_timing.h"
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
static CTRL_STATE uint64_t
    g_wavetable_retire_not_before_sample[WAVETABLE_POOL_MAX_SLOTS];
static CTRL_STATE uint8_t
    g_wavetable_retire_stop_committed[WAVETABLE_POOL_MAX_SLOTS];

static void wavetable_load_job_boot_init(void);


/* Catalogue ownership, preparation, asynchronous storage and AUDIO publication remain in their original sequence.
 * Private fragments share this translation unit to preserve static state and call order. */

#include "Wavetable/wavetable_catalog.inc"

#include "Wavetable/wavetable_prepare.inc"

#include "Wavetable/wavetable_storage_async.inc"

#include "Wavetable/wavetable_publication.inc"
