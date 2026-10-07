#ifndef PERSISTENT_PATTERN_CONTROL_H
#define PERSISTENT_PATTERN_CONTROL_H
#include <stdint.h>
#include "Storage/persistent_control_codec.h"
#include "Mod/mod_destination_contract.h"
#include "NoteFx/note_fx_state.h"
#include "Seq/seq_model.h"
#include "Seq/seq_engine.h"

typedef struct
{
    const persist_control_pattern_t *pattern;
    uint8_t family[BRICK_ENTITY_CAPACITY];
    uint8_t type[BRICK_ENTITY_CAPACITY];
    uint8_t midi_source[BRICK_ENTITY_CAPACITY];
    uint8_t input[TRACK_COUNT];
    uint8_t clock_source;
    uint8_t record_start;
    uint8_t record_length;
    uint8_t lfo_shape[BRICK_ENTITY_CAPACITY][PERSIST_CONTROL_MOD_LFO_COUNT];
    uint8_t lfo_trigger[BRICK_ENTITY_CAPACITY][PERSIST_CONTROL_MOD_LFO_COUNT];
    uint8_t mod_source[BRICK_ENTITY_CAPACITY][6U];
    uint8_t route_source[BRICK_ENTITY_CAPACITY][PERSIST_CONTROL_MOD_ROUTE_COUNT];
    mod_destination_address_t
        route_destination[BRICK_ENTITY_CAPACITY][PERSIST_CONTROL_MOD_ROUTE_COUNT];
    seq_plock_key_t locks[SEQ_LANE_CAPACITY][SEQ_ENGINE_LOCK_POOL_CAPACITY];
    uint16_t lock_count[SEQ_LANE_CAPACITY];
    uint8_t group_active;
    uint8_t control_prepared;
    uint8_t seq_prepared;
    uint8_t audio_prepared;
    uint8_t audio_slot;
    uint32_t audio_generation;
} persistent_pattern_prepared_t;

_Static_assert(sizeof(persistent_pattern_prepared_t) == 8876U,
               "Prepared Pattern metadata size changed");

typedef struct
{
    param_global_control_state_t global_audio;
    uint32_t groove_seed;
} persistent_pattern_default_context_t;

persist_codec_result_t persistent_pattern_control_build_defaults(
    persist_control_pattern_t *destination,
    const persistent_pattern_default_context_t *context);
persist_codec_result_t persistent_pattern_control_capture(persist_control_pattern_t *out_pattern);
persist_codec_result_t persistent_pattern_control_validate(const persist_control_pattern_t *pattern);
/* Runtime/topology validation for a Pattern already accepted by the codec. */
persist_codec_result_t persistent_pattern_control_validate_decoded(
    const persist_control_pattern_t *pattern);
/* The Pattern must already have passed persist_codec_validate_pattern(). */
persist_codec_result_t persistent_pattern_control_prepare(
    const persist_control_pattern_t *pattern,
    persistent_pattern_prepared_t *prepared,
    seq_groove_compiled_t workspace[SEQ_TIMING_TRACK_COUNT]);
void persistent_pattern_control_abort_prepared(
    persistent_pattern_prepared_t *prepared);
/* CONTROL and SEQ are already proven/compiled.  Any refusal here is a
 * firmware invariant failure, never a Pattern result. */
void persistent_pattern_control_commit_prepared_control(
    persistent_pattern_prepared_t *prepared);
void persistent_pattern_control_commit_prepared_seq(
    persistent_pattern_prepared_t *prepared, uint8_t resume_transport);
void persistent_pattern_control_apply_prepared(
    persistent_pattern_prepared_t *prepared, uint8_t resume_transport);
void persistent_pattern_control_sync_ui_after_commit(void);
#endif
