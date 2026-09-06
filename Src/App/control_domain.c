#include "App/control_domain.h"

#include "App/brick6_boot_defaults.h"
#include "App/brick6_boot_fx_policy.h"
#include "App/control_clipboard.h"
#include "App/control_rt_sampled_state.h"
#include "App/control_rt_wakeup.h"
#include "App/live_parameter_audio_publication.h"
#include "encoders.h"
#include "App/Hall/hall_calibration.h"
#include "App/Hall/hall_engine.h"
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
#include "Sampler/sample_stream_admission.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_edit.h"
#include "Seq/metronome_control.h"
#include "Storage/audio_recorder.h"
#include "Storage/patch_product.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/pattern_load_storage.h"
#include "Storage/pattern_control_bank.h"
#include "Storage/project_control.h"
#include "Storage/project_audio_prepared_state.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/project_product.h"
#include "Storage/sd_access_gate.h"
#include "Storage/sd_preview.h"
#include "Storage/sample_capture.h"
#include "Storage/undo_v2.h"
#include "Storage/wav_convert.h"
#include "Storage/waveform_cache.h"
#include "Storage/storage_io_wakeup.h"
#include "Track/track_state.h"
#include "Track/track_mute.h"
#include "Track/control_routing.h"
#include "Track/entity_topology.h"
#include "Track/track_runtime.h"
#include "Track/audio_fx_control_state.h"
#include "Track/polyphony_control.h"
#include "Keyboard/keyboard_runtime.h"
#include "Mod/mod_lfo_v1_control.h"
#include "Mod/mod_matrix_control.h"
#include "Platform/memory_layout.h"
#include "ControlRT/control_rt_publication.h"
#include "IPC/control_audio_command.h"
#include "IPC/control_audio_rec_bus.h"
#include "ui_boot_loading.h"
#include "ui_core.h"
#include "ui_page_manager.h"
#include "UI/ui_service_wakeup.h"
#include "stm32h7xx.h"

#include <stdio.h>
#include <string.h>

#define CONTROL_UI_FIFO_MASK (CONTROL_UI_FIFO_CAPACITY - 1U)
#define CONTROL_UI_PROCESS_BUDGET 16U

#define CONTROL_HALL_PRESSURE_RAW_NOISE_FLOOR 400U
#define CONTROL_HALL_PRESSURE_RAW_NOISE_MARGIN 200U
#define CONTROL_HALL_PRESSURE_HYST 150U
#define CONTROL_HALL_PRESSURE_AMOUNT_DEADZONE 25U

_Static_assert((CONTROL_UI_FIFO_CAPACITY
                & (CONTROL_UI_FIFO_CAPACITY - 1U)) == 0U,
               "UI to CONTROL FIFO capacity must be a power of two");

CONTROL_M4_SRAM2 static control_ui_message_t
    g_control_ui_fifo[CONTROL_UI_FIFO_CAPACITY];
static volatile uint32_t g_control_ui_head;
static volatile uint32_t g_control_ui_tail;
static volatile uint32_t g_control_ui_overflow_count;
static volatile uint8_t g_control_project_request_pending;
static uint8_t g_control_hall_pressure_active[HALL_UI_LANE_COUNT];

static void control_domain_update_hall_pressure(uint8_t scene)
{
    const uint16_t on_delta =
        (uint16_t)(CONTROL_HALL_PRESSURE_RAW_NOISE_FLOOR
                   + CONTROL_HALL_PRESSURE_RAW_NOISE_MARGIN);
    const uint16_t off_delta = (on_delta > CONTROL_HALL_PRESSURE_HYST)
        ? (uint16_t)(on_delta - CONTROL_HALL_PRESSURE_HYST) : 0U;
    const uint16_t min_value = hall_engine_get_min(scene);
    const uint16_t max_value = hall_engine_get_max(scene);
    const uint16_t raw_value = hall_engine_get_raw(scene);
    const uint16_t delta = (raw_value > min_value)
        ? (uint16_t)(raw_value - min_value) : 0U;
    const uint8_t was_active = g_control_hall_pressure_active[scene];

    if ((max_value <= min_value)
        || ((uint16_t)(max_value - min_value) <= on_delta))
    {
        g_control_hall_pressure_active[scene] = 0U;
    }
    else if (g_control_hall_pressure_active[scene] == 0U)
    {
        if (delta >= on_delta)
            g_control_hall_pressure_active[scene] = 1U;
    }
    else if (delta <= off_delta)
    {
        g_control_hall_pressure_active[scene] = 0U;
    }

    if (g_control_hall_pressure_active[scene] != 0U)
    {
        const uint16_t range = (uint16_t)(max_value - min_value);
        const uint16_t amount_start =
            (uint16_t)(on_delta + CONTROL_HALL_PRESSURE_AMOUNT_DEADZONE);
        float amount = 0.0f;

        if (range > amount_start)
        {
            amount = ((float)delta - (float)amount_start)
                / ((float)range - (float)amount_start);
            if (amount < 0.0f) amount = 0.0f;
            if (amount > 1.0f) amount = 1.0f;
        }

        (void)param_macro_set_scene_source_amount(scene, amount);
    }
    else if (was_active != 0U)
    {
        param_macro_release_scene_source(scene);
    }
}

