#ifndef SEQ_PARAM_IFACE_H
#define SEQ_PARAM_IFACE_H

#include <stdint.h>

#include "Seq/seq_types.h"
#include "Param/param_store.h"

typedef enum
{
    SEQ_PLOCK_SET_ENV = 0,
    SEQ_PLOCK_SET_TONE,
    SEQ_PLOCK_SET_PLAY,
    SEQ_PLOCK_SET_MOD,
    SEQ_PLOCK_SET_MIDI_FX,
    SEQ_PLOCK_SET_MIX,
    SEQ_PLOCK_SET_COUNT
} seq_plock_set_id_t;

_Static_assert(SEQ_PLOCK_SET_ENV == 0, "ENV p-lock set ordinal changed");
_Static_assert(SEQ_PLOCK_SET_TONE == 1, "TONE p-lock set ordinal changed");
_Static_assert(SEQ_PLOCK_SET_PLAY == 2, "PLAY p-lock set ordinal changed");
_Static_assert(SEQ_PLOCK_SET_MOD == 3, "MOD p-lock set ordinal changed");
_Static_assert(SEQ_PLOCK_SET_MIDI_FX == 4, "MIDI FX p-lock set ordinal changed");
_Static_assert(SEQ_PLOCK_SET_MIX == 5, "MIX p-lock set ordinal changed");
_Static_assert(SEQ_PLOCK_SET_COUNT == 6, "p-lock set count changed");
_Static_assert(SEQ_PARAM_ENV_SLOT_OFFSET == 0U, "ENV p-lock offset changed");
_Static_assert(SEQ_PARAM_TONE_SLOT_OFFSET == 25U, "TONE p-lock offset changed");
_Static_assert(SEQ_PARAM_PLAY_SLOT_OFFSET == 46U, "PLAY p-lock offset changed");
_Static_assert(SEQ_PARAM_MOD_SLOT_OFFSET == 62U, "MOD p-lock offset changed");
_Static_assert(SEQ_PARAM_MIDI_FX_SLOT_OFFSET == 74U, "MIDI FX p-lock offset changed");
_Static_assert(SEQ_PARAM_MIX_SLOT_OFFSET == 90U, "MIX p-lock offset changed");
_Static_assert(SEQ_PARAM_RUNTIME_SLOT_COUNT == 94U, "runtime p-lock slot count changed");
_Static_assert(SEQ_PARAM_ENV_SLOT_COUNT <= 255U, "ENV p-lock capacity exceeds slot type");
_Static_assert(SEQ_PARAM_TONE_SLOT_COUNT <= 255U, "TONE p-lock capacity exceeds slot type");
_Static_assert(SEQ_PARAM_PLAY_SLOT_COUNT <= 255U, "PLAY p-lock capacity exceeds slot type");
_Static_assert(SEQ_PARAM_MOD_SLOT_COUNT <= 255U, "MOD p-lock capacity exceeds slot type");
_Static_assert(SEQ_PARAM_MIDI_FX_SLOT_COUNT <= 255U, "MIDI FX p-lock capacity exceeds slot type");
_Static_assert(SEQ_PARAM_MIX_SLOT_COUNT <= 255U, "MIX p-lock capacity exceeds slot type");
_Static_assert(SEQ_PARAM_RUNTIME_FLAG_BYTE_COUNT == 94U, "runtime p-lock bitmap size changed");
_Static_assert((PARAM_MIDI_FX_S4_MODEL - PARAM_MIDI_FX_S1_PARAM1 + 1U) == SEQ_PARAM_MIDI_FX_SLOT_COUNT,
               "MIDI FX inverse table capacity changed");

typedef enum
{
    SEQ_PARAM_IFACE_COMMIT_SOURCE_UI_TRACK_EDIT = 0
} seq_param_iface_commit_source_t;

typedef struct
{
    seq_param_iface_commit_source_t source;
    uint8_t authoritative_apply_done;
    seq_track_id_t target_track;
    uint8_t set_id;
    seq_param_slot_t param_slot;
    seq_value16_t value16;
} seq_param_iface_base_commit_cmd_t;

void seq_param_iface_init(void);

/* Track-independent compact-slot allowlist. TONE remains engine-specific at mapping time. */
uint8_t seq_param_iface_is_param_plockable(param_id_t param_id);

uint8_t seq_param_iface_is_set_plockable(uint8_t set_id);
uint8_t seq_param_iface_set_to_mask(uint8_t set_id);

uint8_t seq_param_iface_slot_to_param(seq_track_id_t track,
                                      uint8_t set_id,
                                      seq_param_slot_t param_slot,
                                      param_id_t *out_param_id);
uint8_t seq_param_iface_param_to_slot(seq_track_id_t track,
                                      uint8_t set_id,
                                      param_id_t param_id,
                                      seq_param_slot_t *out_param_slot);
uint8_t seq_param_iface_slot_is_supported(seq_track_id_t track, uint8_t set_id, seq_param_slot_t param_slot);
uint8_t seq_param_iface_param_is_supported(seq_track_id_t track,
                                           uint8_t set_id,
                                           param_id_t param_id);
/* Legacy name kept for compatibility: uses slot semantics (set_id + param_slot slot). */
uint8_t seq_param_iface_is_param_supported(seq_track_id_t track, uint8_t set_id, seq_param_slot_t param_slot);

uint8_t seq_param_iface_get_base_value(seq_track_id_t track,
                                       uint8_t set_id,
                                       seq_param_slot_t param_slot,
                                       seq_value16_t *out_value16);
uint8_t seq_param_iface_set_base_value(seq_track_id_t track,
                                       uint8_t set_id,
                                       seq_param_slot_t param_slot,
                                       seq_value16_t value16);
uint8_t seq_param_iface_get_play_base_value(seq_track_id_t track,
                                            seq_param_slot_t param_slot,
                                            seq_value16_t *out_value16);
uint8_t seq_param_iface_set_play_base_value(seq_track_id_t track,
                                            seq_param_slot_t param_slot,
                                            seq_value16_t value16);
uint8_t seq_param_iface_commit_base_after_authoritative_apply(const seq_param_iface_base_commit_cmd_t *cmd);
uint8_t seq_param_iface_apply_lock(seq_track_id_t track,
                                   uint8_t set_id,
                                   seq_param_slot_t param_slot,
                                   seq_value16_t value16);
uint8_t seq_param_iface_restore_base(seq_track_id_t track,
                                     uint8_t set_id,
                                     seq_param_slot_t param_slot,
                                     seq_value16_t base_value16);

seq_value16_t seq_param_iface_encode_param_value(param_id_t param, float value);
float seq_param_iface_decode_param_value(param_id_t param, seq_value16_t value16);

#endif /* SEQ_PARAM_IFACE_H */

