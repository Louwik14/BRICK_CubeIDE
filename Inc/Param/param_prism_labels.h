#ifndef PARAM_PRISM_LABELS_H
#define PARAM_PRISM_LABELS_H

#include <stdint.h>

#include "Param/param_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    PARAM_PRISM_LABEL_VALUE_PERCENT = 0,
    PARAM_PRISM_LABEL_VALUE_BIPOLAR_PERCENT,
    PARAM_PRISM_LABEL_VALUE_INTERVAL,
    PARAM_PRISM_LABEL_VALUE_STEPPED,
    PARAM_PRISM_LABEL_VALUE_ENUM,
    PARAM_PRISM_LABEL_VALUE_MORPH,
    PARAM_PRISM_LABEL_VALUE_RATE,
    PARAM_PRISM_LABEL_VALUE_NONE
} param_prism_label_value_kind_t;

typedef struct
{
    const char *label_a;
    const char *label_b;
    param_prism_label_value_kind_t kind_a;
    param_prism_label_value_kind_t kind_b;
} param_prism_param_label_t;

uint8_t param_prism_label_count(void);
uint8_t param_prism_edit_index_from_value(float value, uint8_t *out_index);
uint8_t param_prism_edit_index_for_track(uint8_t track, uint8_t *out_index);
const param_prism_param_label_t *param_prism_labels_for_edit_index(uint8_t edit_index);
uint8_t param_prism_label_for_track_param(uint8_t track, param_id_t id, const char **out_label);
uint8_t param_prism_param_is_active(uint8_t track, param_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* PARAM_PRISM_LABELS_H */
