#include "App/control_domain.h"

#include "App/brick6_boot_defaults.h"
#include "App/brick6_boot_fx_policy.h"
#include "App/engine_tasklet.h"
#include "App/Hall/hall_calibration.h"
#include "App/Hall/hall_keyboard_bridge.h"
#include "App/Hall/hall_loop.h"
#include "Board/board_usb.h"
#include "midi.h"
#include "Param/param_registry.h"
#include "Param/live_parameter_migration.h"
#include "Param/param_macro.h"
#include "Sampler/multi_sample_loader.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_edit.h"
#include "Seq/metronome_control.h"
#include "Storage/audio_recorder.h"
#include "Storage/brick6_stream_service_task.h"
#include "Storage/patch_product.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/project_control.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/project_product.h"
#include "Storage/sd_access_gate.h"
#include "Storage/sd_preview.h"
#include "Storage/undo_v2.h"
#include "Storage/wav_convert.h"
#include "Storage/waveform_cache.h"
#include "Track/track_state.h"
#include "Track/track_mute.h"
#include "Track/control_routing.h"
#include "Track/entity_topology.h"
#include "Track/track_runtime.h"
#include "Mod/mod_lfo_v1_control.h"
#include "Mod/mod_matrix_control.h"
#include "UI/ui_active_track_sync.h"
#include "ControlRT/control_rt_publication.h"
#include "ui_boot_loading.h"
#include "ui_core.h"
#include "ui_page_manager.h"

#define CONTROL_PROJECT_INTENT_CAPACITY 4U
#define CONTROL_PATCH_INTENT_CAPACITY 4U
#define CONTROL_TRACK_INTENT_CAPACITY 8U
#define CONTROL_ROUTING_INTENT_CAPACITY 8U
#define CONTROL_PARAM_INTENT_CAPACITY 8U
#define CONTROL_SEQ_INTENT_CAPACITY 32U
#define CONTROL_MOD_INTENT_CAPACITY 16U
#define CONTROL_MACRO_INTENT_CAPACITY 8U
static control_asset_intent_t g_asset_intent;
static volatile uint8_t g_asset_intent_valid;
static control_clipboard_intent_t g_clipboard_intent;
static volatile uint8_t g_clipboard_intent_valid;

static control_project_intent_t g_project_intents[CONTROL_PROJECT_INTENT_CAPACITY];
static volatile uint8_t g_project_intent_head;
static volatile uint8_t g_project_intent_tail;
static control_patch_intent_t g_patch_intents[CONTROL_PATCH_INTENT_CAPACITY];
static volatile uint8_t g_patch_intent_head;
static volatile uint8_t g_patch_intent_tail;
static control_track_intent_t g_track_intents[CONTROL_TRACK_INTENT_CAPACITY];
static volatile uint8_t g_track_intent_head;
static volatile uint8_t g_track_intent_tail;
static control_routing_intent_t g_routing_intents[CONTROL_ROUTING_INTENT_CAPACITY];
static volatile uint8_t g_routing_intent_head;
static volatile uint8_t g_routing_intent_tail;
static control_param_intent_t g_param_intents[CONTROL_PARAM_INTENT_CAPACITY];
static volatile uint8_t g_param_intent_head;
static volatile uint8_t g_param_intent_tail;
static control_seq_intent_t g_seq_intents[CONTROL_SEQ_INTENT_CAPACITY];
static volatile uint8_t g_seq_intent_head;
static volatile uint8_t g_seq_intent_tail;
static control_mod_intent_t g_mod_intents[CONTROL_MOD_INTENT_CAPACITY];
static volatile uint8_t g_mod_intent_head;
static volatile uint8_t g_mod_intent_tail;
static control_macro_intent_t g_macro_intents[CONTROL_MACRO_INTENT_CAPACITY];
static volatile uint8_t g_macro_intent_head;
static volatile uint8_t g_macro_intent_tail;

