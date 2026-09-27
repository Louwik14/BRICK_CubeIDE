#include "Storage/ram_load_trace.h"

#include "Storage/sd_access_gate.h"
#include "stm32h7xx_hal.h"

_Static_assert(sizeof(ram_load_trace_entry_t) == 32U,
               "RAM load trace entry layout changed");
_Static_assert(sizeof(ram_load_trace_block_t) == 2080U,
               "RAM load trace block layout changed");

__attribute__((used, externally_visible, aligned(4), section(".data.ram_load_trace")))
volatile ram_load_trace_block_t g_ram_load_trace = {
    .magic = RAM_LOAD_TRACE_MAGIC,
    .version = RAM_LOAD_TRACE_VERSION,
    .entry_size = sizeof(ram_load_trace_entry_t),
    .capacity = RAM_LOAD_TRACE_CAPACITY
};

static uint32_t g_ram_load_trace_sequence;
static uint32_t g_ram_load_trace_active_sequence;

void ram_load_trace_write(uint32_t event,
                          uint32_t sequence,
                          uint32_t project_state,
                          uint32_t pool_state,
                          uint32_t flags,
                          uint32_t arg0,
                          uint32_t arg1)
{
    const uint32_t write = g_ram_load_trace.write_count;
    volatile ram_load_trace_entry_t *const entry =
        &g_ram_load_trace.entries[write % RAM_LOAD_TRACE_CAPACITY];
    entry->tick_ms = HAL_GetTick();
    entry->event = event;
    entry->sequence = sequence;
    entry->project_state = project_state;
    entry->pool_state = pool_state;
    entry->owner_flags = ((uint32_t)sd_access_gate_current_owner() << 24U)
        | (flags & 0x00FFFFFFUL);
    entry->arg0 = arg0;
    entry->arg1 = arg1;
    __DMB();
    g_ram_load_trace.write_count = write + 1U;
}

uint32_t ram_load_trace_next_sequence(void)
{
    uint32_t sequence = ++g_ram_load_trace_sequence;
    if (sequence == 0U) sequence = ++g_ram_load_trace_sequence;
    return sequence;
}

void ram_load_trace_set_active_sequence(uint32_t sequence)
{
    g_ram_load_trace_active_sequence = sequence;
}

uint32_t ram_load_trace_active_sequence(void)
{
    return g_ram_load_trace_active_sequence;
}

uint32_t ram_load_trace_path_hash(const char *path)
{
    uint32_t hash = 2166136261UL;
    if (path == 0) return 0U;
    while (*path != '\0')
    {
        hash ^= (uint8_t)*path++;
        hash *= 16777619UL;
    }
    return hash;
}

