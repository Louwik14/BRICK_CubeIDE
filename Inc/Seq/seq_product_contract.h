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
/* Worst-case track Groove displacement at BASE 1/4 and GLOBAL 130%.
 * Timing contributes 13/20 of a cell. Random contributes at most
 * (8/21) * (13/10)^2. A Q32 period non-integral in BASE can make the
 * prepared uniform cell approach 3/2 BASE. */
#define SEQ_PRODUCT_QUARTER_MAX_SAMPLES \
    ((SEQ_PRODUCT_SAMPLE_RATE_HZ * 60U) / SEQ_PRODUCT_TEMPO_MIN_BPM)
#define SEQ_PRODUCT_TIMING_MAX_DELAY_SAMPLES \
    ((SEQ_PRODUCT_QUARTER_MAX_SAMPLES * 2717U + 1399U) / 1400U)

#define SEQ_LOGICAL_CAPACITY_MAX 8U
#define SEQ_LOGICAL_CAPACITY_GROUP_CHILD 1U
#define SEQ_LOGICAL_CAPACITY_GROUP_MASTER 0U

/* Raw canonical ingress contract.  A fixed audio window may accept at most
 * this many Keyboard/DIN/USB events in total.  This is deliberately equal to
 * the existing inbox capacity.  It preserves a complete 64-event burst and
 * bounds the sustained accepted rate to 3000 events/s at 48 kHz. */
#define SEQ_INGRESS_WINDOW_SAMPLES 1024U
#define SEQ_INGRESS_EVENTS_PER_WINDOW_MAX 64U

/* Fixed owner proof inputs. */
#define SEQ_ROLL_MIN_INTERVAL_STEP_NUMERATOR 1U
#define SEQ_ROLL_MIN_INTERVAL_STEP_DENOMINATOR 4U
_Static_assert(NOTE_FX_CHAIN_STAGE_COUNT == 4U,
               "product requires four fixed MIDI FX stages");
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

#endif
