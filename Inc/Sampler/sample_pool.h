#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "Storage/sd_access_gate.h"

#define SAMPLE_POOL_PROJECT_CAPACITY (1024U)
#define SAMPLE_CACHE_HOT_SAMPLE_CAPACITY (256U)

/*
 * Stream backend capacity follows the current active product catalogue size.
 * The product authority is sample_global_pool; sample_pool remains the Classic
 * Stream backend addressed by global STREAM slots.
 */
#define SAMPLE_POOL_SIZE (SAMPLE_CACHE_HOT_SAMPLE_CAPACITY)
#define SAMPLE_POOL_PATH_MAX (160U)

typedef enum
{
    SAMPLE_POOL_SLOT_EMPTY = 0,
    SAMPLE_POOL_SLOT_LOADED,
    SAMPLE_POOL_SLOT_PREPARING,
    SAMPLE_POOL_SLOT_ERROR,
    SAMPLE_POOL_SLOT_MISSING
} sample_pool_slot_state_t;

typedef enum
{
    SAMPLE_POOL_LOAD_OK = 0,
    SAMPLE_POOL_LOAD_INVALID_ID,
    SAMPLE_POOL_LOAD_INVALID_PATH,
    SAMPLE_POOL_LOAD_PATH_TOO_LONG,
    SAMPLE_POOL_LOAD_NO_FREE_SLOT,
    SAMPLE_POOL_LOAD_SD_GATE_REFUSED,
    SAMPLE_POOL_LOAD_SD_MOUNT_FAIL,
    SAMPLE_POOL_LOAD_SD_FILE_NOT_FOUND,
    SAMPLE_POOL_LOAD_SD_OPEN_FAIL,
    SAMPLE_POOL_LOAD_WAV_PARSE_FAIL,
    SAMPLE_POOL_LOAD_WAV_UNSUPPORTED_FORMAT,
    SAMPLE_POOL_LOAD_WAV_48K_REQUIRED,
    SAMPLE_POOL_LOAD_MEMORY_LIMIT,
    SAMPLE_POOL_LOAD_SD_READ_FAIL,
    SAMPLE_POOL_LOAD_SD_SEEK_FAIL,
    SAMPLE_POOL_LOAD_SD_SHORT_READ,
    SAMPLE_POOL_LOAD_SD_READ_INT_ERR,
    SAMPLE_POOL_LOAD_SD_NOT_READY,
    SAMPLE_POOL_LOAD_SD_INVALID_OBJECT,
    SAMPLE_POOL_LOAD_SD_TIMEOUT,
    SAMPLE_POOL_LOAD_SD_NOT_ENOUGH_CORE
} sample_pool_load_error_t;

typedef struct
{
    char path[SAMPLE_POOL_PATH_MAX];

    uint32_t data_offset;
    uint32_t length_frames;
    uint32_t bytes_per_frame;
    uint32_t data_start_frame;

    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;

    /* Legacy compatibility only: may alias a READY_FULL sample_cache buffer.
     * sample_pool never owns runtime audio memory. New audio consumers must use
     * sample_cache APIs instead of this pointer. */
    float *data;

    uint8_t valid;
} sample_desc_t;

typedef struct
{
    char path[SAMPLE_POOL_PATH_MAX];
} sample_pool_entry_snapshot_t;

typedef struct
{
    sample_pool_entry_snapshot_t slots[SAMPLE_POOL_SIZE];
} sample_pool_project_snapshot_t;

typedef struct
{
    uint32_t load_open_fail_count;
    uint32_t gate_release_on_error_count;
    char path[SAMPLE_POOL_PATH_MAX];
    sd_access_client_t gate_owner;
    sd_access_client_t gate_last_owner;
    FRESULT fatfs_result;
} sample_pool_diag_t;

#define SAMPLE_POOL_PREPARE_BATCH_MAX (16U)

typedef enum
{
    SAMPLE_POOL_PREPARE_IDLE = 0,
    SAMPLE_POOL_PREPARE_RUNNING,
    SAMPLE_POOL_PREPARE_READY,
    SAMPLE_POOL_PREPARE_ERROR
} sample_pool_prepare_status_t;

typedef struct
{
    sample_pool_prepare_status_t status;
    sample_pool_load_error_t error;
    uint8_t failed_index;
} sample_pool_prepare_result_t;

/*
 * sample_pool is the project/catalog owner only.
 * Runtime audio memory is owned by sample_cache; data is legacy compatibility
 * and may alias a READY_FULL cache, but sample_pool never owns sample audio.
 */
void sample_pool_init(void);
bool sample_pool_load(uint16_t id, const char *path);
void sample_pool_clear(uint16_t id);
bool sample_pool_is_loaded(uint16_t id);
sample_pool_slot_state_t sample_pool_get_state(uint16_t id);
const sample_desc_t *sample_pool_get(uint16_t id);
void sample_pool_capture_project_snapshot(sample_pool_project_snapshot_t *out_snapshot);
void sample_pool_restore_project_snapshot(const sample_pool_project_snapshot_t *snapshot);
sample_pool_load_error_t sample_pool_get_last_load_error(void);
uint8_t sample_pool_get_last_sd_error_code(void);
const sample_pool_diag_t *sample_pool_get_diag(void);
uint8_t sample_pool_prepare_batch_begin(uint16_t first_slot,
                                        const char *const *paths,
                                        uint8_t count);
void sample_pool_prepare_batch_service(void);
void sample_pool_prepare_batch_get_result(sample_pool_prepare_result_t *out_result);
