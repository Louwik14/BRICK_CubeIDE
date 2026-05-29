#ifndef PARAM_WAVE_LABELS_H
#define PARAM_WAVE_LABELS_H

#include <stdint.h>

#include "Param/param_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    PARAM_WAVE_LABEL_VALUE_PERCENT = 0,
    PARAM_WAVE_LABEL_VALUE_BIPOLAR_PERCENT,
    PARAM_WAVE_LABEL_VALUE_INTERVAL,
    PARAM_WAVE_LABEL_VALUE_STEPPED,
    PARAM_WAVE_LABEL_VALUE_ENUM,
    PARAM_WAVE_LABEL_VALUE_MORPH,
    PARAM_WAVE_LABEL_VALUE_RATE,
    PARAM_WAVE_LABEL_VALUE_NONE
} param_wave_label_value_kind_t;

typedef struct
{
    const char *label_a;
    const char *label_b;
    param_wave_label_value_kind_t kind_a;
    param_wave_label_value_kind_t kind_b;
} param_wave_param_label_t;

uint8_t param_wave_label_count(void);
uint8_t param_wave_edit_index_from_value(float value, uint8_t *out_index);
uint8_t param_wave_edit_index_for_track(uint8_t track, uint8_t *out_index);
const param_wave_param_label_t *param_wave_labels_for_edit_index(uint8_t edit_index);
uint8_t param_wave_label_for_track_param(uint8_t track, param_id_t id, const char **out_label);

#ifdef __cplusplus
}
#endif

#endif /* PARAM_WAVE_LABELS_H */