#define CONTROL_STORAGE_FIFO_CAPACITY 512U
#define CONTROL_STORAGE_FIFO_MASK (CONTROL_STORAGE_FIFO_CAPACITY - 1U)
#define CONTROL_STORAGE_PROCESS_BUDGET 16U

#define CONTROL_STORAGE_FIFO_MAX_RETIRE_EVENTS \
    (SAMPLER_RAM_POOL_MAX_SLOTS + WAVETABLE_POOL_MAX_SLOTS \
     + MULTI_SAMPLE_POOL_MAX_INSTRUMENTS + 2U)

_Static_assert((CONTROL_STORAGE_FIFO_CAPACITY
                & (CONTROL_STORAGE_FIFO_CAPACITY - 1U)) == 0U,
               "Storage to CONTROL FIFO capacity must be a power of two");
_Static_assert(CONTROL_STORAGE_FIFO_CAPACITY
                   >= CONTROL_STORAGE_FIFO_MAX_RETIRE_EVENTS,
               "Storage to CONTROL FIFO must cover one complete retire burst");

CONTROL_M4_SRAM2 static control_storage_audio_event_t
    g_control_storage_fifo[CONTROL_STORAGE_FIFO_CAPACITY];
static volatile uint32_t g_control_storage_head;
static volatile uint32_t g_control_storage_tail;
static control_asset_terminal_t g_control_asset_terminals[CONTROL_ASSET_FAMILY_COUNT];
static volatile uint8_t g_control_asset_terminal_valid[CONTROL_ASSET_FAMILY_COUNT];
static volatile uint32_t g_control_storage_overflow_count;

static uint8_t control_domain_submit_ui_message(
    control_ui_message_type_t type,
    const control_ui_message_payload_t *payload,
    uint8_t wake)
{
    if (payload == NULL) return 0U;
    /* UI_SERVICE and STORAGE_IO both publish here on H743.  Serialize only
     * the publication window; CONTROL remains the sole consumer. */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint32_t head = g_control_ui_head;
    const uint32_t tail = g_control_ui_tail;
    if ((uint32_t)(head - tail) >= CONTROL_UI_FIFO_CAPACITY)
    {
        ++g_control_ui_overflow_count;
        __set_PRIMASK(primask);
        return 0U;
    }

    control_ui_message_t *const message =
        &g_control_ui_fifo[head & CONTROL_UI_FIFO_MASK];
    message->type = (uint8_t)type;
    message->reserved[0] = 0U;
    message->reserved[1] = 0U;
    message->reserved[2] = 0U;
    message->payload = *payload;
    __DMB();
    g_control_ui_head = head + 1U;
    if (type == CONTROL_UI_MSG_PROJECT)
        g_control_project_request_pending = 1U;
    __set_PRIMASK(primask);
    if (wake != 0U) control_rt_wakeup(CONTROL_RT_WAKE_UI);
    return 1U;
}

static uint8_t control_domain_take_ui_message(control_ui_message_t *message)
{
    if (message == NULL) return 0U;
    const uint32_t tail = g_control_ui_tail;
    if (tail == g_control_ui_head) return 0U;
    *message = g_control_ui_fifo[tail & CONTROL_UI_FIFO_MASK];
    __DMB();
    g_control_ui_tail = tail + 1U;
    return 1U;
}

static uint8_t control_domain_submit_storage_message(
    const control_storage_audio_event_t *message)
{
    if (message == NULL) return 0U;
    const uint32_t head = g_control_storage_head;
    const uint32_t tail = g_control_storage_tail;
    if ((uint32_t)(head - tail) >= CONTROL_STORAGE_FIFO_CAPACITY)
    {
        ++g_control_storage_overflow_count;
        return 0U;
    }
    g_control_storage_fifo[head & CONTROL_STORAGE_FIFO_MASK] = *message;
    __DMB();
    g_control_storage_head = head + 1U;
    control_rt_wakeup(CONTROL_RT_WAKE_STORAGE);
    return 1U;
}

uint8_t control_domain_publish_storage_event(
    const control_storage_audio_event_t *message)
{
    return control_domain_submit_storage_message(message);
}

static uint8_t control_domain_take_storage_message(
    control_storage_audio_event_t *message)
{
    if (message == NULL) return 0U;
    const uint32_t tail = g_control_storage_tail;
    if (tail == g_control_storage_head) return 0U;
    *message = g_control_storage_fifo[tail & CONTROL_STORAGE_FIFO_MASK];
    __DMB();
    g_control_storage_tail = tail + 1U;
    return 1U;
}