uint8_t control_domain_request_project(const control_project_intent_t *intent)
{
    if (intent == NULL) return 0U;
    const uint8_t head = g_project_intent_head;
    const uint8_t next = (uint8_t)((head + 1U) % CONTROL_PROJECT_INTENT_CAPACITY);
    if (next == g_project_intent_tail) return 0U;
    g_project_intents[head] = *intent;
    __DMB();
    g_project_intent_head = next;
    return 1U;
}

uint8_t control_domain_take_project(control_project_intent_t *intent)
{
    if (intent == NULL || g_project_intent_tail == g_project_intent_head) return 0U;
    const uint8_t tail = g_project_intent_tail;
    *intent = g_project_intents[tail];
    __DMB();
    g_project_intent_tail = (uint8_t)((tail + 1U) % CONTROL_PROJECT_INTENT_CAPACITY);
    return 1U;
}

uint8_t control_domain_request_patch(const control_patch_intent_t *intent)
{
    if (intent == NULL) return 0U;
    const uint8_t head = g_patch_intent_head;
    const uint8_t next = (uint8_t)((head + 1U) % CONTROL_PATCH_INTENT_CAPACITY);
    if (next == g_patch_intent_tail) return 0U;
    g_patch_intents[head] = *intent;
    __DMB();
    g_patch_intent_head = next;
    return 1U;
}

uint8_t control_domain_take_patch(control_patch_intent_t *intent)
{
    if (intent == NULL || g_patch_intent_tail == g_patch_intent_head) return 0U;
    const uint8_t tail = g_patch_intent_tail;
    *intent = g_patch_intents[tail];
    __DMB();
    g_patch_intent_tail = (uint8_t)((tail + 1U) % CONTROL_PATCH_INTENT_CAPACITY);
    return 1U;
}

uint8_t control_domain_request_track(const control_track_intent_t *intent)
{
    if (intent == NULL) return 0U;
    const uint8_t head = g_track_intent_head;
    const uint8_t next = (uint8_t)((head + 1U) % CONTROL_TRACK_INTENT_CAPACITY);
    if (next == g_track_intent_tail) return 0U;
    g_track_intents[head] = *intent;
    __DMB();
    g_track_intent_head = next;
    return 1U;
}

uint8_t control_domain_take_track(control_track_intent_t *intent)
{
    if (intent == NULL || g_track_intent_tail == g_track_intent_head) return 0U;
    const uint8_t tail = g_track_intent_tail;
    *intent = g_track_intents[tail];
    __DMB();
    g_track_intent_tail = (uint8_t)((tail + 1U) % CONTROL_TRACK_INTENT_CAPACITY);
    return 1U;
}

uint8_t control_domain_request_routing(const control_routing_intent_t *intent)
{
    if (intent == NULL) return 0U;
    const uint8_t head = g_routing_intent_head;
    const uint8_t next = (uint8_t)((head + 1U) % CONTROL_ROUTING_INTENT_CAPACITY);
    if (next == g_routing_intent_tail) return 0U;
    g_routing_intents[head] = *intent;
    __DMB();
    g_routing_intent_head = next;
    return 1U;
}

uint8_t control_domain_take_routing(control_routing_intent_t *intent)
{
    if (intent == NULL || g_routing_intent_tail == g_routing_intent_head) return 0U;
    const uint8_t tail = g_routing_intent_tail;
    *intent = g_routing_intents[tail];
    __DMB();
    g_routing_intent_tail = (uint8_t)((tail + 1U) % CONTROL_ROUTING_INTENT_CAPACITY);
    return 1U;
}

uint8_t control_domain_request_param(const control_param_intent_t *intent)
{
    if (intent == NULL) return 0U;
    const uint8_t head = g_param_intent_head;
    const uint8_t next = (uint8_t)((head + 1U) % CONTROL_PARAM_INTENT_CAPACITY);
    if (next == g_param_intent_tail) return 0U;
    g_param_intents[head] = *intent;
    __DMB();
    g_param_intent_head = next;
    return 1U;
}

