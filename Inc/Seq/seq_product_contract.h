#ifndef SEQ_PRODUCT_CONTRACT_H
#define SEQ_PRODUCT_CONTRACT_H

#include <stdint.h>

#include "NoteFx/note_fx_contract.h"
#include "NoteFx/note_fx_state.h"
#include "Seq/seq_model.h"
#include "Track/entity_types.h"

#define SEQ_PRODUCT_SAMPLE_RATE_HZ 48000U
#define SEQ_PRODUCT_TEMPO_MIN_BPM 40U
#define SEQ_PRODUCT_TEMPO_MAX_BPM 300U
#define SEQ_PRODUCT_STEPS_PER_QUARTER 4U

#define SEQ_LOGICAL_CAPACITY_MAX 8U
#define SEQ_LOGICAL_CAPACITY_GROUP_CHILD 1U
#define SEQ_LOGICAL_CAPACITY_GROUP_MASTER 0U

/* Raw canonical ingress contract.  A fixed audio window may accept at most
 * this many Keyboard/DIN/USB events in total.  This is deliberately equal to
 * the existing inbox capacity.  It preserves a complete 64-event burst and
 * bounds the sustained accepted rate to 3000 events/s at 48 kHz. */
#define SEQ_INGRESS_WINDOW_SAMPLES 1024U
#define SEQ_INGRESS_EVENTS_PER_WINDOW_MAX 64U

#define SEQ_GROOVE_NEGATIVE_HORIZON_NUMERATOR 1U
#define SEQ_GROOVE_NEGATIVE_HORIZON_DENOMINATOR 12U

/* Scheduler PASS 2 proof inputs.  No scheduler is instantiated in PASS 1. */
#define SEQ_ROLL_MIN_INTERVAL_STEP_NUMERATOR 1U
#define SEQ_ROLL_MIN_INTERVAL_STEP_DENOMINATOR 5U
#define SEQ_ECHO_MAX_DELAY_STEPS 4U
#define SEQ_ECHO_CHAIN_HORIZON_STEPS \
    (NOTE_FX_ECHO_REPEAT_MAX * SEQ_ECHO_MAX_DELAY_STEPS)
#define SEQ_ECHO_CHAINS_PER_LOGICAL_VOICE_MAX \
    ((SEQ_ECHO_CHAIN_HORIZON_STEPS \
        * SEQ_ROLL_MIN_INTERVAL_STEP_DENOMINATOR) \
        / SEQ_ROLL_MIN_INTERVAL_STEP_NUMERATOR)
#define SEQ_SEQUENCED_ECHO_CONTINUATIONS_MAX \
    (BRICK_ENTITY_TOP_LEVEL_COUNT * SEQ_LOGICAL_CAPACITY_MAX \
        * SEQ_ECHO_CHAINS_PER_LOGICAL_VOICE_MAX)

_Static_assert(NOTE_FX_SLOT_COUNT == 4U, "product requires four MIDI FX slots");
_Static_assert(NOTE_FX_ECHO_REPEAT_MAX == 2U, "Echo repeat proof changed");
_Static_assert(SEQ_PLAY_MAX_CAPACITY == SEQ_LOGICAL_CAPACITY_MAX,
               "PLAY and logical polyphony contracts diverged");
_Static_assert(SEQ_LOGICAL_CAPACITY_GROUP_CHILD == 1U,
               "GROUP child must remain mono in SEQ");
_Static_assert(SEQ_LOGICAL_CAPACITY_GROUP_MASTER == 0U,
               "GROUP master must not emit sequenced notes");
_Static_assert(SEQ_INGRESS_WINDOW_SAMPLES == 1024U,
               "raw ingress window contract changed");
_Static_assert(SEQ_INGRESS_EVENTS_PER_WINDOW_MAX == 64U,
               "raw ingress rate contract changed");
_Static_assert(SEQ_SEQUENCED_ECHO_CONTINUATIONS_MAX == 2560U,
               "sequenced Echo lower-bound proof changed");

#endif
