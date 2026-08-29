#pragma once

#include <stdint.h>

#include "Sampler/sample_classic_config.h"
#include "Sampler/sample_page_cache_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_GLOBAL_POOL_FINAL_SLOTS    (256U)
#define SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS   SAMPLE_PAGE_PRODUCT_MAX_LONG_SAMPLE_SLOTS
#define SAMPLE_GLOBAL_POOL_MAX_SLOTS      SAMPLE_GLOBAL_POOL_FINAL_SLOTS
/* Product-wide content entries: Classic, RAM, Multi samples and wavetables. */
#if BRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST
#define SAMPLE_GLOBAL_POOL_ENTRY_CAPACITY SAMPLE_PREP_MULTI_START_SLOT_BUDGET
#else
#define SAMPLE_GLOBAL_POOL_ENTRY_CAPACITY (208U)
#endif
#define SAMPLE_GLOBAL_POOL_BUDGET_BYTES \
    (SAMPLE_PAGE_SLOT_POOL_COUNT * SAMPLE_PAGE_BYTES)
#define SAMPLE_GLOBAL_POOL_PATH_MAX       SAMPLE_CLASSIC_PATH_MAX
#define SAMPLE_GLOBAL_POOL_INVALID_INDEX  (0xFFFFU)

_Static_assert(SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS <= SAMPLE_GLOBAL_POOL_FINAL_SLOTS,
               "active sample catalogue capacity must fit final catalogue");

typedef enum
{
    SAMPLE_GLOBAL_KIND_EMPTY = 0,
    SAMPLE_GLOBAL_KIND_CLASSIC,
    SAMPLE_GLOBAL_KIND_MULTI,
    SAMPLE_GLOBAL_KIND_RAM,
    SAMPLE_GLOBAL_KIND_WAVETABLE
} sample_global_kind_t;

typedef enum
{
    SAMPLE_GLOBAL_STATE_EMPTY = 0,
    SAMPLE_GLOBAL_STATE_LOADING,
    SAMPLE_GLOBAL_STATE_READY,
    SAMPLE_GLOBAL_STATE_ERROR,
    SAMPLE_GLOBAL_STATE_MISSING,
    SAMPLE_GLOBAL_STATE_RESERVED
} sample_global_state_t;

typedef enum
{
    SAMPLE_CLASSIC_SLOT_EMPTY = 0,
    SAMPLE_CLASSIC_SLOT_LOADED,
    SAMPLE_CLASSIC_SLOT_PREPARING,
    SAMPLE_CLASSIC_SLOT_ERROR,
    SAMPLE_CLASSIC_SLOT_MISSING
} sample_classic_slot_state_t;

typedef enum
{
    SAMPLE_CLASSIC_LOAD_OK = 0,
    SAMPLE_CLASSIC_LOAD_INVALID_ID,
    SAMPLE_CLASSIC_LOAD_INVALID_PATH,
    SAMPLE_CLASSIC_LOAD_PATH_TOO_LONG,
    SAMPLE_CLASSIC_LOAD_NO_FREE_SLOT,
    SAMPLE_CLASSIC_LOAD_SD_GATE_REFUSED,
    SAMPLE_CLASSIC_LOAD_SD_MOUNT_FAIL,
    SAMPLE_CLASSIC_LOAD_SD_FILE_NOT_FOUND,
    SAMPLE_CLASSIC_LOAD_SD_OPEN_FAIL,
    SAMPLE_CLASSIC_LOAD_WAV_PARSE_FAIL,
    SAMPLE_CLASSIC_LOAD_WAV_UNSUPPORTED_FORMAT,
    SAMPLE_CLASSIC_LOAD_WAV_48K_REQUIRED,
    SAMPLE_CLASSIC_LOAD_MEMORY_LIMIT,
    SAMPLE_CLASSIC_LOAD_SD_READ_FAIL,
    SAMPLE_CLASSIC_LOAD_SD_SEEK_FAIL,
    SAMPLE_CLASSIC_LOAD_SD_SHORT_READ,
    SAMPLE_CLASSIC_LOAD_SD_READ_INT_ERR,
    SAMPLE_CLASSIC_LOAD_SD_NOT_READY,
    SAMPLE_CLASSIC_LOAD_SD_INVALID_OBJECT,
    SAMPLE_CLASSIC_LOAD_SD_TIMEOUT,
    SAMPLE_CLASSIC_LOAD_SD_NOT_ENOUGH_CORE
} sample_classic_load_error_t;