uint8_t control_domain_take_param(control_param_intent_t *intent)
{
    if (intent == NULL || g_param_intent_tail == g_param_intent_head) return 0U;
    const uint8_t tail = g_param_intent_tail;
    *intent = g_param_intents[tail];
    __DMB();
    g_param_intent_tail = (uint8_t)((tail + 1U) % CONTROL_PARAM_INTENT_CAPACITY);
    return 1U;
}

uint8_t control_domain_request_seq(const control_seq_intent_t *intent)
{
    if (intent == NULL) return 0U;
    const uint8_t head = g_seq_intent_head;
    const uint8_t next = (uint8_t)((head + 1U) % CONTROL_SEQ_INTENT_CAPACITY);
    if (next == g_seq_intent_tail) return 0U;
    g_seq_intents[head] = *intent;
    __DMB();
    g_seq_intent_head = next;
    return 1U;
}

uint8_t control_domain_take_seq(control_seq_intent_t *intent)
{
    if (intent == NULL || g_seq_intent_tail == g_seq_intent_head) return 0U;
    const uint8_t tail = g_seq_intent_tail;
    *intent = g_seq_intents[tail];
    __DMB();
    g_seq_intent_tail = (uint8_t)((tail + 1U) % CONTROL_SEQ_INTENT_CAPACITY);
    return 1U;
}

uint8_t control_domain_request_mod(const control_mod_intent_t *intent)
{
    if (intent == NULL) return 0U;
    const uint8_t head = g_mod_intent_head;
    const uint8_t next = (uint8_t)((head + 1U) % CONTROL_MOD_INTENT_CAPACITY);
    if (next == g_mod_intent_tail) return 0U;
    g_mod_intents[head] = *intent;
    __DMB();
    g_mod_intent_head = next;
    return 1U;
}

uint8_t control_domain_take_mod(control_mod_intent_t *intent)
{
    if (intent == NULL || g_mod_intent_tail == g_mod_intent_head) return 0U;
    const uint8_t tail = g_mod_intent_tail;
    *intent = g_mod_intents[tail];
    __DMB();
    g_mod_intent_tail = (uint8_t)((tail + 1U) % CONTROL_MOD_INTENT_CAPACITY);
    return 1U;
}

uint8_t control_domain_request_macro(const control_macro_intent_t *intent)
{
    if (intent == NULL) return 0U;
    const uint8_t head = g_macro_intent_head;
    const uint8_t next = (uint8_t)((head + 1U) % CONTROL_MACRO_INTENT_CAPACITY);
    if (next == g_macro_intent_tail) return 0U;
    g_macro_intents[head] = *intent;
    __DMB();
    g_macro_intent_head = next;
    return 1U;
}

uint8_t control_domain_take_macro(control_macro_intent_t *intent)
{
    if (intent == NULL || g_macro_intent_tail == g_macro_intent_head) return 0U;
    const uint8_t tail = g_macro_intent_tail;
    *intent = g_macro_intents[tail];
    __DMB();
    g_macro_intent_tail = (uint8_t)((tail + 1U) % CONTROL_MACRO_INTENT_CAPACITY);
    return 1U;
}

uint8_t control_domain_request_asset(const control_asset_intent_t *intent)
{
    if ((intent == NULL) || (g_asset_intent_valid != 0U)) return 0U;
    g_asset_intent = *intent;
    __DMB();
    g_asset_intent_valid = 1U;
    return 1U;
}

uint8_t control_domain_take_asset(control_asset_intent_t *intent)
{
    if ((intent == NULL) || (g_asset_intent_valid == 0U)) return 0U;
    *intent = g_asset_intent;
    __DMB();
    g_asset_intent_valid = 0U;
    return 1U;
}

uint8_t control_domain_request_clipboard(const control_clipboard_intent_t *intent)
{
    if ((intent == NULL) || (g_clipboard_intent_valid != 0U)) return 0U;
    g_clipboard_intent = *intent;
    __DMB();
    g_clipboard_intent_valid = 1U;
    return 1U;
}

