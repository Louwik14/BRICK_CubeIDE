#ifndef PATTERN_LIVE_RAM_H
#define PATTERN_LIVE_RAM_H

#include <stdint.h>

#include "Seq/seq_model.h"
#include "Param/param_store.h"

typedef struct __attribute__((packed))
{
    uint8_t set_id;
    seq_param_slot_t param_slot;
    seq_value16_t value16;
    uint8_t flags;
} pattern_v1_plock_t;

typedef struct
{
    uint8_t trig;
    uint8_t lock_count;
    uint8_t roll;
    uint8_t reserved;
    pattern_v1_plock_t locks[SEQ_STEP_MAX_LOCKS];
} pattern_v1_step_t;

typedef struct
{
    uint8_t length_steps;
    uint8_t ui_page;
    pattern_v1_step_t steps[SEQ_MAX_STEPS];
} pattern_v1_track_seq_t;

typedef struct
{
    uint8_t family[SEQ_TRACK_COUNT];
    uint8_t type[SEQ_TRACK_COUNT];
    uint8_t midi_channel[SEQ_TRACK_COUNT];
    uint8_t midi_source[SEQ_TRACK_COUNT];
    uint8_t voice_group_role[SEQ_TRACK_COUNT];
    float voice_group_spread[SEQ_TRACK_COUNT];
    uint8_t voice_group_link[SEQ_TRACK_COUNT];
    uint8_t voice_group_seq_link[SEQ_TRACK_COUNT];
    uint8_t looper_route_enabled[SEQ_TRACK_COUNT][SEQ_TRACK_COUNT];
} pattern_v1_track_cfg_block_t;

typedef struct
{
    pattern_v1_track_seq_t tracks[SEQ_TRACK_COUNT];
} pattern_v1_seq_block_t;

typedef struct
{
    float track_values[SEQ_TRACK_COUNT][PARAM_COUNT];
    uint8_t track_valid[SEQ_TRACK_COUNT][PARAM_COUNT];
} pattern_v1_track_param_block_t;

typedef struct
{
    float global_values[PARAM_COUNT];
    uint8_t global_valid[PARAM_COUNT];
    uint32_t tempo_bpm_milli;
    uint8_t clock_src;
    uint8_t rec_start_mode;
    uint8_t rec_len_mode;
    uint8_t linked_kit_valid;
    uint8_t reserved;
    uint16_t linked_kit_slot;
    uint8_t track_div[SEQ_TRACK_COUNT];
    uint8_t track_quant[SEQ_TRACK_COUNT];
    uint8_t track_swing[SEQ_TRACK_COUNT];
} pattern_v1_globals_block_t;

typedef struct
{
    pattern_v1_seq_block_t seq;
    pattern_v1_track_cfg_block_t track_cfg;
    pattern_v1_track_param_block_t sound;
    pattern_v1_track_param_block_t mix;
    pattern_v1_globals_block_t globals;
} PatternSaveV1;

void pattern_live_init(void);
uint8_t pattern_live_capture_boot_snapshot(void);
uint8_t pattern_load_request(uint8_t bank, uint8_t pattern);
void pattern_load_service(uint32_t byte_budget);
uint8_t pattern_load_is_pending(void);
uint8_t pattern_load_is_ready(uint8_t *out_bank, uint8_t *out_pattern);
uint8_t pattern_load_take_ready(uint8_t *out_bank, uint8_t *out_pattern, PatternSaveV1 *out_snapshot);
void pattern_load_cancel(void);
void pattern_live_service(void);
uint8_t pattern_live_capture_to_slot(uint8_t bank, uint8_t pattern);
uint8_t pattern_live_queue_slot(uint8_t bank, uint8_t pattern);
uint8_t pattern_live_get_active(uint8_t *out_bank, uint8_t *out_pattern);
uint8_t pattern_live_get_queued(uint8_t *out_valid, uint8_t *out_bank, uint8_t *out_pattern);
void pattern_live_set_active_state(uint8_t active_bank,
                                   uint8_t active_pattern,
                                   uint8_t queued_valid,
                                   uint8_t queued_bank,
                                   uint8_t queued_pattern);
uint8_t pattern_live_capture_current(PatternSaveV1 *out_pattern);
uint8_t pattern_live_apply_snapshot(const PatternSaveV1 *pattern, uint8_t resume_transport);
uint8_t pattern_live_apply_boot_snapshot(uint8_t resume_transport);
uint8_t pattern_live_is_apply_in_progress(void);
uint8_t pattern_live_get_active_linked_kit(uint16_t *out_slot);
uint8_t pattern_live_link_active_kit(uint16_t slot);
void pattern_live_clear_active_kit_link_if_slot(uint16_t slot);

#endif
