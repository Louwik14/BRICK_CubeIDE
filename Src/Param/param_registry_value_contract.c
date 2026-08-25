#include "Param/param_registry.h"

#include <stddef.h>

uint8_t param_registry_prepare_value(param_id_t id,
                                     float value,
                                     param_registry_prepared_value_t *out_value)
{
    if ((out_value == NULL)
            || (id >= PARAM_COUNT)
            || (param_id_is_reserved(id) != 0U))
    {
        return 0U;
    }

    const param_desc_t *const desc = &param_registry[id];
    if (value < desc->min)
        value = desc->min;
    else if (value > desc->max)
        value = desc->max;

    out_value->id = id;
    out_value->value = value;
    return 1U;
}
