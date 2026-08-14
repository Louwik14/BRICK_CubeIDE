#ifndef SEQ_TYPES_H
#define SEQ_TYPES_H

#include <stdint.h>
#include "Core/entity_topology.h"
#include "Param/param_store.h"

#define SEQ_TRACK_COUNT        BRICK_ENTITY_TOP_LEVEL_COUNT
#define SEQ_LANE_CAPACITY      BRICK_ENTITY_CAPACITY
#define SEQ_STEPS_PER_PAGE    16U
#define SEQ_PAGE_COUNT         4U
#define SEQ_MAX_STEPS         (SEQ_STEPS_PER_PAGE * SEQ_PAGE_COUNT)
#define SEQ_DEFAULT_LENGTH_STEPS SEQ_STEPS_PER_PAGE

#define SEQ_STEP_MAX_LOCKS 32U
#define SEQ_PLOCK_POOL_CAP_PER_TRACK 1024U

/* Global per-track memory budget: shared by all steps of the track. */
#define SEQ_PLOCK_BUDGET_PER_TRACK SEQ_PLOCK_POOL_CAP_PER_TRACK

/* Current firmware p-lock contract.  These slots are the compact identity
 * used by the runtime and the current Pattern format; no legacy slot range
 * is part of the contract. */
#define SEQ_PARAM_ENV_SLOT_COUNT       25U
#define SEQ_PARAM_TONE_SLOT_COUNT      21U
#define SEQ_PARAM_MOD_SLOT_COUNT       12U
#define SEQ_PARAM_MIDI_FX_SLOT_COUNT   12U
#define SEQ_PARAM_MIX_SLOT_COUNT        4U
#define SEQ_PARAM_FM_OPERATOR_SLOT_COUNT (PARAM_FM_OPERATOR_COUNT * PARAM_FM_OPERATOR_PARAM_COUNT)

#define SEQ_PARAM_ENV_SLOT_OFFSET       0U
#define SEQ_PARAM_TONE_SLOT_OFFSET     (SEQ_PARAM_ENV_SLOT_OFFSET + SEQ_PARAM_ENV_SLOT_COUNT)
#define SEQ_PARAM_MOD_SLOT_OFFSET      (SEQ_PARAM_TONE_SLOT_OFFSET + SEQ_PARAM_TONE_SLOT_COUNT)
#define SEQ_PARAM_MIDI_FX_SLOT_OFFSET  (SEQ_PARAM_MOD_SLOT_OFFSET + SEQ_PARAM_MOD_SLOT_COUNT)
#define SEQ_PARAM_MIX_SLOT_OFFSET      (SEQ_PARAM_MIDI_FX_SLOT_OFFSET + SEQ_PARAM_MIDI_FX_SLOT_COUNT)
#define SEQ_PARAM_FM_OPERATOR_SLOT_OFFSET (SEQ_PARAM_MIX_SLOT_OFFSET + SEQ_PARAM_MIX_SLOT_COUNT)
#define SEQ_PARAM_RUNTIME_SLOT_COUNT   (SEQ_PARAM_FM_OPERATOR_SLOT_OFFSET + SEQ_PARAM_FM_OPERATOR_SLOT_COUNT)

#define SEQ_PARAM_RUNTIME_FLAG_BIT_COUNT \
    (SEQ_LANE_CAPACITY * SEQ_PARAM_RUNTIME_SLOT_COUNT)
#define SEQ_PARAM_RUNTIME_FLAG_BYTE_COUNT \
    ((SEQ_PARAM_RUNTIME_FLAG_BIT_COUNT + 7U) / 8U)

/* Sequencer names preserve domain vocabulary without creating another
 * logical identity or requiring conversion. */
typedef brick_entity_id_t seq_track_id_t;
typedef uint8_t seq_step_id_t;
typedef uint8_t seq_param_slot_t;
typedef uint8_t seq_plock_key_t;
typedef uint16_t seq_value16_t;

#ifdef __cplusplus
static_assert(SEQ_LANE_CAPACITY <= UINT8_MAX, "sequence lane capacity exceeds lane id range");
static_assert(BRICK_ENTITY_CAPACITY == SEQ_LANE_CAPACITY,
              "sequencer capacity must match the canonical entity domain");
#else
_Static_assert(SEQ_LANE_CAPACITY <= UINT8_MAX, "sequence lane capacity exceeds lane id range");
_Static_assert(BRICK_ENTITY_CAPACITY == SEQ_LANE_CAPACITY,
               "sequencer capacity must match the canonical entity domain");
#endif

typedef enum
{
    SEQ_CLOCK_SRC_INTERNAL = 0,
    SEQ_CLOCK_SRC_EXTERNAL_MIDI,
    SEQ_CLOCK_SRC_EXTERNAL_USB,
    SEQ_CLOCK_SRC_COUNT
} seq_clock_src_t;

#endif /* SEQ_TYPES_H */
