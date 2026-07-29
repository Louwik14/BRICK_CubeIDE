#ifndef TRACK_SNAPSHOT_H
#define TRACK_SNAPSHOT_H

#include <stddef.h>
#include <stdint.h>

#include "Core/track_sound_state.h"
#include "Core/track_tone_sound_state.h"
#include "Seq/seq_model.h"
#include "UI/ui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t count;
    uint8_t reserved[3];
    seq_plock_entry_t locks[SEQ_STEP_MAX_LOCKS];
} track_snapshot_step_locks_t;

typedef struct
{
    uint8_t valid;
    ui_track_config_t config;
    uint8_t midi_channel;
    ui_track_midi_source_t midi_source;
    uint8_t voice_group_role;
    float voice_group_spread;
    uint8_t voice_group_spread_keytrack;
    uint8_t voice_group_link;
    uint8_t voice_group_seq_link;
    track_sound_state_t sound;
    track_tone_sound_state_t tone;
    seq_track_data_t seq_track;
    track_snapshot_step_locks_t step_locks[SEQ_MAX_STEPS];
    uint8_t seq_div;
    uint8_t seq_quant;
    uint8_t seq_swing;
} track_snapshot_t;

typedef struct
{
    uint8_t has_family_override;
    ui_track_family_t family_override;
    uint8_t clear_source_track;
    uint8_t source_track;
} track_snapshot_apply_options_t;

_Static_assert(sizeof(seq_plock_entry_t) == 8U, "seq_plock_entry_t size changed");
_Static_assert((offsetof(track_snapshot_step_locks_t, locks) % 4U) == 0U,
               "track snapshot p-lock entries offset must be 4-byte aligned");
_Static_assert((sizeof(track_snapshot_step_locks_t) % 4U) == 0U,
               "track snapshot p-lock step stride must preserve 4-byte alignment");
_Static_assert((offsetof(track_snapshot_t, step_locks) % 4U) == 0U,
               "track snapshot step_locks block must be 4-byte aligned");
_Static_assert(((offsetof(track_snapshot_t, step_locks)
                 + offsetof(track_snapshot_step_locks_t, locks)) % 4U) == 0U,
               "track snapshot first p-lock entries table must be 4-byte aligned");
_Static_assert((_Alignof(track_snapshot_t) >= 4U) && ((sizeof(track_snapshot_t) % _Alignof(track_snapshot_t)) == 0U),
               "track snapshot owner buffers require natural snapshot alignment and stride");

uint8_t track_snapshot_capture(uint8_t track, track_snapshot_t *out_snapshot);
uint8_t track_snapshot_make_default(uint8_t track, track_snapshot_t *out_snapshot);
uint8_t track_snapshot_apply(uint8_t target_track, const track_snapshot_t *snapshot);
uint8_t track_snapshot_apply_ex(uint8_t target_track,
                                const track_snapshot_t *snapshot,
                                const track_snapshot_apply_options_t *options);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_SNAPSHOT_H */