uint8_t control_domain_take_clipboard(control_clipboard_intent_t *intent)
{
    if ((intent == NULL) || (g_clipboard_intent_valid == 0U)) return 0U;
    *intent = g_clipboard_intent;
    __DMB();
    g_clipboard_intent_valid = 0U;
    return 1U;
}

static uint8_t control_domain_apply_track_structure(const control_track_intent_t *intent)
{
    uint8_t family[BRICK_ENTITY_CAPACITY];
    uint8_t type[BRICK_ENTITY_CAPACITY];
    uint8_t midi_channel[BRICK_ENTITY_CAPACITY];
    uint8_t midi_source[BRICK_ENTITY_CAPACITY];
    uint8_t external_input[TRACK_COUNT];

    if (intent->track >= BRICK_ENTITY_CAPACITY) return 0U;
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        family[entity] = (uint8_t)track_state_get_family(entity);
        type[entity] = (uint8_t)track_state_get_type(entity);
        midi_channel[entity] = track_state_get_midi_channel(entity);
        midi_source[entity] = (uint8_t)track_state_get_midi_source(entity);
        if (entity < TRACK_COUNT)
            external_input[entity] = track_state_get_external_input(entity);
    }

    const track_family_t previous_family = track_state_get_family(intent->track);
    family[intent->track] = intent->value0;
    type[intent->track] = intent->value1;
    if (!track_structure_apply_entity_bulk_with_inputs(
            family, type, midi_channel, midi_source, external_input))
        return 0U;

    if ((previous_family == TRACK_FAMILY_EXTERNAL)
            || (family[intent->track] == (uint8_t)TRACK_FAMILY_EXTERNAL))
        mod_lfo_v1_invalidate_dest_cache_all();
    else
        mod_lfo_v1_invalidate_dest_cache_track(intent->track);

    if (intent->track == ui_get_active_track())
    {
        if ((previous_family == TRACK_FAMILY_OFF)
                && (family[intent->track] != (uint8_t)TRACK_FAMILY_OFF))
            ui_active_track_sync_after_track_creation_from_off(1U);
        else
            ui_active_track_sync_after_track_structure_change(1U);
    }
    return 1U;
}

static void control_domain_apply_track_intent(const control_track_intent_t *intent)
{
    switch ((control_track_operation_t)intent->operation)
    {
    case CONTROL_TRACK_SET_STRUCTURE:
        (void)control_domain_apply_track_structure(intent);
        break;
    case CONTROL_TRACK_SET_TYPE:
    {
        control_track_intent_t structure = *intent;
        structure.operation = CONTROL_TRACK_SET_STRUCTURE;
        structure.value0 = (uint8_t)track_state_get_family(intent->track);
        (void)control_domain_apply_track_structure(&structure);
        break;
    }
    case CONTROL_TRACK_SET_MIDI_CHANNEL:
        if (track_state_set_track_midi_channel(intent->track, intent->value0))
            track_runtime_rebuild_track(intent->track);
        break;
    case CONTROL_TRACK_SET_MIDI_SOURCE:
        if (track_state_set_track_midi_source(
                intent->track, (track_midi_source_t)intent->value0))
            track_runtime_rebuild_track(intent->track);
        break;
    case CONTROL_TRACK_SET_EXTERNAL_INPUT:
        if (track_state_set_external_input(intent->track, intent->value0))
        {
            mod_lfo_v1_invalidate_dest_cache_all();
            track_runtime_rebuild_track(intent->track);
            if (intent->track == ui_get_active_track())
                ui_active_track_sync_after_track_structure_change(1U);
        }
        break;
    case CONTROL_TRACK_SET_MUTE:
        (void)track_mute_set(intent->track, intent->value0);
        break;
    case CONTROL_TRACK_SET_MUTE_MASK:
        for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
        {
            if ((entity_topology_is_active((brick_entity_id_t)track) != 0U)
                    && (track_runtime_has_capability(
                            track, TRACK_CAPABILITY_MUTE) != 0U))
                (void)track_mute_set(track,
                (uint8_t)((intent->mute_mask >> track) & 1U));
        }
        break;
    case CONTROL_TRACK_SET_LOOPER_CONFIG:
    {
        audio_recorder_looper_config_t config = {
            .arm_mode = intent->value0,
            .length_mode = intent->value1,
            .play_auto = intent->value2
        };
        (void)audio_recorder_control_set_looper_config(intent->track, &config);
        break;
    }
    default:
        break;
    }
}