#define CONTROL_DOMAIN_REQUEST(_name, _type, _member, _intent_type) \
uint8_t control_domain_request_##_name(const _intent_type *intent) \
{ \
    if (intent == NULL) return 0U; \
    control_ui_message_payload_t payload = { 0 }; \
    payload._member = *intent; \
    return control_domain_submit_ui_message((_type), &payload, 1U); \
}

uint8_t control_domain_request_project(const control_project_intent_t *intent)
{
    if (intent == NULL)
        return 0U;
    if ((intent->operation == CONTROL_PROJECT_SAVE)
        || (intent->operation == CONTROL_PROJECT_LOAD))
    {
        if ((project_load_allowed() == 0U)
            || (sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA)
            || (g_control_project_request_pending != 0U)
            || (project_product_ui_busy() != 0U)
            || (pattern_storage_is_pending() != 0U)
            || (pattern_storage_save_busy() != 0U)
            || (pattern_control_bank_async_busy() != 0U))
            return 0U;
    }
    control_ui_message_payload_t payload = { 0 };
    payload.project = *intent;
    return control_domain_submit_ui_message(CONTROL_UI_MSG_PROJECT, &payload, 1U);
}
CONTROL_DOMAIN_REQUEST(patch, CONTROL_UI_MSG_PATCH, patch,
                       control_patch_intent_t)
CONTROL_DOMAIN_REQUEST(track, CONTROL_UI_MSG_TRACK, track,
                       control_track_intent_t)
CONTROL_DOMAIN_REQUEST(routing, CONTROL_UI_MSG_ROUTING, routing,
                       control_routing_intent_t)
CONTROL_DOMAIN_REQUEST(param, CONTROL_UI_MSG_PARAM, param,
                       control_param_intent_t)
CONTROL_DOMAIN_REQUEST(seq, CONTROL_UI_MSG_SEQ, seq,
                       control_seq_intent_t)
CONTROL_DOMAIN_REQUEST(mod, CONTROL_UI_MSG_MOD, mod,
                       control_mod_intent_t)
CONTROL_DOMAIN_REQUEST(macro, CONTROL_UI_MSG_MACRO, macro,
                       control_macro_intent_t)
CONTROL_DOMAIN_REQUEST(asset, CONTROL_UI_MSG_ASSET, asset,
                       control_asset_intent_t)

uint8_t control_domain_request_asset_deferred(const control_asset_intent_t *intent)
{
    if (intent == NULL) return 0U;
    control_ui_message_payload_t payload = { 0 };
    payload.asset = *intent;
    return control_domain_submit_ui_message(CONTROL_UI_MSG_ASSET, &payload, 0U);
}

uint8_t control_domain_publish_asset_terminal(const control_asset_terminal_t *terminal)
{
    if ((terminal == NULL) || (terminal->family >= CONTROL_ASSET_FAMILY_COUNT)
        || (g_control_asset_terminal_valid[terminal->family] != 0U)) return 0U;
    g_control_asset_terminals[terminal->family] = *terminal;
    __DMB();
    g_control_asset_terminal_valid[terminal->family] = 1U;
    control_rt_wakeup(CONTROL_RT_WAKE_UI);
    ui_service_dirty_set();
    return 1U;
}

uint8_t control_domain_asset_terminal_available(control_asset_family_t family)
{
    return (family < CONTROL_ASSET_FAMILY_COUNT)
        ? g_control_asset_terminal_valid[family] : 0U;
}

uint8_t control_domain_take_asset_terminal(control_asset_family_t family,
                                           control_asset_terminal_t *out_terminal)
{
    if ((family >= CONTROL_ASSET_FAMILY_COUNT) || (out_terminal == NULL)
        || (g_control_asset_terminal_valid[family] == 0U)) return 0U;
    *out_terminal = g_control_asset_terminals[family];
    __DMB();
    g_control_asset_terminal_valid[family] = 0U;
    return 1U;
}

CONTROL_DOMAIN_REQUEST(clipboard, CONTROL_UI_MSG_CLIPBOARD, clipboard,
                       control_clipboard_intent_t)

uint8_t control_domain_request_keyboard(uint8_t operation, int8_t value)
{
    control_ui_message_payload_t payload = { 0 };
    payload.keyboard.operation = operation;
    payload.keyboard.value = value;
    return control_domain_submit_ui_message(CONTROL_UI_MSG_KEYBOARD, &payload, 1U);
}

CONTROL_DOMAIN_REQUEST(audio_fx, CONTROL_UI_MSG_AUDIO_FX, audio_fx,
                       control_audio_fx_intent_t)
CONTROL_DOMAIN_REQUEST(audio_rec, CONTROL_UI_MSG_AUDIO_REC, audio_rec,
                       control_audio_rec_intent_t)
