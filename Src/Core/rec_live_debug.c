#include "Core/rec_live_debug.h"

#include "Storage/memory_layout.h"
#include "stm32h7xx_hal.h"

#include <string.h>

CTRL_STATE static rec_live_debug_state_t g_rec_live_debug;

static void rec_live_debug_ensure_init(void)
{
    if((g_rec_live_debug.magic == REC_LIVE_DEBUG_MAGIC)
            && (g_rec_live_debug.capacity == REC_LIVE_DEBUG_RING_CAPACITY))
    {
        return;
    }
    memset(&g_rec_live_debug, 0, sizeof(g_rec_live_debug));
    g_rec_live_debug.magic = REC_LIVE_DEBUG_MAGIC;
    g_rec_live_debug.capacity = REC_LIVE_DEBUG_RING_CAPACITY;
}

static uint32_t rec_live_debug_now(void)
{
    return HAL_GetTick();
}

uint32_t rec_live_debug_path_hash(const char *path)
{
    uint32_t hash = 2166136261UL;
    if(path == 0)
    {
        return hash;
    }
    for(uint32_t i = 0U; path[i] != '\0'; ++i)
    {
        uint8_t c = (uint8_t)path[i];
        if(c == '\\')
        {
            c = '/';
        }
        if((c >= 'a') && (c <= 'z'))
        {
            c = (uint8_t)(c - ('a' - 'A'));
        }
        hash ^= (uint32_t)c;
        hash *= 16777619UL;
    }
    return hash;
}

void rec_live_debug_mark(uint32_t code,
                         uint32_t recorded_frames,
                         uint32_t wav_path_hash,
                         uint32_t writer_state,
                         uint32_t sample_state,
                         uint32_t last_error)
{
    rec_live_debug_ensure_init();

    uint32_t lr = 0U;
    __asm volatile("mov %0, lr" : "=r"(lr));

    rec_live_debug_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.code = code;
    entry.tick = rec_live_debug_now();
    entry.recorded_frames = recorded_frames;
    entry.wav_path_hash = wav_path_hash;
    entry.writer_state = writer_state;
    entry.sample_state = sample_state;
    entry.last_error = last_error;
    entry.pc = (uint32_t)__builtin_return_address(0);
    entry.lr = lr;
    entry.cfsr = SCB->CFSR;
    entry.hfsr = SCB->HFSR;
    entry.bfar = SCB->BFAR;
    entry.mmfar = SCB->MMFAR;

    const uint32_t idx = g_rec_live_debug.write_index % REC_LIVE_DEBUG_RING_CAPACITY;
    g_rec_live_debug.entries[idx] = entry;
    g_rec_live_debug.write_index = (idx + 1U) % REC_LIVE_DEBUG_RING_CAPACITY;
}

void rec_live_debug_hardfault(uint32_t *sp)
{
    rec_live_debug_ensure_init();

    uint32_t lr = 0U;
    __asm volatile("mov %0, lr" : "=r"(lr));

    rec_live_debug_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.tick = rec_live_debug_now();
    entry.pc = (sp != 0) ? sp[6] : 0U;
    entry.lr = lr;
    entry.cfsr = SCB->CFSR;
    entry.hfsr = SCB->HFSR;
    entry.bfar = SCB->BFAR;
    entry.mmfar = SCB->MMFAR;

    if(sp != 0)
    {
        g_rec_live_debug.stacked_r0 = sp[0];
        g_rec_live_debug.stacked_r1 = sp[1];
        g_rec_live_debug.stacked_r2 = sp[2];
        g_rec_live_debug.stacked_r3 = sp[3];
        g_rec_live_debug.stacked_r12 = sp[4];
        g_rec_live_debug.stacked_lr = sp[5];
        g_rec_live_debug.stacked_pc = sp[6];
        g_rec_live_debug.stacked_xpsr = sp[7];
    }

    g_rec_live_debug.hardfault = entry;
    g_rec_live_debug.hardfault_valid = REC_LIVE_DEBUG_HARDFAULT_VALID;
}

const volatile rec_live_debug_state_t *rec_live_debug_get_state(void)
{
    return &g_rec_live_debug;
}
