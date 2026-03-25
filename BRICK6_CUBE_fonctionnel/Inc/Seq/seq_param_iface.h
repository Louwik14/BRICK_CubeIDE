#ifndef SEQ_PARAM_IFACE_H
#define SEQ_PARAM_IFACE_H

#include <stdint.h>

typedef enum
{
    SEQ_PLOCK_SET_COLORS = 0,
    SEQ_PLOCK_SET_TONE,
    SEQ_PLOCK_SET_COUNT
} seq_plock_set_id_t;

uint8_t seq_param_iface_is_set_plockable(uint8_t set_id);
uint8_t seq_param_iface_set_to_mask(uint8_t set_id);

#endif /* SEQ_PARAM_IFACE_H */