void control_domain_process_track_intents(void)
{
    control_track_intent_t intent;
    for (uint8_t count = 0U; count < CONTROL_TRACK_INTENT_CAPACITY; ++count)
    {
        if (control_domain_take_track(&intent) == 0U) break;
        control_domain_apply_track_intent(&intent);
    }
}

void control_domain_process_routing_intents(void)
{
    control_routing_intent_t intent;
    for (uint8_t count = 0U; count < CONTROL_ROUTING_INTENT_CAPACITY; ++count)
    {
        if (control_domain_take_routing(&intent) == 0U) break;
        (void)control_routing_set_looper_source(
            (brick_entity_id_t)intent.looper,
            (brick_entity_id_t)intent.source,
            intent.enabled);
    }
}

static void control_domain_commit_param_base(
    const control_param_intent_t *intent)
{
    if (live_parameter_is_audio_owned(intent->parameter_id) != 0U)
        return;

    uint8_t set_id = 0U;
    seq_param_slot_t param_slot = 0U;
    seq_value16_t encoded;
    uint8_t slot_found = 0U;
    for (uint8_t candidate = 0U; candidate < SEQ_PLOCK_SET_COUNT; ++candidate)
    {
        if (seq_param_iface_param_to_slot(intent->track, candidate,
                                          intent->parameter_id,
                                          &param_slot) != 0U)
        {
            set_id = candidate;
            slot_found = 1U;
            break;
        }
    }
    if ((slot_found == 0U)
            || (seq_param_iface_encode_param_value(
                    intent->parameter_id, intent->value, &encoded) == 0U))
    {
        return;
    }

    const seq_param_iface_base_commit_cmd_t cmd = {
        .source = SEQ_PARAM_IFACE_COMMIT_SOURCE_UI_TRACK_EDIT,
        .authoritative_apply_done = 1U,
        .target_track = intent->track,
        .set_id = set_id,
        .param_slot = param_slot,
        .value16 = encoded
    };
    (void)seq_param_iface_commit_base_after_authoritative_apply(&cmd);
}

static void control_domain_apply_param_intent(
    const control_param_intent_t *intent)
{
    if (intent->scope == (uint8_t)CONTROL_PARAM_SCOPE_GLOBAL)
    {
        (void)param_registry_commit_global(intent->parameter_id, intent->value);
        return;
    }

    if (intent->scope != (uint8_t)CONTROL_PARAM_SCOPE_TRACK)
        return;
    if (param_registry_apply_track_value(
            intent->parameter_id, intent->track, intent->value) == 0U)
        return;
    control_domain_commit_param_base(intent);
}

void control_domain_process_param_intents(void)
{
    control_param_intent_t intent;
    for (uint8_t count = 0U; count < CONTROL_PARAM_INTENT_CAPACITY; ++count)
    {
        if (control_domain_take_param(&intent) == 0U) break;
        control_domain_apply_param_intent(&intent);
    }
}

