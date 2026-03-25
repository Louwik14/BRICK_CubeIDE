#include "Seq/seq_param_iface.h"

uint8_t seq_param_iface_is_set_plockable(uint8_t set_id)
{
    return (set_id < (uint8_t)SEQ_PLOCK_SET_COUNT) ? 1U : 0U;
}

uint8_t seq_param_iface_set_to_mask(uint8_t set_id)
{
    if ((set_id >= 8U) || (seq_param_iface_is_set_plockable(set_id) == 0U))
    {
        return 0U;
    }

    return (uint8_t)(1U << set_id);
}