CONTROL_DOMAIN_REQUEST(audio_visual, CONTROL_UI_MSG_AUDIO_VISUAL, audio_visual,
                       control_audio_visual_intent_t)

#undef CONTROL_DOMAIN_REQUEST

uint8_t control_domain_request_polyphony(uint8_t track, uint8_t voices)
{
    control_ui_message_payload_t payload = { 0 };
    payload.polyphony.track = track;
    payload.polyphony.voices = voices;
    return control_domain_submit_ui_message(CONTROL_UI_MSG_POLYPHONY, &payload, 1U);
}

uint8_t control_domain_request_history(uint8_t redo)
{
    control_ui_message_payload_t payload = { 0 };
    payload.history.redo = (redo != 0U) ? 1U : 0U;
    return control_domain_submit_ui_message(CONTROL_UI_MSG_HISTORY, &payload, 1U);
}

uint8_t control_domain_request_preview_gain(float gain)
{
    control_ui_message_payload_t payload = { 0 };
    payload.preview_gain.gain = gain;
    return control_domain_submit_ui_message(CONTROL_UI_MSG_PREVIEW_GAIN, &payload, 1U);
}

uint8_t control_domain_request_rec_bus(uint16_t source_entity_mask,
                                       uint8_t arm, uint8_t source_flags,
                                       uint8_t has_sample_time,
                                       uint64_t sample_time)
{
    control_ui_message_payload_t payload = { 0 };
    payload.rec_bus.source_entity_mask = source_entity_mask;
    payload.rec_bus.arm = arm;
    payload.rec_bus.source_flags = source_flags;
    payload.rec_bus.has_sample_time = (has_sample_time != 0U) ? 1U : 0U;
    payload.rec_bus.sample_time = sample_time;
    return control_domain_submit_ui_message(CONTROL_UI_MSG_REC_BUS, &payload, 1U);
}

uint8_t control_domain_request_storage_ui(uint8_t operation)
{
    control_ui_message_payload_t payload = { 0 };
    payload.storage.operation = operation;
    return control_domain_submit_ui_message(CONTROL_UI_MSG_STORAGE, &payload, 1U);
}

uint8_t control_domain_request_calibration(uint8_t operation)
{
    control_ui_message_payload_t payload = { 0 };
    payload.calibration.operation = operation;
    return control_domain_submit_ui_message(CONTROL_UI_MSG_CALIBRATION, &payload, 1U);
}

static void control_domain_apply_keyboard_intent(
    const control_keyboard_intent_t *intent)
{
    switch ((control_keyboard_operation_t)intent->operation)
    {
    case CONTROL_KEYBOARD_SET_ROOT:
        keyboard_runtime_set_root((uint8_t)intent->value);
        break;
    case CONTROL_KEYBOARD_SET_SCALE:
        keyboard_runtime_set_scale((uint8_t)intent->value);
        break;
    case CONTROL_KEYBOARD_SET_OMNICHORD:
        keyboard_runtime_set_omnichord(intent->value != 0);
        break;
    case CONTROL_KEYBOARD_SET_NOTE_ORDER:
        keyboard_runtime_set_note_order((note_order_t)intent->value);
        break;
    case CONTROL_KEYBOARD_SET_CHORD_OVERRIDE:
        keyboard_runtime_set_chord_override(intent->value != 0);
        break;
    case CONTROL_KEYBOARD_SET_MONO_LAST:
        keyboard_runtime_set_mono_last(intent->value != 0);
        break;
    case CONTROL_KEYBOARD_STEP_OCTAVE:
        keyboard_runtime_step_octave(intent->value);
        break;
    case CONTROL_KEYBOARD_SET_VELOCITY_PROFILE:
        hall_set_velocity_profile((uint8_t)intent->value);
        hall_calibration_save();
        break;
    case CONTROL_KEYBOARD_SET_VELOCITY_MODE:
        hall_set_velocity_mode((uint8_t)intent->value);
        hall_calibration_save();
        break;
    case CONTROL_KEYBOARD_SET_VELOCITY_CURVE:
        hall_set_velocity_curve((uint8_t)intent->value);
        hall_calibration_save();
        break;
    default:
        break;
    }
}

static void control_domain_apply_audio_fx_intent(
    const control_audio_fx_intent_t *intent)
{
    switch ((control_audio_fx_operation_t)intent->operation)
    {
    case CONTROL_AUDIO_FX_SET_FILTER_POSITION:
        (void)audio_fx_control_set_filter_position(
            (brick_entity_id_t)intent->entity,
            (audio_fx_filter_pos_t)intent->value);
        break;
    case CONTROL_AUDIO_FX_SET_ORDER:
        (void)audio_fx_control_set_order(
            (brick_entity_id_t)intent->entity,
            (audio_fx_order_t)intent->value);
        break;
    case CONTROL_AUDIO_FX_SET_SPATIAL_MODE:
        (void)audio_fx_control_set_spatial_mode(
            (brick_entity_id_t)intent->entity,
            (audio_fx_slot_t)intent->slot, intent->value);
        break;
    default:
        break;
    }
}