static void control_domain_apply_seq_intent(const control_seq_intent_t *intent)
{
    switch ((control_seq_operation_t)intent->operation)
    {
    case CONTROL_SEQ_STEP_PRESS:
        seq_edit_step_press(intent->track, intent->step);
        break;
    case CONTROL_SEQ_STEP_RELEASE:
        seq_edit_step_release(intent->track, intent->step);
        break;
    case CONTROL_SEQ_CHANGE_PAGE:
        seq_edit_change_page(intent->track, intent->delta);
        break;
    case CONTROL_SEQ_ROLL_DELTA:
        (void)seq_edit_adjust_held_step_roll(intent->delta, NULL, NULL, NULL);
        break;
    case CONTROL_SEQ_SET_LENGTH:
        (void)seq_edit_set_track_length(intent->track, intent->step);
        break;
    case CONTROL_SEQ_SET_DIVISION:
        (void)seq_edit_set_track_division(intent->track, intent->step);
        break;
    case CONTROL_SEQ_SET_QUANTIZATION:
        (void)seq_edit_set_track_quantization(intent->track, intent->step);
        break;
    case CONTROL_SEQ_SET_SWING:
        (void)seq_edit_set_track_swing(intent->track, intent->step);
        break;
    case CONTROL_SEQ_PLAY_SET:
        if (intent->step == 0xFFU)
            (void)seq_model_play_base_set(
                intent->track, intent->voice,
                (seq_step_play_field_t)intent->field, intent->value);
        else
        {
            const seq_plock_op_status_t status = seq_edit_step_play_upsert(
                intent->track, intent->step, intent->voice,
                (seq_step_play_field_t)intent->field, intent->value);
            if ((status == SEQ_PLOCK_OP_CREATED) || (status == SEQ_PLOCK_OP_UPDATED))
                seq_edit_step_play_commit(intent->track, intent->step,
                                          intent->voice,
                                          (seq_step_play_field_t)intent->field);
        }
        break;
    case CONTROL_SEQ_PLOCK_UPSERT:
    {
        const seq_plock_op_status_t status = seq_edit_step_plock_upsert(
            intent->track, intent->step, intent->set_id,
            intent->param_slot, intent->value16, intent->flags);
        if ((status == SEQ_PLOCK_OP_CREATED) || (status == SEQ_PLOCK_OP_UPDATED))
            seq_edit_step_plock_commit(intent->track, intent->step,
                                       intent->set_id, intent->param_slot);
        break;
    }
    case CONTROL_SEQ_PLOCK_DELETE:
        (void)seq_edit_step_plock_delete(intent->track, intent->step,
                                         intent->set_id, intent->param_slot);
        break;
    case CONTROL_SEQ_LIVE_REC_PLOCK_WRITE:
        (void)seq_runtime_live_rec_param_write(
            intent->track, intent->set_id, intent->param_slot, intent->value16);
        break;
    case CONTROL_SEQ_TRANSPORT_TOGGLE:
        seq_runtime_toggle_play_stop();
        break;
    case CONTROL_SEQ_RECORD_TARGET_ARM:
        seq_runtime_set_pattern_rec_target_track(intent->track);
        seq_runtime_rec_toggle_arm();
        break;
    case CONTROL_SEQ_SET_RECORD_LENGTH_MODE:
        seq_runtime_set_rec_len_mode(intent->step);
        break;
    case CONTROL_SEQ_SET_RECORD_START_MODE:
        seq_runtime_set_rec_start_mode(intent->step);
        break;
    case CONTROL_SEQ_SET_TEMPO:
        seq_runtime_set_tempo_bpm_milli(intent->value32);
        break;
    case CONTROL_SEQ_SET_CLOCK_SOURCE:
        seq_runtime_set_clock_source((seq_clock_src_t)intent->step);
        break;
    case CONTROL_SEQ_SET_METRONOME:
        (void)metronome_control_set_level(intent->step);
        break;
    default:
        break;
    }
}

void control_domain_process_seq_intents(void)
{
    control_seq_intent_t intent;
    for (uint8_t count = 0U; count < CONTROL_SEQ_INTENT_CAPACITY; ++count)
    {
        if (control_domain_take_seq(&intent) == 0U) break;
        control_domain_apply_seq_intent(&intent);
    }
    seq_edit_step_hold_update();
}

