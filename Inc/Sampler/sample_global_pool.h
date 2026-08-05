#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_GLOBAL_POOL_FINAL_SLOTS    (256U)
#define SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS   SAMPLE_PAGE_PRODUCT_MAX_LONG_SAMPLE_SLOTS
#define SAMPLE_GLOBAL_POOL_MAX_SLOTS      SAMPLE_GLOBAL_POOL_FINAL_SLOTS
/* Product-wide content entries: Classic, RAM, Multi samples and wavetables. */
#define SAMPLE_GLOBAL_POOL_ENTRY_CAPACITY (208U)
#define SAMPLE_GLOBAL_POOL_BUDGET_BYTES   (20U * 1024U * 1024U)
#define SAMPLE_GLOBAL_POOL_PATH_MAX       (160U)
#define SAMPLE_GLOBAL_POOL_INVALID_INDEX  (0xFFFFU)

_Static_assert(SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS <= SAMPLE_GLOBAL_POOL_FINAL_SLOTS,
               "active sample catalogue capacity must fit final catalogue");

typedef enum
{
    SAMPLE_GLOBAL_KIND_EMPTY = 0,
    SAMPLE_GLOBAL_KIND_STREAM,
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

uint8_t sample_global_pool_register_stream(uint16_t stream_slot,
                                           const char *path,
                                           uint32_t cost_bytes,
                                           uint16_t *out_global_index);
uint8_t sample_global_pool_register_stream_at(uint16_t global_index,
                                              uint16_t stream_slot,
                                              const char *path,
                                              uint32_t cost_bytes);
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