static void control_domain_apply_polyphony_intent(
    const control_polyphony_intent_t *intent)
{
    polyphony_control_state_t polyphony;
    polyphony_control_state_t prepared_polyphony;
    audio_fx_control_state_t audio_fx;
    audio_fx_control_state_t prepared_audio_fx;
    live_parameter_audio_bulk_t bulk = { .capture_tick = 0U, .count = 0U };

    if ((polyphony_control_capture(intent->track, &polyphony) == 0U)
            || (audio_fx_control_state_capture(intent->track, &audio_fx) == 0U))
        return;
    polyphony.voice_count = intent->voices;
    if ((polyphony_control_prepare(&polyphony, &prepared_polyphony) == 0U)
            || (audio_fx_control_state_prepare_for_polyphony(
                intent->track, &audio_fx, prepared_polyphony.voice_count,
                &prepared_audio_fx) == 0U)
            || (polyphony_control_bulk_add(intent->track, &prepared_polyphony,
                &bulk) == 0U)
            || (audio_fx_control_state_bulk_add_prepared(intent->track,
                &prepared_audio_fx, &bulk) == 0U)
            || (bulk.count == 0U)
            || (live_parameter_audio_publication_submit_bulk_now(&bulk) == false))
        return;
    (void)polyphony_control_install_prepared(intent->track, &prepared_polyphony);
    (void)audio_fx_control_state_install_prepared(intent->track,
                                                  &prepared_audio_fx);
}

static void control_domain_apply_audio_rec_intent(
    const control_audio_rec_intent_t *intent)
{
    sample_capture_model_set_control_context(1U);
    switch ((control_audio_rec_operation_t)intent->operation)
    {
    case CONTROL_AUDIO_REC_TOGGLE_ROUTE:
        (void)sample_capture_model_toggle_route(intent->value0);
        break;
    case CONTROL_AUDIO_REC_SET_ARM:
        (void)sample_capture_model_set_arm(
            (sample_capture_arm_t)intent->value0);
        break;
    case CONTROL_AUDIO_REC_STEP_ARM:
        (void)sample_capture_model_step_arm(intent->delta);
        break;
    case CONTROL_AUDIO_REC_STEP_LENGTH:
        (void)sample_capture_model_step_len(intent->delta);
        break;
    case CONTROL_AUDIO_REC_STEP_QUANTIZATION:
        (void)sample_capture_model_step_quant(intent->delta);
        break;
    case CONTROL_AUDIO_REC_STEP_THRESHOLD:
        (void)sample_capture_model_step_threshold(intent->delta);
        break;
    case CONTROL_AUDIO_REC_TOGGLE_LINE:
        (void)sample_capture_model_toggle_line();
        break;
    case CONTROL_AUDIO_REC_TOGGLE_MIC:
        (void)sample_capture_model_toggle_mic();
        break;
    case CONTROL_AUDIO_REC_TOGGLE_USB:
        (void)sample_capture_model_toggle_usb();
        break;
    case CONTROL_AUDIO_REC_STEP_EDIT:
        (void)sample_capture_model_step_edit(intent->value0, intent->delta,
                                             intent->value1);
        break;
    case CONTROL_AUDIO_REC_RETURN:
        (void)sample_capture_model_return_to_audio_rec();
        break;
    case CONTROL_AUDIO_REC_AUDITION:
        (void)sample_capture_model_audition_trimmed();
        break;
    case CONTROL_AUDIO_REC_SAVE:
        (void)sample_capture_model_save_trimmed();
        break;
    case CONTROL_AUDIO_REC_ASSIGN:
        (void)sample_capture_model_assign_saved_take_to_pool();
        break;
    case CONTROL_AUDIO_REC_TOGGLE_ZCROSS:
        (void)sample_capture_model_toggle_zcross();
        break;
    case CONTROL_AUDIO_REC_STOP_CLIENT:
    {
        uint64_t sample_time = 0U;
        if (control_rt_now_sample(&sample_time) != 0U)
            (void)audio_recorder_request_stop_client_at(
                (audio_recorder_client_t)intent->value0, sample_time);
        break;
    }
    default:
        break;
    }
    sample_capture_model_set_control_context(0U);
}

static void control_domain_apply_history_intent(
    const control_history_intent_t *intent)
{
    if (intent->redo != 0U)
        (void)undo_v2_redo();
    else
        (void)undo_v2_undo();
}