static void control_domain_apply_mod_intent(const control_mod_intent_t *intent)
{
    switch ((control_mod_operation_t)intent->operation)
    {
    case CONTROL_MOD_SET_SELECTED_SLOT:
        (void)mod_matrix_set_selected_slot(intent->track, intent->value);
        break;
    case CONTROL_MOD_SET_SELECTED_SOURCE:
        (void)mod_matrix_set_selected_slot_source(intent->track, intent->value);
        break;
    case CONTROL_MOD_SET_SELECTED_DESTINATION:
        (void)mod_matrix_set_selected_slot_destination_index(intent->track, intent->value);
        break;
    case CONTROL_MOD_SET_SELECTED_DEPTH:
        (void)mod_matrix_set_selected_slot_depth(intent->track, intent->value);
        break;
    case CONTROL_MOD_SET_MULTI_SOURCE:
        (void)mod_matrix_set_multi_source(intent->track, intent->index,
                                          intent->input, intent->value);
        break;
    case CONTROL_MOD_SET_SLEW_SOURCE:
        (void)mod_matrix_set_slew_source(intent->track, intent->index,
                                         intent->value);
        break;
    case CONTROL_MOD_SET_SLEW_AMOUNT:
        (void)mod_matrix_set_slew_amount(intent->track, intent->index,
                                         intent->value);
        break;
    default:
        break;
    }
}

void control_domain_process_mod_intents(void)
{
    control_mod_intent_t intent;
    for (uint8_t count = 0U; count < CONTROL_MOD_INTENT_CAPACITY; ++count)
    {
        if (control_domain_take_mod(&intent) == 0U) break;
        control_domain_apply_mod_intent(&intent);
    }
}

static void control_domain_apply_macro_intent(const control_macro_intent_t *intent)
{
    switch ((control_macro_operation_t)intent->operation)
    {
    case CONTROL_MACRO_ASSIGN_SCENE_LOCK:
        (void)project_control_assign_scene_lock(intent->scene, intent->track,
                                                intent->parameter, intent->value);
        break;
    case CONTROL_MACRO_CLEAR_SCENE_LOCK:
        (void)project_control_clear_scene_lock(intent->scene, intent->track,
                                               intent->parameter);
        break;
    case CONTROL_MACRO_SET_SCENE_SOURCE_AMOUNT:
        (void)param_macro_set_scene_source_amount(intent->scene, intent->value);
        break;
    case CONTROL_MACRO_RELEASE_SCENE_SOURCE:
        param_macro_release_scene_source(intent->scene);
        break;
    case CONTROL_MACRO_RELEASE_ALL_SCENE_SOURCES:
        for (uint8_t scene = 0U;
             scene < PERSIST_CONTROL_MACRO_SCENE_COUNT; ++scene)
            param_macro_release_scene_source(scene);
        break;
    case CONTROL_MACRO_SET_HALL_MODE:
        (void)project_control_set_hall_mode(
            (project_control_hall_mode_t)intent->scene);
        break;
    default:
        break;
    }
}

void control_domain_process_macro_intents(void)
{
    control_macro_intent_t intent;
    for (uint8_t count = 0U; count < CONTROL_MACRO_INTENT_CAPACITY; ++count)
    {
        if (control_domain_take_macro(&intent) == 0U) break;
        control_domain_apply_macro_intent(&intent);
    }
}

static const char *control_domain_asset_path(uint32_t kind, uint16_t runtime)
{
    if (kind == PERSIST_ASSET_MULTI)
    {
        const multi_sample_instrument_t *const instrument =
            multi_sample_pool_get_instrument(runtime);
        return (instrument != NULL) ? instrument->index_path : NULL;
    }

    const sample_global_slot_t *const slot = sample_global_pool_get_slot(runtime);
    if ((slot == NULL) || (slot->path[0] == '\0')) return NULL;
    return slot->path;
}

