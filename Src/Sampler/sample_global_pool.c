#include "Sampler/sample_global_pool.h"

#include <string.h>

#include "Storage/memory_layout.h"

STORAGE_STATE_SDRAM static sample_global_slot_t
    g_sample_global_pool[SAMPLE_GLOBAL_POOL_MAX_SLOTS];

_Static_assert(SAMPLE_POOL_SIZE >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,
               "stream backend must cover active global sample slots");

static uint8_t sample_global_kind_valid(sample_global_kind_t kind)
{
    return ((kind == SAMPLE_GLOBAL_KIND_STREAM)
            || (kind == SAMPLE_GLOBAL_KIND_MULTI)
            || (kind == SAMPLE_GLOBAL_KIND_RAM)) ? 1U : 0U;
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
        if ((slot->kind == kind) && (slot->backend_index == backend_index))
        {
            continue;
        }
        used += slot->cost_bytes;
    }
    return used;
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
    }
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
    if ((slot->kind != expected_kind)
        || (slot->backend_index == SAMPLE_GLOBAL_POOL_INVALID_INDEX))
    {
        return 0U;
    }

    if (out_backend_index != 0)
    {
        *out_backend_index = slot->backend_index;
    }
    return 1U;
}

static uint8_t sample_global_pool_register(sample_global_kind_t kind,
                                           sample_global_state_t state,
                                           uint16_t backend_index,
                                           const char *path,
                                           uint32_t cost_bytes,
                                           uint16_t *out_global_index)
{
    uint16_t global_index = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (out_global_index != 0)
    {
        *out_global_index = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }
    if ((sample_global_kind_valid(kind) == 0U)
        || (backend_index == SAMPLE_GLOBAL_POOL_INVALID_INDEX)
        || (sample_global_pool_validate_budget(kind, backend_index, cost_bytes) == 0U))
    {
        return 0U;
    }

    if (sample_global_pool_find_by_backend(kind, backend_index, &global_index) == 0U)
    {
        global_index = sample_global_pool_find_free_slot();
    }
    if (global_index >= SAMPLE_GLOBAL_POOL_MAX_SLOTS)
    {
        return 0U;
    }

    sample_global_slot_t *const slot = &g_sample_global_pool[global_index];
    memset(slot, 0, sizeof(*slot));
    slot->kind = kind;
    slot->state = state;
    slot->backend_index = backend_index;
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
                                              uint32_t cost_bytes)
{
    if ((global_index >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
        || (sample_global_kind_valid(kind) == 0U)
        || (backend_index == SAMPLE_GLOBAL_POOL_INVALID_INDEX)
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
    slot->cost_bytes = cost_bytes;
    if (sample_global_copy_path(slot->path, sizeof(slot->path), path) == 0U)
    {
        sample_global_pool_clear_slot(global_index);
        return 0U;
    }
    return 1U;
}

uint8_t sample_global_pool_register_stream(uint16_t stream_slot,
                                           const char *path,
                                           uint32_t cost_bytes,
                                           uint16_t *out_global_index)
{
    return sample_global_pool_register(SAMPLE_GLOBAL_KIND_STREAM,
                                       SAMPLE_GLOBAL_STATE_READY,
                                       stream_slot,
                                       path,
                                       cost_bytes,
                                       out_global_index);
}

uint8_t sample_global_pool_register_stream_at(uint16_t global_index,
                                              uint16_t stream_slot,
                                              const char *path,
                                              uint32_t cost_bytes)
{
    return sample_global_pool_register_at(SAMPLE_GLOBAL_KIND_STREAM,
                                          SAMPLE_GLOBAL_STATE_READY,
                                          global_index,
                                          stream_slot,
                                          path,
                                          cost_bytes);
}

uint8_t sample_global_pool_register_multi(uint16_t instrument_id,
                                          const char *path,
                                          uint32_t cost_bytes,
                                          uint16_t *out_global_index)
{
    return sample_global_pool_register(SAMPLE_GLOBAL_KIND_MULTI,
                                       SAMPLE_GLOBAL_STATE_READY,
                                       instrument_id,
                                       path,
                                       cost_bytes,
                                       out_global_index);
}

uint8_t sample_global_pool_register_multi_loading_at(uint16_t global_index,
                                                     uint16_t instrument_id,
                                                     const char *path)
{
    return sample_global_pool_register_at(SAMPLE_GLOBAL_KIND_MULTI,
                                          SAMPLE_GLOBAL_STATE_LOADING,
                                          global_index,
                                          instrument_id,
                                          path,
                                          0U);
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
                                          cost_bytes);
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
                                          0U);
}

void sample_global_pool_clear_slot(uint16_t global_index)
{
    if (global_index >= SAMPLE_GLOBAL_POOL_MAX_SLOTS)
    {
        return;
    }

    memset(&g_sample_global_pool[global_index], 0, sizeof(g_sample_global_pool[global_index]));
    g_sample_global_pool[global_index].kind = SAMPLE_GLOBAL_KIND_EMPTY;
    g_sample_global_pool[global_index].state = SAMPLE_GLOBAL_STATE_EMPTY;
    g_sample_global_pool[global_index].backend_index = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
}

void sample_global_pool_clear_backend(sample_global_kind_t kind, uint16_t backend_index)
{
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
        && (sample_global_pool_find_free_slot() >= SAMPLE_GLOBAL_POOL_MAX_SLOTS))
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