static void control_domain_apply_audio_visual_intent(
    const control_audio_visual_intent_t *intent)
{
    uint16_t parameter_id = CONTROL_AUDIO_PARAM_AUDIO_WAVEFORM_REQUEST;
    uint32_t value = (uint32_t)intent->value;
    if (intent->operation != 0U)
    {
        parameter_id = CONTROL_AUDIO_PARAM_SYNTH_WAVEFORM_REQUEST;
        value = (uint32_t)intent->slot | ((uint32_t)intent->value << 8);
    }
    if (control_rt_publish_param_now(intent->entity, parameter_id, value, 0U)
            == 0U)
        Error_Handler();
}

static void control_domain_apply_rec_bus_intent(
    const control_rec_bus_intent_t *intent)
{
    const audio_rec_bus_arm_t arm = (audio_rec_bus_arm_t)intent->arm;
    const uint8_t published = (intent->has_sample_time != 0U)
        ? control_audio_rec_bus_publish_at(intent->source_entity_mask, arm,
                                           intent->source_flags,
                                           intent->sample_time)
        : control_audio_rec_bus_publish(intent->source_entity_mask, arm,
                                        intent->source_flags);
    if (published == 0U) Error_Handler();
}

static void control_domain_apply_storage_ui_intent(
    const control_storage_ui_intent_t *intent)
{
    if (intent->operation == CONTROL_STORAGE_UI_CANCEL_MULTI_LOAD)
        (void)multi_sample_cancel_load();
    else if (intent->operation == CONTROL_STORAGE_UI_CLEAR_CONVERSION)
        wav_convert_clear_finished();
}

uint8_t control_domain_request_storage_audio_param(uint8_t entity,
                                                   uint16_t parameter_id,
                                                   uint32_t value)
{
    const control_storage_audio_event_t message = {
        .type = CONTROL_STORAGE_EVENT_AUDIO_PARAM,
        .family = 1U,
        .requester = 0U,
        .result = 1U,
        .entity = entity,
        .parameter_id = parameter_id,
        .value = value
    };
    return control_domain_submit_storage_message(&message);
}

uint8_t control_domain_request_storage_record_stop(uint8_t client,
                                                   uint32_t request_id)
{
    const control_storage_audio_event_t message = {
        .type = CONTROL_STORAGE_EVENT_RECORD_STOP,
        .family = 2U,
        .requester = 0U,
        .result = 1U,
        .request_id = request_id,
        .client = client
    };
    return control_domain_submit_storage_message(&message);
}

uint8_t control_domain_request_storage_waveform_cache(const char *path,
                                                      uint8_t reason,
                                                      uint32_t frame_count,
                                                      uint32_t sample_rate)
{
    return waveform_cache_request_for_wav_known_duration(
        path,
        (waveform_cache_reason_t)reason,
        frame_count,
        sample_rate);
}

uint32_t control_domain_ui_overflow_count(void)
{
    return g_control_ui_overflow_count;
}