static void control_domain_apply_asset_intent(const control_asset_intent_t *intent)
{
    if (intent->operation == CONTROL_ASSET_SELECT_TRACK_LOGICAL)
    {
        (void)project_control_track_asset_select_logical(
            intent->track, (project_control_asset_role_t)intent->role,
            intent->logical);
        return;
    }

    if (intent->operation == CONTROL_ASSET_REGISTER_RUNTIME)
    {
        const char *const path = control_domain_asset_path(
            intent->kind, intent->runtime);
        uint16_t logical = UINT16_MAX;
        if (path == NULL) return;
        if (intent->kind == PERSIST_ASSET_SAMPLE_STREAM
            || intent->kind == PERSIST_ASSET_SAMPLE_RAM)
            (void)project_control_register_sample_runtime(
                intent->kind, path, intent->runtime, &logical);
        else if (intent->kind == PERSIST_ASSET_WAVETABLE)
            (void)project_control_register_wavetable_runtime(
                path, intent->runtime, &logical);
        else if (intent->kind == PERSIST_ASSET_MULTI)
            (void)project_control_register_multi_runtime(
                path, intent->runtime, &logical);
        return;
    }

    if (intent->operation != CONTROL_ASSET_REMOVE_RUNTIME) return;

    if (intent->kind == PERSIST_ASSET_SAMPLE_STREAM)
    {
        if (sample_global_pool_request_clear_classic(intent->runtime) != 0U)
            (void)project_control_remove_sample(intent->logical);
    }
    else if (intent->kind == PERSIST_ASSET_SAMPLE_RAM)
    {
        if (sampler_ram_pool_request_clear(intent->runtime) != 0U)
            (void)project_control_remove_sample(intent->logical);
    }
    else if (intent->kind == PERSIST_ASSET_WAVETABLE)
    {
        if (wavetable_pool_request_clear(intent->runtime) != 0U)
            (void)project_control_remove_wavetable(intent->logical);
    }
    else if (intent->kind == PERSIST_ASSET_MULTI)
    {
        if (multi_sample_pool_request_clear_instrument(intent->runtime) != 0U)
            (void)project_control_remove_multi(intent->logical);
    }
}

void control_domain_process_asset_intents(void)
{
    control_asset_intent_t intent;
    if (control_domain_take_asset(&intent) != 0U)
        control_domain_apply_asset_intent(&intent);
}

void control_domain_init(void)
{
    g_project_intent_head = 0U;
    g_project_intent_tail = 0U;
    g_patch_intent_head = 0U;
    g_patch_intent_tail = 0U;
    g_track_intent_head = 0U;
    g_track_intent_tail = 0U;
    g_routing_intent_head = 0U;
    g_routing_intent_tail = 0U;
    g_param_intent_head = 0U;
    g_param_intent_tail = 0U;
    g_seq_intent_head = 0U;
    g_seq_intent_tail = 0U;
    g_mod_intent_head = 0U;
    g_mod_intent_tail = 0U;
    g_macro_intent_head = 0U;
    g_macro_intent_tail = 0U;
    g_asset_intent_valid = 0U;
    g_clipboard_intent_valid = 0U;
    control_rt_publication_init();
    project_load_quiesce_init();
    board_usb_device_init();
    sd_access_gate_init();
    wav_convert_init();
    waveform_cache_init();
    sd_preview_init();
    sample_page_cache_init();
    sample_global_pool_init();
    sampler_ram_pool_init();
    wavetable_pool_init();
    multi_sample_pool_init();
    multi_sample_loader_init();
    sample_cache_init();
    audio_recorder_init();
}

void control_domain_start(float postgain, float output_compensation)
{
    engine_tasklet_init(48000U);
    param_registry_init();
    track_state_init();
    seq_runtime_init();
    brick6_boot_fx_policy_init();
    ui_core_init();
    (void)param_registry_commit_global(PARAM_POST_GAIN, postgain);
    (void)param_registry_commit_global(PARAM_OUTPUT_COMP, output_compensation);
    brick6_boot_apply_param_defaults();
    project_control_init();
    pattern_live_init();
    patch_product_init();
    project_product_init();
    ui_boot_loading_begin();
    undo_v2_init();
    hall_loop_init();
    hall_keyboard_bridge_init();
    if (hall_calibration_load() != 0U)
    {
        ui_page_set(UI_PAGE_TEMPLATE_CFG);
    }
    else
    {
        ui_page_set(UI_PAGE_CALIBRATION);
    }
    ui_active_track_sync_full_after_global_restore();
    brick6_stream_service_task_init();
    midi_init();
}