typedef struct
{
    sample_global_kind_t kind;
    sample_global_state_t state;
    uint16_t backend_index;
    /* Multi records account for their instrument sample count here. */
    uint16_t entry_count;
    uint32_t cost_bytes;
    uint32_t flags;
    char path[SAMPLE_GLOBAL_POOL_PATH_MAX];
} sample_global_slot_t;

void sample_global_pool_init(void);
void sample_global_pool_reset(void);

uint16_t sample_global_pool_find_free_slot(void);
uint16_t sample_global_pool_find_first_ready(sample_global_kind_t kind);
uint16_t sample_global_pool_find_next_ready(sample_global_kind_t kind,
                                            uint16_t current,
                                            int8_t direction);
uint8_t sample_global_pool_find_by_backend(sample_global_kind_t kind,
                                           uint16_t backend_index,
                                           uint16_t *out_global_index);
uint8_t sample_global_pool_resolve_backend(uint16_t global_index,
                                           sample_global_kind_t expected_kind,
                                           uint16_t *out_backend_index);

uint8_t sample_global_pool_register_classic_at(uint16_t global_index,
                                               const char *path,
                                               uint32_t cost_bytes);
uint8_t sample_global_pool_load_classic(uint16_t global_index, const char *path);
void sample_global_pool_clear_classic(uint16_t global_index);
sample_classic_slot_state_t sample_global_pool_get_classic_state(uint16_t global_index);
sample_classic_load_error_t sample_global_pool_get_last_classic_load_error(void);
uint8_t sample_global_pool_register_multi(uint16_t instrument_id,
                                          const char *path,
                                          uint32_t cost_bytes,
                                          uint16_t entry_count,
                                          uint16_t *out_global_index);
uint8_t sample_global_pool_register_multi_loading_at(uint16_t global_index,
                                                     uint16_t instrument_id,
                                                     const char *path,
                                                     uint16_t entry_count);
uint8_t sample_global_pool_reserve_ram(uint16_t ram_slot,
                                       const char *path,
                                       uint32_t cost_bytes,
                                       uint16_t *out_global_index);
uint8_t sample_global_pool_register_ram(uint16_t ram_slot,
                                        const char *path,
                                        uint32_t cost_bytes,
                                        uint16_t *out_global_index);
uint8_t sample_global_pool_register_ram_at(uint16_t global_index,
                                           uint16_t ram_slot,
                                           const char *path,
                                           uint32_t cost_bytes);
uint8_t sample_global_pool_register_ram_error(uint16_t ram_slot,
                                              const char *path,
                                              uint16_t *out_global_index);
uint8_t sample_global_pool_register_ram_error_at(uint16_t global_index,
                                                 uint16_t ram_slot,
                                                 const char *path);
uint8_t sample_global_pool_reserve_wavetable(uint16_t wavetable_slot,
                                             const char *path,
                                             uint32_t cost_bytes,
                                             uint16_t *out_global_index);
uint8_t sample_global_pool_register_wavetable(uint16_t wavetable_slot,
                                              const char *path,
                                              uint32_t cost_bytes,
                                              uint16_t *out_global_index);
uint8_t sample_global_pool_register_wavetable_at(uint16_t global_index,
                                                 uint16_t wavetable_slot,
                                                 const char *path,
                                                 uint32_t cost_bytes);
uint8_t sample_global_pool_register_wavetable_error(uint16_t wavetable_slot,
                                                    const char *path,
                                                    uint16_t *out_global_index);
uint8_t sample_global_pool_register_wavetable_error_at(uint16_t global_index,
                                                       uint16_t wavetable_slot,
                                                       const char *path);

void sample_global_pool_clear_slot(uint16_t global_index);
void sample_global_pool_clear_backend(sample_global_kind_t kind, uint16_t backend_index);

const sample_global_slot_t *sample_global_pool_get_slot(uint16_t global_index);
uint16_t sample_global_pool_get_active_slot_capacity(void);
uint16_t sample_global_pool_get_used_slots(void);
uint16_t sample_global_pool_get_entry_capacity(void);
uint16_t sample_global_pool_get_used_entries(void);
uint32_t sample_global_pool_get_used_bytes(void);
uint32_t sample_global_pool_get_free_bytes(void);
uint8_t sample_global_pool_validate_entries(sample_global_kind_t kind,
                                            uint16_t backend_index,
                                            uint16_t entry_count);
uint8_t sample_global_pool_validate_budget(sample_global_kind_t kind,
                                           uint16_t backend_index,
                                           uint32_t cost_bytes);

#ifdef __cplusplus
}
#endif