uint8_t control_domain_project_ui_busy(void)
{
    return ((g_control_project_request_pending != 0U)
            || (project_product_ui_busy() != 0U)) ? 1U : 0U;
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
    const uint8_t previous_looper = (uint8_t)(
        (previous_family == TRACK_FAMILY_SAMPLER)
        && (track_state_get_type(intent->track) == TRACK_TYPE_LOOPER));
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

    if ((previous_looper != 0U)
            && !((family[intent->track] == (uint8_t)TRACK_FAMILY_SAMPLER)
                && (type[intent->track] == (uint8_t)TRACK_TYPE_LOOPER)))
        (void)sample_stream_admission_control_request_looper_release(
            intent->track);

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
        (void)track_state_set_external_input(intent->track, intent->value0);
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
        {
            param_macro_release_scene_source(scene);
            if (scene < HALL_UI_LANE_COUNT)
                g_control_hall_pressure_active[scene] = 0U;
        }
        break;
    case CONTROL_MACRO_UPDATE_HALL_PRESSURE:
        if (intent->scene < HALL_UI_LANE_COUNT)
            control_domain_update_hall_pressure(intent->scene);
        break;
    case CONTROL_MACRO_SET_HALL_MODE:
        (void)project_control_set_hall_mode(
            (project_control_hall_mode_t)intent->scene);
        break;
    default:
        break;
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
        if (intent->kind == PERSIST_ASSET_SAMPLE_STREAM)
            storage_io_owner_wakeup(STORAGE_OWNER_STREAM);
        else if (intent->kind == PERSIST_ASSET_SAMPLE_RAM)
            storage_io_owner_wakeup(STORAGE_OWNER_SAMPLE_RAM);
        else if (intent->kind == PERSIST_ASSET_WAVETABLE)
            storage_io_owner_wakeup(STORAGE_OWNER_WAVETABLE);
        else if (intent->kind == PERSIST_ASSET_MULTI)
            storage_io_owner_wakeup(STORAGE_OWNER_MULTI);
        return;
    }

    if (intent->operation == CONTROL_ASSET_RETIRE_MULTI_FOR_REPLACE)
    {
        multi_sample_external_request_t request;
        if (multi_sample_load_peek_external(intent->request_id, &request) == 0U)
            return;
        if (request.cancelled != 0U)
        {
            (void)multi_sample_load_publish_external_result(
                intent->request_id, MULTI_SAMPLE_LOAD_CANCELLED);
            return;
        }
        if ((request.old_logical_id != MULTI_SAMPLE_POOL_INVALID_ID))
        {
            uint16_t runtime = MULTI_SAMPLE_POOL_INVALID_ID;
            if ((request.old_logical_id != intent->logical)
                || (request.instrument_id != intent->runtime)
                || (project_control_resolve_multi_runtime(
                        request.old_logical_id, &runtime) == 0U)
                || (runtime != request.instrument_id)
                || (project_control_remove_multi(request.old_logical_id) == 0U))
            {
                (void)multi_sample_load_publish_external_result(
                    intent->request_id, MULTI_SAMPLE_LOAD_POOL_FAIL);
                return;
            }
        }
        if (multi_sample_load_mark_canonical_retired(intent->request_id) == 0U)
            return;
        storage_io_owner_wakeup(STORAGE_OWNER_MULTI);
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

static void control_domain_apply_project_intent(
    const control_project_intent_t *intent)
{
    const uint8_t operation = (uint8_t)intent->operation + 1U;
    project_product_control_process_intent(operation, intent->slot);
}

static void control_domain_apply_patch_intent(
    const control_patch_intent_t *intent)
{
    patch_product_control_process_intent((uint8_t)intent->operation,
                                         intent->slot,
                                         intent->target_mask,
                                         intent->entity,
                                         intent->name);
}

static uint8_t control_domain_project_busy_allows_message(
    control_ui_message_type_t type)
{
    if (project_product_ui_busy() == 0U)
        return 1U;
    return (type == CONTROL_UI_MSG_PROJECT) ? 1U : 0U;
}

void control_domain_process_ui_messages(void)
{
    control_ui_message_t message;
    uint16_t processed = 0U;

    while ((processed < CONTROL_UI_PROCESS_BUDGET)
           && (control_domain_take_ui_message(&message) != 0U))
    {
        if ((control_domain_project_busy_allows_message(
                (control_ui_message_type_t)message.type) == 0U)
            && !((message.type == CONTROL_UI_MSG_ASSET)
                 && (message.payload.asset.operation
                     == CONTROL_ASSET_RETIRE_MULTI_FOR_REPLACE)))
        {
            ++processed;
            continue;
        }
        switch ((control_ui_message_type_t)message.type)
        {
        case CONTROL_UI_MSG_PROJECT:
            control_domain_apply_project_intent(&message.payload.project);
            break;
        case CONTROL_UI_MSG_PATCH:
            control_domain_apply_patch_intent(&message.payload.patch);
            break;
        case CONTROL_UI_MSG_TRACK:
            control_domain_apply_track_intent(&message.payload.track);
            break;
        case CONTROL_UI_MSG_ROUTING:
            (void)control_routing_set_looper_source(
                (brick_entity_id_t)message.payload.routing.looper,
                (brick_entity_id_t)message.payload.routing.source,
                message.payload.routing.enabled);
            break;
        case CONTROL_UI_MSG_PARAM:
            control_domain_apply_param_intent(&message.payload.param);
            break;
        case CONTROL_UI_MSG_SEQ:
            control_domain_apply_seq_intent(&message.payload.seq);
            break;
        case CONTROL_UI_MSG_MOD:
            control_domain_apply_mod_intent(&message.payload.mod);
            break;
        case CONTROL_UI_MSG_MACRO:
            control_domain_apply_macro_intent(&message.payload.macro);
            break;
        case CONTROL_UI_MSG_ASSET:
            control_domain_apply_asset_intent(&message.payload.asset);
            break;
        case CONTROL_UI_MSG_CLIPBOARD:
            control_clipboard_process(&message.payload.clipboard);
            break;
        case CONTROL_UI_MSG_KEYBOARD:
            control_domain_apply_keyboard_intent(&message.payload.keyboard);
            break;
        case CONTROL_UI_MSG_AUDIO_FX:
            control_domain_apply_audio_fx_intent(&message.payload.audio_fx);
            break;
        case CONTROL_UI_MSG_POLYPHONY:
            control_domain_apply_polyphony_intent(&message.payload.polyphony);
            break;
        case CONTROL_UI_MSG_AUDIO_REC:
            control_domain_apply_audio_rec_intent(&message.payload.audio_rec);
            break;
        case CONTROL_UI_MSG_HISTORY:
            control_domain_apply_history_intent(&message.payload.history);
            break;
        case CONTROL_UI_MSG_AUDIO_VISUAL:
            control_domain_apply_audio_visual_intent(
                &message.payload.audio_visual);
            break;
        case CONTROL_UI_MSG_PREVIEW_GAIN:
            sd_preview_set_gain(message.payload.preview_gain.gain);
            {
                union { float f; uint32_t u; } encoded = {
                    .f = sd_preview_get_gain()
                };
                if (control_rt_publish_param_now(
                        0U, CONTROL_AUDIO_PARAM_PREVIEW_GAIN,
                        encoded.u, 0U) == 0U)
                    Error_Handler();
            }
            break;
        case CONTROL_UI_MSG_REC_BUS:
            control_domain_apply_rec_bus_intent(&message.payload.rec_bus);
            break;
        case CONTROL_UI_MSG_STORAGE:
            control_domain_apply_storage_ui_intent(&message.payload.storage);
            break;
        case CONTROL_UI_MSG_CALIBRATION:
            if (message.payload.calibration.operation
                == (uint8_t)CONTROL_CALIBRATION_START_USER)
                hall_user_calibration_start();
            else
                hall_calibration_start();
            break;
        default:
            break;
        }
        ++processed;
    }

    if ((processed >= CONTROL_UI_PROCESS_BUDGET)
        && (g_control_ui_tail != g_control_ui_head))
        control_rt_wakeup(CONTROL_RT_WAKE_UI);

    if (g_control_ui_tail == g_control_ui_head)
        g_control_project_request_pending = 0U;

    if (processed != 0U)
    {
        ui_service_dirty_set();
        ui_service_led_dirty_set();
    }
}

void control_domain_process_storage_messages(void)
{
    control_storage_audio_event_t message;
    uint32_t processed = 0U;
    while ((processed < CONTROL_STORAGE_PROCESS_BUDGET)
           && (control_domain_take_storage_message(&message) != 0U))
    {
        switch ((control_storage_event_type_t)message.type)
        {
        case CONTROL_STORAGE_EVENT_AUDIO_PARAM:
            if (control_rt_publish_param_now(message.entity,
                    message.parameter_id, message.value, 0U) == 0U)
                Error_Handler();
            break;
        case CONTROL_STORAGE_EVENT_RECORD_STOP:
            if (audio_recorder_control_on_storage_record_stop(
                    message.request_id,
                    (audio_recorder_client_t)message.client) == 0U)
                break;
            break;
        case CONTROL_STORAGE_EVENT_RECORDER_PREPARED:
            if (audio_recorder_control_on_storage_prepared(message.request_id)
                    != 0U)
                sample_capture_control_on_recorder_prepared(
                    message.request_id);
            break;
        case CONTROL_STORAGE_EVENT_RECORDER_CANCELED:
            audio_recorder_control_on_storage_canceled(message.request_id);
            break;
        case CONTROL_STORAGE_EVENT_RECORDER_TAKE_READY:
            audio_recorder_control_on_storage_take_ready(message.request_id);
            break;
        case CONTROL_STORAGE_EVENT_RECORDER_ERROR:
            audio_recorder_control_on_storage_error(
                message.request_id,
                (audio_recorder_error_t)message.value,
                message.result);
            break;
        case CONTROL_STORAGE_EVENT_REC_EDIT_SAVED:
            sample_capture_control_on_storage_event(
                message.result, message.request_id);
            break;
        default:
            break;
        }
        ++processed;
    }

    if ((processed >= CONTROL_STORAGE_PROCESS_BUDGET)
        && (g_control_storage_tail != g_control_storage_head))
        control_rt_wakeup(CONTROL_RT_WAKE_STORAGE);
}

uint32_t control_domain_ui_pending_count(void)
{
    return g_control_ui_head - g_control_ui_tail;
}

uint32_t control_domain_storage_pending_count(void)
{
    return g_control_storage_head - g_control_storage_tail;
}

void control_domain_init(void)
{
    memset(g_control_asset_terminals, 0, sizeof(g_control_asset_terminals));
    memset((void *)g_control_asset_terminal_valid, 0,
           sizeof(g_control_asset_terminal_valid));
    sample_stream_admission_control_init();
    g_control_ui_head = 0U;
    g_control_ui_tail = 0U;
    g_control_ui_overflow_count = 0U;
    g_control_project_request_pending = 0U;
    g_control_storage_head = 0U;
    g_control_storage_tail = 0U;
    g_control_storage_overflow_count = 0U;
    control_rt_publication_init();
    project_load_quiesce_init();
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
    project_audio_prepared_state_init();
}

void control_domain_start(float postgain, float output_compensation)
{
    control_rt_sampled_state_init();
    param_registry_init();
    track_state_init();
    seq_runtime_init();
    brick6_boot_fx_policy_init();
    ui_core_init();
    control_audio_rec_bus_init();
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
    encoders_start_fast_poll();
    midi_init();
    board_usb_device_init();
}
