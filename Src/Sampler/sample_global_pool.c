#include "Sampler/sample_global_pool.h"

#include <string.h>

#include "Sampler/sample_cache.h"
#include "Platform/memory_layout.h"

STORAGE_STATE_SDRAM static sample_global_slot_t
    g_sample_global_pool[SAMPLE_GLOBAL_POOL_MAX_SLOTS];

static sample_classic_load_error_t g_sample_classic_last_error;

static uint8_t sample_global_kind_valid(sample_global_kind_t kind)
{
    return ((kind == SAMPLE_GLOBAL_KIND_CLASSIC)
            || (kind == SAMPLE_GLOBAL_KIND_MULTI)
            || (kind == SAMPLE_GLOBAL_KIND_RAM)
            || (kind == SAMPLE_GLOBAL_KIND_WAVETABLE)) ? 1U : 0U;
}

static uint8_t sample_global_copy_path(char *dst, uint32_t dst_size, const char *src)
{
    if ((dst == 0) || (dst_size == 0U))
    {
        return 0U;
    }

    dst[0] = '\0';
    if (src == 0)
    {
        return 1U;
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

static uint32_t sample_global_used_bytes_without(sample_global_kind_t kind,
                                                 uint16_t backend_index)
{
    uint32_t used = 0U;
    for (uint16_t i = 0U; i < SAMPLE_GLOBAL_POOL_MAX_SLOTS; ++i)
    {
        const sample_global_slot_t *const slot = &g_sample_global_pool[i];
        if (slot->kind == SAMPLE_GLOBAL_KIND_EMPTY)
        {
            continue;
        }
        if ((slot->kind == kind)
            && (((kind == SAMPLE_GLOBAL_KIND_CLASSIC) && (i == backend_index))
                || (slot->backend_index == backend_index)))
        {
            continue;
        }
        used += slot->cost_bytes;
    }
    return used;
}

static uint16_t sample_global_entry_count(const sample_global_slot_t *slot)
{
    if ((slot == 0) || (slot->kind == SAMPLE_GLOBAL_KIND_EMPTY))
    {
        return 0U;
    }

    return (slot->entry_count != 0U) ? slot->entry_count : 1U;
}

static uint16_t sample_global_used_entries_without(sample_global_kind_t kind,
                                                   uint16_t backend_index)
{
    uint32_t used = 0U;
    for (uint16_t i = 0U; i < SAMPLE_GLOBAL_POOL_MAX_SLOTS; ++i)
    {
        const sample_global_slot_t *const slot = &g_sample_global_pool[i];
        if (slot->kind == SAMPLE_GLOBAL_KIND_EMPTY)
        {
            continue;
        }
        if ((slot->kind == kind)
            && (((kind == SAMPLE_GLOBAL_KIND_CLASSIC) && (i == backend_index))
                || (slot->backend_index == backend_index)))
        {
            continue;
        }
        used += sample_global_entry_count(slot);
    }

    return (used > UINT16_MAX) ? UINT16_MAX : (uint16_t)used;
}

void sample_global_pool_init(void)
{
    sample_global_pool_reset();
}

void sample_global_pool_reset(void)
{
    memset(g_sample_global_pool, 0, sizeof(g_sample_global_pool));
    for (uint16_t i = 0U; i < SAMPLE_GLOBAL_POOL_MAX_SLOTS; ++i)
    {
        g_sample_global_pool[i].kind = SAMPLE_GLOBAL_KIND_EMPTY;
        g_sample_global_pool[i].state = SAMPLE_GLOBAL_STATE_EMPTY;
        g_sample_global_pool[i].backend_index = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        g_sample_global_pool[i].entry_count = 0U;
    }
    g_sample_classic_last_error = SAMPLE_CLASSIC_LOAD_OK;
}

uint16_t sample_global_pool_find_free_slot(void)
{
    for (uint16_t i = 0U; i < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS; ++i)
    {
        if (g_sample_global_pool[i].kind == SAMPLE_GLOBAL_KIND_EMPTY)
        {
            return i;
        }
    }
    return SAMPLE_GLOBAL_POOL_INVALID_INDEX;
}

uint16_t sample_global_pool_find_first_ready(sample_global_kind_t kind)
{
    return sample_global_pool_find_next_ready(kind, SAMPLE_GLOBAL_POOL_INVALID_INDEX, 1);
}

uint16_t sample_global_pool_find_next_ready(sample_global_kind_t kind,
                                            uint16_t current,
                                            int8_t direction)
{
    if ((sample_global_kind_valid(kind) == 0U) || (direction == 0))
    {
        return SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }

    int32_t index = (current == SAMPLE_GLOBAL_POOL_INVALID_INDEX)
        ? ((direction > 0) ? 0 : ((int32_t)SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS - 1))
        : (int32_t)current + ((direction > 0) ? 1 : -1);
    const int32_t end = (direction > 0) ? (int32_t)SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS : -1;
    while (index != end)
    {
        const sample_global_slot_t *const slot = &g_sample_global_pool[index];
        if ((slot->kind == kind) && (slot->state == SAMPLE_GLOBAL_STATE_READY))
        {
            return (uint16_t)index;
        }
        index += (direction > 0) ? 1 : -1;
    }
    return SAMPLE_GLOBAL_POOL_INVALID_INDEX;
}

uint8_t sample_global_pool_find_by_backend(sample_global_kind_t kind,
                                           uint16_t backend_index,
                                           uint16_t *out_global_index)
{
    if (out_global_index != 0)
    {
        *out_global_index = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }
    if (sample_global_kind_valid(kind) == 0U)
    {
        return 0U;
    }
    if (kind == SAMPLE_GLOBAL_KIND_CLASSIC)
    {
        if ((backend_index < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
            && (g_sample_global_pool[backend_index].kind == SAMPLE_GLOBAL_KIND_CLASSIC))
        {
            if (out_global_index != 0) *out_global_index = backend_index;
            return 1U;
        }
        return 0U;
    }

    for (uint16_t i = 0U; i < SAMPLE_GLOBAL_POOL_MAX_SLOTS; ++i)
    {
        const sample_global_slot_t *const slot = &g_sample_global_pool[i];
        if ((slot->kind == kind) && (slot->backend_index == backend_index))
        {
            if (out_global_index != 0)
            {
                *out_global_index = i;
            }
            return 1U;
        }
    }
    return 0U;
}

uint8_t sample_global_pool_resolve_backend(uint16_t global_index,
                                           sample_global_kind_t expected_kind,
                                           uint16_t *out_backend_index)
{
    if (out_backend_index != 0)
    {
        *out_backend_index = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }
    if ((global_index >= SAMPLE_GLOBAL_POOL_MAX_SLOTS)
        || (sample_global_kind_valid(expected_kind) == 0U))
    {
        return 0U;
    }

    const sample_global_slot_t *const slot = &g_sample_global_pool[global_index];
    if (slot->kind != expected_kind)
    {
        return 0U;
    }
    if (expected_kind == SAMPLE_GLOBAL_KIND_CLASSIC)
    {
        if (out_backend_index != 0) *out_backend_index = global_index;
        return 1U;
    }
    if (slot->backend_index == SAMPLE_GLOBAL_POOL_INVALID_INDEX)
    {
        return 0U;
    }

    if (out_backend_index != 0)
    {
        *out_backend_index = slot->backend_index;
    }
    return 1U;
}

uint8_t sample_global_pool_validate_entries(sample_global_kind_t kind,
                                            uint16_t backend_index,
                                            uint16_t entry_count)
{
    if ((sample_global_kind_valid(kind) == 0U)
        || (backend_index == SAMPLE_GLOBAL_POOL_INVALID_INDEX)
        || (entry_count == 0U)
        || (entry_count > SAMPLE_GLOBAL_POOL_ENTRY_CAPACITY))
    {
        return 0U;
    }

    const uint32_t used_without_old =
        sample_global_used_entries_without(kind, backend_index);
    return (used_without_old <= SAMPLE_GLOBAL_POOL_ENTRY_CAPACITY)
        && ((uint32_t)entry_count
            <= ((uint32_t)SAMPLE_GLOBAL_POOL_ENTRY_CAPACITY - used_without_old));
}

static uint8_t sample_global_pool_register(sample_global_kind_t kind,
                                           sample_global_state_t state,
                                           uint16_t backend_index,
                                           const char *path,
                                           uint32_t cost_bytes,
                                           uint16_t entry_count,
                                           uint16_t *out_global_index)
{
    uint16_t global_index = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (out_global_index != 0)
    {
        *out_global_index = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }
    if ((sample_global_kind_valid(kind) == 0U)
        || (backend_index == SAMPLE_GLOBAL_POOL_INVALID_INDEX)
        || (sample_global_pool_validate_entries(kind, backend_index, entry_count) == 0U)
        || (sample_global_pool_validate_budget(kind, backend_index, cost_bytes) == 0U))
    {
        return 0U;
    }

    if (sample_global_pool_find_by_backend(kind, backend_index, &global_index) == 0U)
    {
        global_index = sample_global_pool_find_free_slot();
    }
    if (global_index == SAMPLE_GLOBAL_POOL_INVALID_INDEX)
    {
        return 0U;
    }

    sample_global_slot_t *const slot = &g_sample_global_pool[global_index];
    memset(slot, 0, sizeof(*slot));
    slot->kind = kind;
    slot->state = state;
    slot->backend_index = backend_index;
    slot->entry_count = entry_count;
    slot->cost_bytes = cost_bytes;
    if (sample_global_copy_path(slot->path, sizeof(slot->path), path) == 0U)
    {
        sample_global_pool_clear_slot(global_index);
        return 0U;
    }

    if (out_global_index != 0)
    {
        *out_global_index = global_index;
    }
    return 1U;
}

static uint8_t sample_global_pool_register_at(sample_global_kind_t kind,
                                              sample_global_state_t state,
                                              uint16_t global_index,
                                              uint16_t backend_index,
                                              const char *path,
                                              uint32_t cost_bytes,
                                              uint16_t entry_count)
{
    if ((global_index >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
        || (sample_global_kind_valid(kind) == 0U)
        || (backend_index == SAMPLE_GLOBAL_POOL_INVALID_INDEX)
        || (sample_global_pool_validate_entries(kind, backend_index, entry_count) == 0U)
        || (sample_global_pool_validate_budget(kind, backend_index, cost_bytes) == 0U))
    {
        return 0U;
    }

    sample_global_slot_t *const slot = &g_sample_global_pool[global_index];
    if ((slot->kind != SAMPLE_GLOBAL_KIND_EMPTY)
        && !((slot->kind == kind) && (slot->backend_index == backend_index)))
    {
        return 0U;
    }

    uint16_t existing_global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if ((sample_global_pool_find_by_backend(kind, backend_index, &existing_global) != 0U)
        && (existing_global != global_index))
    {
        sample_global_pool_clear_slot(existing_global);
    }

    memset(slot, 0, sizeof(*slot));
    slot->kind = kind;
    slot->state = state;
    slot->backend_index = backend_index;
    slot->entry_count = entry_count;
    slot->cost_bytes = cost_bytes;
    if (sample_global_copy_path(slot->path, sizeof(slot->path), path) == 0U)
    {
        sample_global_pool_clear_slot(global_index);
        return 0U;
    }
    return 1U;
}

uint8_t sample_global_pool_register_classic_at(uint16_t global_index,
                                               const char *path,
                                               uint32_t cost_bytes)
{
    if ((global_index >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
        || ((g_sample_global_pool[global_index].kind != SAMPLE_GLOBAL_KIND_EMPTY)
            && (g_sample_global_pool[global_index].kind != SAMPLE_GLOBAL_KIND_CLASSIC))
        || (sample_global_pool_validate_entries(SAMPLE_GLOBAL_KIND_CLASSIC,
                                                global_index, 1U) == 0U)
        || (sample_global_pool_validate_budget(SAMPLE_GLOBAL_KIND_CLASSIC,
                                               global_index, cost_bytes) == 0U))
        return 0U;
    sample_global_slot_t *const slot = &g_sample_global_pool[global_index];
    memset(slot, 0, sizeof(*slot));
    slot->kind = SAMPLE_GLOBAL_KIND_CLASSIC;
    slot->state = SAMPLE_GLOBAL_STATE_READY;
    slot->backend_index = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    slot->entry_count = 1U;
    slot->cost_bytes = cost_bytes;
    if (sample_global_copy_path(slot->path, sizeof(slot->path), path) == 0U)
    {
        sample_global_pool_clear_slot(global_index);
        return 0U;
    }
    return 1U;
}

static sample_classic_load_error_t sample_global_classic_error_from_cache(uint16_t id)
{
    const uint8_t error = sample_cache_get_last_error(id);
    const FRESULT fr = (FRESULT)sample_cache_get_last_fresult(id);
    if (fr == FR_TIMEOUT) return SAMPLE_CLASSIC_LOAD_SD_GATE_REFUSED;
    if (fr == FR_DISK_ERR) return SAMPLE_CLASSIC_LOAD_SD_MOUNT_FAIL;
    if (fr == FR_NO_FILE) return SAMPLE_CLASSIC_LOAD_SD_FILE_NOT_FOUND;
    if (fr == FR_INVALID_NAME) return SAMPLE_CLASSIC_LOAD_INVALID_PATH;
    if (fr == FR_NOT_ENOUGH_CORE) return SAMPLE_CLASSIC_LOAD_SD_NOT_ENOUGH_CORE;
    switch (error)
    {
        case 4U: return SAMPLE_CLASSIC_LOAD_SD_OPEN_FAIL;
        case 5U: return SAMPLE_CLASSIC_LOAD_WAV_PARSE_FAIL;
        case 6U: return SAMPLE_CLASSIC_LOAD_WAV_UNSUPPORTED_FORMAT;
        case 7U: return SAMPLE_CLASSIC_LOAD_WAV_UNSUPPORTED_FORMAT;
        case 8U: return SAMPLE_CLASSIC_LOAD_MEMORY_LIMIT;
        case 9U: return SAMPLE_CLASSIC_LOAD_SD_SEEK_FAIL;
        case 15U: return SAMPLE_CLASSIC_LOAD_WAV_48K_REQUIRED;
        case 13U:
        case 14U: return SAMPLE_CLASSIC_LOAD_SD_SHORT_READ;
        default: return SAMPLE_CLASSIC_LOAD_SD_READ_FAIL;
    }
}

uint8_t sample_global_pool_load_classic(uint16_t global_index, const char *path)
{
    if (global_index >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
    {
        g_sample_classic_last_error = SAMPLE_CLASSIC_LOAD_INVALID_ID;
        return 0U;
    }
    if ((g_sample_global_pool[global_index].kind != SAMPLE_GLOBAL_KIND_EMPTY)
        && (g_sample_global_pool[global_index].kind != SAMPLE_GLOBAL_KIND_CLASSIC))
    {
        g_sample_classic_last_error = SAMPLE_CLASSIC_LOAD_NO_FREE_SLOT;
        return 0U;
    }
    if ((path == 0) || (path[0] == '\0'))
    {
        g_sample_classic_last_error = SAMPLE_CLASSIC_LOAD_INVALID_PATH;
        return 0U;
    }
    if (strlen(path) >= SAMPLE_GLOBAL_POOL_PATH_MAX)
    {
        g_sample_classic_last_error = SAMPLE_CLASSIC_LOAD_PATH_TOO_LONG;
        return 0U;
    }
    if (sample_cache_prepare(global_index, path) == 0U)
    {
        g_sample_classic_last_error = sample_global_classic_error_from_cache(global_index);
        return 0U;
    }
    g_sample_classic_last_error = SAMPLE_CLASSIC_LOAD_OK;
    return 1U;
}

void sample_global_pool_clear_classic(uint16_t global_index)
{
    if (global_index >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS) return;
    if (g_sample_global_pool[global_index].kind == SAMPLE_GLOBAL_KIND_CLASSIC)
    {
        sample_global_pool_clear_slot(global_index);
    }
}

sample_classic_slot_state_t sample_global_pool_get_classic_state(uint16_t global_index)
{
    if (global_index >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
        return SAMPLE_CLASSIC_SLOT_EMPTY;
    const sample_global_slot_t *const slot = &g_sample_global_pool[global_index];
    if (slot->kind != SAMPLE_GLOBAL_KIND_CLASSIC)
        return SAMPLE_CLASSIC_SLOT_EMPTY;
    switch (sample_cache_get_slot_readiness(global_index))
    {
        case SAMPLE_CACHE_SLOT_PLAYABLE: return SAMPLE_CLASSIC_SLOT_LOADED;
        case SAMPLE_CACHE_SLOT_PREPARING:
        case SAMPLE_CACHE_SLOT_START_PENDING:
        case SAMPLE_CACHE_SLOT_NEEDS_REPREPARE: return SAMPLE_CLASSIC_SLOT_PREPARING;
        case SAMPLE_CACHE_SLOT_ERROR: return SAMPLE_CLASSIC_SLOT_ERROR;
        default: return (slot->path[0] != '\0')
                            ? SAMPLE_CLASSIC_SLOT_MISSING
                            : SAMPLE_CLASSIC_SLOT_EMPTY;
    }
}

sample_classic_load_error_t sample_global_pool_get_last_classic_load_error(void)
{
    return g_sample_classic_last_error;
}

uint8_t sample_global_pool_register_multi(uint16_t instrument_id,
                                           const char *path,
                                           uint32_t cost_bytes,
                                           uint16_t entry_count,
                                           uint16_t *out_global_index)
{
    return sample_global_pool_register(SAMPLE_GLOBAL_KIND_MULTI,
                                       SAMPLE_GLOBAL_STATE_READY,
                                       instrument_id,
                                       path,
                                       cost_bytes,
                                       entry_count,
                                       out_global_index);
}

uint8_t sample_global_pool_register_multi_loading_at(uint16_t global_index,
                                                      uint16_t instrument_id,
                                                      const char *path,
                                                      uint16_t entry_count)
{
    return sample_global_pool_register_at(SAMPLE_GLOBAL_KIND_MULTI,
                                          SAMPLE_GLOBAL_STATE_LOADING,
                                           global_index,
                                           instrument_id,
                                           path,
                                           0U,
                                           entry_count);
}

uint8_t sample_global_pool_reserve_ram(uint16_t ram_slot,
                                       const char *path,
                                       uint32_t cost_bytes,
                                       uint16_t *out_global_index)
{
    return sample_global_pool_register(SAMPLE_GLOBAL_KIND_RAM,
                                       SAMPLE_GLOBAL_STATE_RESERVED,
                                       ram_slot,
                                       path,
                                       cost_bytes,
                                       1U,
                                       out_global_index);
}

uint8_t sample_global_pool_register_ram(uint16_t ram_slot,
                                        const char *path,
                                        uint32_t cost_bytes,
                                        uint16_t *out_global_index)
{
    return sample_global_pool_register(SAMPLE_GLOBAL_KIND_RAM,
                                       SAMPLE_GLOBAL_STATE_READY,
                                       ram_slot,
                                       path,
                                       cost_bytes,
                                       1U,
                                       out_global_index);
}

uint8_t sample_global_pool_register_ram_at(uint16_t global_index,
                                           uint16_t ram_slot,
                                           const char *path,
                                           uint32_t cost_bytes)
{
    return sample_global_pool_register_at(SAMPLE_GLOBAL_KIND_RAM,
                                          SAMPLE_GLOBAL_STATE_READY,
                                          global_index,
                                           ram_slot,
                                           path,
                                           cost_bytes,
                                           1U);
}

uint8_t sample_global_pool_register_ram_error(uint16_t ram_slot,
                                              const char *path,
                                              uint16_t *out_global_index)
{
    return sample_global_pool_register(SAMPLE_GLOBAL_KIND_RAM,
                                       SAMPLE_GLOBAL_STATE_ERROR,
                                       ram_slot,
                                       path,
                                       0U,
                                       1U,
                                       out_global_index);
}

uint8_t sample_global_pool_register_ram_error_at(uint16_t global_index,
                                                 uint16_t ram_slot,
                                                 const char *path)
{
    return sample_global_pool_register_at(SAMPLE_GLOBAL_KIND_RAM,
                                          SAMPLE_GLOBAL_STATE_ERROR,
                                          global_index,
                                           ram_slot,
                                           path,
                                           0U,
                                           1U);
}

uint8_t sample_global_pool_reserve_wavetable(uint16_t wavetable_slot,
                                             const char *path,
                                             uint32_t cost_bytes,
                                             uint16_t *out_global_index)
{
    return sample_global_pool_register(SAMPLE_GLOBAL_KIND_WAVETABLE,
                                       SAMPLE_GLOBAL_STATE_RESERVED,
                                       wavetable_slot,
                                       path,
                                       cost_bytes,
                                       1U,
                                       out_global_index);
}

uint8_t sample_global_pool_register_wavetable(uint16_t wavetable_slot,
                                              const char *path,
                                              uint32_t cost_bytes,
                                              uint16_t *out_global_index)
{
    return sample_global_pool_register(SAMPLE_GLOBAL_KIND_WAVETABLE,
                                       SAMPLE_GLOBAL_STATE_READY,
                                       wavetable_slot,
                                       path,
                                       cost_bytes,
                                       1U,
                                       out_global_index);
}

uint8_t sample_global_pool_register_wavetable_at(uint16_t global_index,
                                                 uint16_t wavetable_slot,
                                                 const char *path,
                                                 uint32_t cost_bytes)
{
    return sample_global_pool_register_at(SAMPLE_GLOBAL_KIND_WAVETABLE,
                                          SAMPLE_GLOBAL_STATE_READY,
                                          global_index,
                                           wavetable_slot,
                                           path,
                                           cost_bytes,
                                           1U);
}

uint8_t sample_global_pool_register_wavetable_error(uint16_t wavetable_slot,
                                                    const char *path,
                                                    uint16_t *out_global_index)
{
    return sample_global_pool_register(SAMPLE_GLOBAL_KIND_WAVETABLE,
                                       SAMPLE_GLOBAL_STATE_ERROR,
                                       wavetable_slot,
                                       path,
                                       0U,
                                       1U,
                                       out_global_index);
}

uint8_t sample_global_pool_register_wavetable_error_at(uint16_t global_index,
                                                       uint16_t wavetable_slot,
                                                       const char *path)
{
    return sample_global_pool_register_at(SAMPLE_GLOBAL_KIND_WAVETABLE,
                                          SAMPLE_GLOBAL_STATE_ERROR,
                                          global_index,
                                           wavetable_slot,
                                           path,
                                           0U,
                                           1U);
}

void sample_global_pool_clear_slot(uint16_t global_index)
{
    if (global_index >= SAMPLE_GLOBAL_POOL_MAX_SLOTS)
    {
        return;
    }

    if (g_sample_global_pool[global_index].kind == SAMPLE_GLOBAL_KIND_CLASSIC)
    {
        sample_cache_clear(global_index);
    }
    memset(&g_sample_global_pool[global_index], 0, sizeof(g_sample_global_pool[global_index]));
    g_sample_global_pool[global_index].kind = SAMPLE_GLOBAL_KIND_EMPTY;
    g_sample_global_pool[global_index].state = SAMPLE_GLOBAL_STATE_EMPTY;
    g_sample_global_pool[global_index].backend_index = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    g_sample_global_pool[global_index].entry_count = 0U;
}

void sample_global_pool_clear_backend(sample_global_kind_t kind, uint16_t backend_index)
{
    if (kind == SAMPLE_GLOBAL_KIND_CLASSIC)
    {
        sample_global_pool_clear_classic(backend_index);
        return;
    }
    uint16_t global_index = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (sample_global_pool_find_by_backend(kind, backend_index, &global_index) != 0U)
    {
        sample_global_pool_clear_slot(global_index);
    }
}

const sample_global_slot_t *sample_global_pool_get_slot(uint16_t global_index)
{
    if (global_index >= SAMPLE_GLOBAL_POOL_MAX_SLOTS)
    {
        return 0;
    }
    return &g_sample_global_pool[global_index];
}

uint16_t sample_global_pool_get_active_slot_capacity(void)
{
    return SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;
}

uint16_t sample_global_pool_get_used_slots(void)
{
    uint16_t used = 0U;
    for (uint16_t i = 0U; i < SAMPLE_GLOBAL_POOL_MAX_SLOTS; ++i)
    {
        if (g_sample_global_pool[i].kind != SAMPLE_GLOBAL_KIND_EMPTY)
        {
            used++;
        }
    }
    return used;
}

uint16_t sample_global_pool_get_entry_capacity(void)
{
    return SAMPLE_GLOBAL_POOL_ENTRY_CAPACITY;
}

uint16_t sample_global_pool_get_used_entries(void)
{
    uint32_t used = 0U;
    for (uint16_t i = 0U; i < SAMPLE_GLOBAL_POOL_MAX_SLOTS; ++i)
    {
        used += sample_global_entry_count(&g_sample_global_pool[i]);
    }
    return (used > UINT16_MAX) ? UINT16_MAX : (uint16_t)used;
}

uint32_t sample_global_pool_get_used_bytes(void)
{
    uint32_t used = 0U;
    for (uint16_t i = 0U; i < SAMPLE_GLOBAL_POOL_MAX_SLOTS; ++i)
    {
        if (g_sample_global_pool[i].kind != SAMPLE_GLOBAL_KIND_EMPTY)
        {
            used += g_sample_global_pool[i].cost_bytes;
        }
    }
    return (used > SAMPLE_GLOBAL_POOL_BUDGET_BYTES)
        ? SAMPLE_GLOBAL_POOL_BUDGET_BYTES
        : used;
}

uint32_t sample_global_pool_get_free_bytes(void)
{
    const uint32_t used = sample_global_pool_get_used_bytes();
    return (used >= SAMPLE_GLOBAL_POOL_BUDGET_BYTES)
        ? 0U
        : (SAMPLE_GLOBAL_POOL_BUDGET_BYTES - used);
}

uint8_t sample_global_pool_validate_budget(sample_global_kind_t kind,
                                           uint16_t backend_index,
                                           uint32_t cost_bytes)
{
    uint16_t existing = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if ((sample_global_kind_valid(kind) == 0U)
        || (backend_index == SAMPLE_GLOBAL_POOL_INVALID_INDEX)
        || (cost_bytes > SAMPLE_GLOBAL_POOL_BUDGET_BYTES))
    {
        return 0U;
    }

    if ((sample_global_pool_find_by_backend(kind, backend_index, &existing) == 0U)
        && (sample_global_pool_find_free_slot() == SAMPLE_GLOBAL_POOL_INVALID_INDEX))
    {
        return 0U;
    }

    const uint32_t used_without_old =
        sample_global_used_bytes_without(kind, backend_index);
    if (used_without_old > SAMPLE_GLOBAL_POOL_BUDGET_BYTES)
    {
        return 0U;
    }

    return (cost_bytes <= (SAMPLE_GLOBAL_POOL_BUDGET_BYTES - used_without_old)) ? 1U : 0U;
}
