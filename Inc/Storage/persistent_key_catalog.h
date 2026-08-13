#ifndef PERSISTENT_KEY_CATALOG_H
#define PERSISTENT_KEY_CATALOG_H

#include <stdint.h>
#include "Storage/persistent_control_model.h"
#include "Param/param_store.h"
#include "UI/ui_core.h"
#include "Seq/seq_types.h"
#include "Mod/mod_lfo_v1.h"
#include "NoteFx/note_fx_state.h"

typedef enum { PERSIST_PARAM_SCOPE_NONE=0, PERSIST_PARAM_SCOPE_ENTITY, PERSIST_PARAM_SCOPE_GLOBAL } persist_param_scope_t;
typedef struct { persist_control_parameter_key_t key; persist_control_value_kind_t kind; persist_param_scope_t scope; uint8_t persistent; uint8_t plockable; } persist_param_descriptor_t;

uint8_t persist_key_param_descriptor(param_id_t id,persist_param_descriptor_t *out);
uint8_t persist_key_param_to_disk(param_id_t id,persist_control_parameter_key_t *out);
uint8_t persist_key_param_from_disk(persist_control_parameter_key_t key,param_id_t *out);
uint8_t persist_key_family_to_disk(ui_track_family_t value,persist_control_family_key_t *out);
uint8_t persist_key_family_from_disk(persist_control_family_key_t key,ui_track_family_t *out);
uint8_t persist_key_type_to_disk(ui_track_type_t value,persist_control_type_key_t *out);
uint8_t persist_key_type_from_disk(persist_control_type_key_t key,ui_track_type_t *out);
uint8_t persist_key_midi_source_to_disk(ui_track_midi_source_t value,uint32_t *out);
uint8_t persist_key_midi_source_from_disk(uint32_t key,ui_track_midi_source_t *out);
uint8_t persist_key_clock_to_disk(seq_clock_src_t value,uint32_t *out);
uint8_t persist_key_clock_from_disk(uint32_t key,seq_clock_src_t *out);
uint8_t persist_key_note_fx_to_disk(note_fx_model_t value,uint32_t *out);
uint8_t persist_key_note_fx_from_disk(uint32_t key,note_fx_model_t *out);
uint8_t persist_key_lfo_shape_to_disk(mod_lfo_shape_t value,uint32_t *out);
uint8_t persist_key_lfo_shape_from_disk(uint32_t key,mod_lfo_shape_t *out);
uint8_t persist_key_lfo_trigger_to_disk(mod_lfo_trig_mode_t value,uint32_t *out);
uint8_t persist_key_lfo_trigger_from_disk(uint32_t key,mod_lfo_trig_mode_t *out);
uint8_t persist_key_mod_source_to_disk(uint8_t value,uint32_t *out);
uint8_t persist_key_mod_source_from_disk(uint32_t key,uint8_t *out);
uint8_t persist_key_record_start_to_disk(uint8_t value,uint32_t *out);
uint8_t persist_key_record_start_from_disk(uint32_t key,uint8_t *out);
uint8_t persist_key_record_length_to_disk(uint8_t value,uint32_t *out);
uint8_t persist_key_record_length_from_disk(uint32_t key,uint8_t *out);
uint8_t persist_key_input_to_disk(uint8_t value,uint32_t *out);
uint8_t persist_key_input_from_disk(uint32_t key,uint8_t *out);
uint8_t persist_key_mod_destination_to_disk(uint8_t entity,param_id_t param,persist_control_entity_id_t *out_entity,persist_control_parameter_key_t *out_parameter);
uint8_t persist_key_mod_destination_from_disk(persist_control_entity_id_t entity,persist_control_parameter_key_t parameter,uint8_t group_active,uint8_t *out_entity,param_id_t *out_param);

#endif
