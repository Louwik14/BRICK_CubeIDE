#ifndef SEQ_PARAM_IFACE_H
#define SEQ_PARAM_IFACE_H

#include <stdint.h>

#include "Seq/seq_types.h"
#include "Param/param_store.h"

typedef enum
{
    SEQ_PLOCK_SET_COLORS = 0,
    SEQ_PLOCK_SET_TONE,
    SEQ_PLOCK_SET_PLAY,
    SEQ_PLOCK_SET_MOD,
    SEQ_PLOCK_SET_COUNT
} seq_plock_set_id_t;

void seq_param_iface_init(void);

uint8_t seq_param_iface_is_set_plockable(uint8_t set_id);
uint8_t seq_param_iface_set_to_mask(uint8_t set_id);
uint8_t seq_param_iface_is_param_supported(seq_track_id_t track, uint8_t set_id, seq_param8_t param8);

uint8_t seq_param_iface_get_base_value(seq_track_id_t track,
                                       uint8_t set_id,
                                       seq_param8_t param8,
                                       seq_value16_t *out_value16);
uint8_t seq_param_iface_set_base_value(seq_track_id_t track,
                                       uint8_t set_id,
                                       seq_param8_t param8,
                                       seq_value16_t value16);
uint8_t seq_param_iface_apply_lock(seq_track_id_t track,
                                   uint8_t set_id,
                                   seq_param8_t param8,
                                   seq_value16_t value16);
uint8_t seq_param_iface_restore_base(seq_track_id_t track,
                                     uint8_t set_id,
                                     seq_param8_t param8,
                                     seq_value16_t base_value16);

uint8_t seq_param_iface_map_param(param_id_t param,
                                  uint8_t *out_set_id,
                                  seq_param8_t *out_param8);
seq_value16_t seq_param_iface_encode_param_value(param_id_t param, float value);
float seq_param_iface_decode_param_value(param_id_t param, seq_value16_t value16);

#endif /* SEQ_PARAM_IFACE_H */
