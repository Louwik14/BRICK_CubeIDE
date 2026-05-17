#ifndef REC_LIVE_DEBUG_H
#define REC_LIVE_DEBUG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REC_LIVE_DEBUG_RING_CAPACITY 64U
#define REC_LIVE_DEBUG_MAGIC 0x524C4442UL
#define REC_LIVE_DEBUG_HARDFAULT_VALID 0x48464155UL

typedef enum
{
    REC_LIVE_DEBUG_REC_LIVE_STOP_REQUESTED = 1U,
    REC_LIVE_DEBUG_WRITER_DRAINING,
    REC_LIVE_DEBUG_WRITER_FINALIZING,
    REC_LIVE_DEBUG_WRITER_FINAL_READY,
    REC_LIVE_DEBUG_TAKE_READY_ENTER,
    REC_LIVE_DEBUG_WAVECACHE_REQUEST_ENTER,
    REC_LIVE_DEBUG_WAVECACHE_REQUEST_EXIT,
    REC_LIVE_DEBUG_REC_EDIT_ENTER_REQUEST,
    REC_LIVE_DEBUG_REC_EDIT_MODEL_INIT,
    REC_LIVE_DEBUG_REC_EDIT_FIRST_RENDER,
    REC_LIVE_DEBUG_REC_EDIT_ENTER_OK,
    REC_LIVE_DEBUG_WAVECACHE_SERVICE_ENTER,
    REC_LIVE_DEBUG_WAVECACHE_SERVICE_EXIT
} rec_live_debug_code_t;

typedef struct
{
    uint32_t code;
    uint32_t tick;
    uint32_t recorded_frames;
    uint32_t wav_path_hash;
    uint32_t writer_state;
    uint32_t sample_state;
    uint32_t last_error;
    uint32_t pc;
    uint32_t lr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t bfar;
    uint32_t mmfar;
} rec_live_debug_entry_t;

typedef struct
{
    volatile uint32_t magic;
    volatile uint32_t capacity;
    volatile uint32_t write_index;
    volatile uint32_t hardfault_valid;
    volatile rec_live_debug_entry_t entries[REC_LIVE_DEBUG_RING_CAPACITY];
    volatile rec_live_debug_entry_t hardfault;
    volatile uint32_t stacked_r0;
    volatile uint32_t stacked_r1;
    volatile uint32_t stacked_r2;
    volatile uint32_t stacked_r3;
    volatile uint32_t stacked_r12;
    volatile uint32_t stacked_lr;
    volatile uint32_t stacked_pc;
    volatile uint32_t stacked_xpsr;
} rec_live_debug_state_t;

uint32_t rec_live_debug_path_hash(const char *path);
void rec_live_debug_mark(uint32_t code,
                         uint32_t recorded_frames,
                         uint32_t wav_path_hash,
                         uint32_t writer_state,
                         uint32_t sample_state,
                         uint32_t last_error);
void rec_live_debug_hardfault(uint32_t *sp);
const volatile rec_live_debug_state_t *rec_live_debug_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* REC_LIVE_DEBUG_H */
