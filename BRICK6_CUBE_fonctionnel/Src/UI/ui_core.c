/**
 * @file ui_core.c
 * @brief Module applicatif ui_core.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à ui_core.
 * - Fournir les services internes utilisés par le firmware utilisateur.
 *
 * Architecture:
 * - Appelé par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dépendances matérielles et/ou modules utilisateur associés.
 *
 * Contraintes temps réel:
 * - IRQ: selon les API appelées.
 * - Hard realtime: selon le chemin d'exécution.
 * - malloc: éviter en chemin critique.
 *
 * Notes:
 * - Documentation ajoutée sans modification de la logique d'exécution.
 */

#include "ui_core.h"

#include <stdio.h>
#include <string.h>

#include "stm32h7xx_hal.h"

#include "buttons.h"
#include "encoders.h"
#include "pages/ui_page_param_test.h"
#include "pages/ui_page_debug_hall.h"
#include "pages/ui_page_calibration.h"
#include "pages/ui_page_template_filter.h"
#include "pages/ui_page_template_dx7.h"
#include "pages/ui_page_template_mod.h"
#include "pages/ui_page_template_cfg.h"
#include "pages/ui_page_template_keyboard.h"
#include "pages/ui_page_template_arp.h"
#include "pages/ui_page_template_seq.h"
#include "pages/ui_page_template_mix.h"
#include "pages/ui_page_template_play.h"
#include "pages/ui_page_settings.h"
#include "ui_event.h"
#include "ui_navigation.h"
#include "ui_page_manager.h"
#include "ui_param.h"
#include "ui_template_page.h"
#include "App/Hall/hall_calibration.h"
#include "App/Hall/hall_engine.h"
#include "Keyboard/keyboard_runtime.h"
#include "param_registry.h"
#include "param_store.h"
#include "audio_float.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_runtime.h"
#include "Core/track_runtime.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/undo_v1.h"

#define UI_CFG_TRACK_PARAM ((param_id_t)PARAM_CFG_TRACK)
#define UI_CFG_TRACK_TYPE_PARAM ((param_id_t)PARAM_CFG_TRACK_TYPE)
#define UI_CFG_TRACK_MIDI_CH_PARAM ((param_id_t)PARAM_CFG_MIDI_CH)
#define UI_CFG_TRACK_MIDI_SRC_PARAM ((param_id_t)PARAM_CFG_MIDI_SRC)
#define UI_CFG_REC_PARAM ((param_id_t)PARAM_CFG_REC)
#define UI_CFG_TEMPO_PARAM ((param_id_t)PARAM_CFG_TEMPO)
#define UI_CFG_SYNC_PARAM ((param_id_t)PARAM_CFG_SYNC)
#define UI_CFG_REC_LEN_PARAM ((param_id_t)PARAM_CFG_REC_LEN)
#define UI_HALL_KEYBOARD_MODE_TRIGGER 8U
#define UI_HALL_ARP_MODE_TRIGGER 9U
#define UI_HALL_SEQ_MODE_TRIGGER 10U
#define UI_HALL_MODE_DOUBLE_TAP_MS 400U
#define UI_FEEDBACK_DURATION_MS 1000U
#define UI_TRACK_MOD_BUTTON BTN_PARAM_8

typedef struct
{
    uint8_t active_track;
    uint8_t shift_down;
    uint8_t track_select_armed;
    ui_hall_mode_t hall_mode;
    uint32_t mode_tap_ms[UI_HALL_MODE_COUNT];
    uint32_t cfg_tap_ms[UI_TRACK_COUNT];
    ui_track_config_t track_configs[UI_TRACK_COUNT];
    uint8_t track_midi_channel[UI_TRACK_COUNT];
    uint8_t track_midi_source[UI_TRACK_COUNT];
    uint8_t hall_prev_pressed[HALL_KEY_COUNT];
    uint8_t hall_note_suppressed[HALL_KEY_COUNT];
    ui_pattern_substate_t pattern_substate;
    ui_pattern_mode_t pattern_mode;
    uint8_t pattern_selected_bank;
    ui_hall_mode_t pattern_prev_mode;
    uint8_t pattern_prev_mode_valid;
    char feedback_message[16];
    uint32_t feedback_until_ms;
} ui_track_state_t;

typedef enum
{
    UI_CLIPBOARD_SCOPE_NONE = 0,
    UI_CLIPBOARD_SCOPE_TRACK,
    UI_CLIPBOARD_SCOPE_ENSEMBLE,
    UI_CLIPBOARD_SCOPE_PAGE
} ui_clipboard_scope_t;

typedef struct
{
    uint8_t valid;
    ui_clipboard_scope_t scope;
    ui_track_family_t source_family;
    ui_track_type_t source_type;
    uint8_t param_count;
    param_id_t params[PARAM_COUNT];
    float values[PARAM_COUNT];
} ui_param_clipboard_t;

typedef struct
{
    uint8_t valid;
    ui_track_config_t config;
    uint8_t midi_channel;
    ui_track_midi_source_t midi_source;
    uint8_t seq_page;
    uint8_t seq_length;
    uint8_t seq_div;
    uint8_t seq_quant;
    uint8_t seq_swing;
    uint8_t param_count;
    param_id_t params[PARAM_COUNT];
    float values[PARAM_COUNT];
} ui_track_clipboard_t;

typedef struct
{
    ui_track_clipboard_t track;
    ui_param_clipboard_t ensemble;
    ui_param_clipboard_t page;
} ui_clipboard_state_t;

static ui_track_state_t g_ui_track_state = {
    .active_track = 0U,
    .shift_down = 0U,
    .track_select_armed = 0U,
    .hall_mode = UI_HALL_MODE_SEQ,
    .mode_tap_ms = { 0U },
    .cfg_tap_ms = { 0U },
    .track_configs = {
        { UI_TRACK_FAMILY_OFF, UI_TRACK_TYPE_AUDIO },
        { UI_TRACK_FAMILY_OFF, UI_TRACK_TYPE_AUDIO },
        { UI_TRACK_FAMILY_OFF, UI_TRACK_TYPE_AUDIO },
        { UI_TRACK_FAMILY_OFF, UI_TRACK_TYPE_AUDIO },
        { UI_TRACK_FAMILY_OFF, UI_TRACK_TYPE_AUDIO },
        { UI_TRACK_FAMILY_OFF, UI_TRACK_TYPE_AUDIO },
        { UI_TRACK_FAMILY_OFF, UI_TRACK_TYPE_AUDIO },
        { UI_TRACK_FAMILY_OFF, UI_TRACK_TYPE_AUDIO },
    },
    .track_midi_channel = { 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U },
    .track_midi_source = {
        (uint8_t)UI_TRACK_MIDI_SRC_ALL, (uint8_t)UI_TRACK_MIDI_SRC_ALL,
        (uint8_t)UI_TRACK_MIDI_SRC_ALL, (uint8_t)UI_TRACK_MIDI_SRC_ALL,
        (uint8_t)UI_TRACK_MIDI_SRC_ALL, (uint8_t)UI_TRACK_MIDI_SRC_ALL,
        (uint8_t)UI_TRACK_MIDI_SRC_ALL, (uint8_t)UI_TRACK_MIDI_SRC_ALL
    },
    .hall_prev_pressed = { 0U },
    .hall_note_suppressed = { 0U },
    .pattern_substate = UI_PATTERN_SUBSTATE_BANK_SELECT,
    .pattern_mode = UI_PATTERN_MODE_RECALL,
    .pattern_selected_bank = 0U,
    .pattern_prev_mode = UI_HALL_MODE_SEQ,
    .pattern_prev_mode_valid = 0U,
    .feedback_message = { 0 },
    .feedback_until_ms = 0U,
};

static ui_clipboard_state_t g_ui_clipboard;

volatile uint32_t g_ui_tb3_type_switch_stage = 0U;
volatile uint32_t g_ui_tb3_type_switch_track = 0U;
volatile uint32_t g_ui_tb3_type_switch_type = 0U;
volatile uint32_t g_ui_tb3_cfg_sync_seen = 0U;

typedef struct
{
    uint8_t hall_index;
    ui_hall_mode_t target_mode;
    uint8_t target_page;
} ui_hall_mode_trigger_t;

static const ui_hall_mode_trigger_t g_ui_hall_mode_triggers[] = {
    { UI_HALL_KEYBOARD_MODE_TRIGGER, UI_HALL_MODE_KEYBOARD, UI_PAGE_TEMPLATE_KEYBOARD },
    { UI_HALL_ARP_MODE_TRIGGER, UI_HALL_MODE_ARP, UI_PAGE_TEMPLATE_ARP },
    { UI_HALL_SEQ_MODE_TRIGGER, UI_HALL_MODE_SEQ, UI_PAGE_TEMPLATE_SEQ },
};

static void ui_core_pattern_reset_selection_only(void)
{
    g_ui_track_state.pattern_substate = UI_PATTERN_SUBSTATE_BANK_SELECT;
    g_ui_track_state.pattern_selected_bank = 0U;
}

static void ui_core_pattern_abort_internal(void)
{
    ui_core_pattern_reset_selection_only();
    g_ui_track_state.pattern_prev_mode_valid = 0U;
}

static void ui_core_pattern_enter(ui_pattern_mode_t mode)
{
    g_ui_track_state.pattern_prev_mode = ui_get_hall_mode();
    g_ui_track_state.pattern_prev_mode_valid = (g_ui_track_state.pattern_prev_mode != UI_HALL_MODE_PATTERN) ? 1U : 0U;
    g_ui_track_state.pattern_mode = mode;
    ui_core_pattern_reset_selection_only();
    ui_set_hall_mode(UI_HALL_MODE_PATTERN);
}

static void ui_core_pattern_exit_to_previous_mode(void)
{
    const ui_hall_mode_t target = (g_ui_track_state.pattern_prev_mode_valid != 0U)
            ? g_ui_track_state.pattern_prev_mode
            : UI_HALL_MODE_SEQ;
    g_ui_track_state.pattern_prev_mode_valid = 0U;
    ui_set_hall_mode(target);
}


static ui_track_config_t ui_core_get_default_track_config(void)
{
    ui_track_config_t config = {
        .family = UI_TRACK_FAMILY_OFF,
        .type = UI_TRACK_TYPE_AUDIO,
    };

    return config;
}

bool ui_track_family_is_input(ui_track_family_t family)
{
    return (family == UI_TRACK_FAMILY_INPUT1)
            || (family == UI_TRACK_FAMILY_INPUT2)
            || (family == UI_TRACK_FAMILY_INPUT3)
            || (family == UI_TRACK_FAMILY_INPUT4);
}

bool ui_track_family_is_engine(ui_track_family_t family)
{
    return (family == UI_TRACK_FAMILY_SYNTH) || (family == UI_TRACK_FAMILY_DRUM);
}

static const ui_track_type_t *ui_core_get_catalog_types_for_family(ui_track_family_t family, uint8_t *out_count)
{
    static const ui_track_type_t k_input_types[] = { UI_TRACK_TYPE_AUDIO, UI_TRACK_TYPE_HYBRID };
    static const ui_track_type_t k_synth_types[] = { UI_TRACK_TYPE_DX7, UI_TRACK_TYPE_MONOB, UI_TRACK_TYPE_TB3 };
    static const ui_track_type_t k_drum_types[] = {
        UI_TRACK_TYPE_DRUM_TRX_BD,
        UI_TRACK_TYPE_DRUM_TRX_CLAVES,
        UI_TRACK_TYPE_DRUM_TRX_HIHAT,
        UI_TRACK_TYPE_DRUM_TRX_SNARE,
        UI_TRACK_TYPE_DRUM_FM_KICK,
        UI_TRACK_TYPE_DRUM_FM_SNARE,
        UI_TRACK_TYPE_DRUM_FM_TOM,
        UI_TRACK_TYPE_DRUM_FM_RIMSHOT,
        UI_TRACK_TYPE_DRUM_FM_CLAP,
        UI_TRACK_TYPE_DRUM_FM_COWBELL,
        UI_TRACK_TYPE_DRUM_FM_CYMBAL
    };

    if (out_count == NULL)
    {
        return NULL;
    }

    switch (family)
    {
        case UI_TRACK_FAMILY_INPUT1:
        case UI_TRACK_FAMILY_INPUT2:
        case UI_TRACK_FAMILY_INPUT3:
        case UI_TRACK_FAMILY_INPUT4:
            *out_count = (uint8_t)(sizeof(k_input_types) / sizeof(k_input_types[0]));
            return k_input_types;

        case UI_TRACK_FAMILY_SYNTH:
            *out_count = (uint8_t)(sizeof(k_synth_types) / sizeof(k_synth_types[0]));
            return k_synth_types;

        case UI_TRACK_FAMILY_DRUM:
            *out_count = (uint8_t)(sizeof(k_drum_types) / sizeof(k_drum_types[0]));
            return k_drum_types;

        case UI_TRACK_FAMILY_OFF:
        default:
            *out_count = 0U;
            return NULL;
    }
}

bool ui_track_type_is_valid_for_family(ui_track_family_t family, ui_track_type_t type)
{
    if (((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
            || ((uint8_t)type >= (uint8_t)UI_TRACK_TYPE_COUNT))
    {
        return false;
    }

    uint8_t type_count = 0U;
    const ui_track_type_t *const catalog = ui_core_get_catalog_types_for_family(family, &type_count);
    if ((catalog == NULL) || (type_count == 0U))
    {
        return false;
    }

    for (uint8_t i = 0U; i < type_count; ++i)
    {
        if (catalog[i] == type)
        {
            return true;
        }
    }

    return false;
}

static uint8_t ui_core_track_uses_synth_type(uint8_t track, ui_track_type_t type)
{
    if (track >= UI_TRACK_COUNT)
    {
        return 0U;
    }

    const ui_track_config_t *const config = &g_ui_track_state.track_configs[track];
    return (uint8_t)((config->family == UI_TRACK_FAMILY_SYNTH) && (config->type == type));
}

bool ui_track_type_is_available(uint8_t track, ui_track_family_t family, ui_track_type_t type)
{
    if ((track >= UI_TRACK_COUNT) || !ui_track_type_is_valid_for_family(family, type))
    {
        return false;
    }

    if (family != UI_TRACK_FAMILY_SYNTH)
    {
        return true;
    }

    if ((type != UI_TRACK_TYPE_DX7) && (type != UI_TRACK_TYPE_TB3))
    {
        return true;
    }

    for (uint8_t other_track = 0U; other_track < UI_TRACK_COUNT; ++other_track)
    {
        if (other_track == track)
        {
            continue;
        }

        if (ui_core_track_uses_synth_type(other_track, type) != 0U)
        {
            return false;
        }
    }

    return true;
}

static uint8_t ui_core_get_track_type_count_for_family_and_track(ui_track_family_t family, uint8_t track)
{
    if ((track >= UI_TRACK_COUNT) || ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT))
    {
        return 0U;
    }

    if (family == UI_TRACK_FAMILY_OFF)
    {
        return 0U;
    }

    uint8_t catalog_count = 0U;
    const ui_track_type_t *const catalog = ui_core_get_catalog_types_for_family(family, &catalog_count);
    if ((catalog == NULL) || (catalog_count == 0U))
    {
        return 0U;
    }

    uint8_t count = 0U;
    for (uint8_t i = 0U; i < catalog_count; ++i)
    {
        if (ui_track_type_is_available(track, family, catalog[i]))
        {
            ++count;
        }
    }
    return count;
}

static ui_track_type_t ui_core_get_first_available_track_type(ui_track_family_t family, uint8_t track)
{
    uint8_t catalog_count = 0U;
    const ui_track_type_t *const catalog = ui_core_get_catalog_types_for_family(family, &catalog_count);
    if ((catalog == NULL) || (catalog_count == 0U))
    {
        return UI_TRACK_TYPE_AUDIO;
    }

    for (uint8_t i = 0U; i < catalog_count; ++i)
    {
        if (ui_track_type_is_available(track, family, catalog[i]))
        {
            return catalog[i];
        }
    }

    return UI_TRACK_TYPE_AUDIO;
}

ui_track_type_t ui_get_default_track_type_for_family(ui_track_family_t family)
{
    uint8_t catalog_count = 0U;
    const ui_track_type_t *const catalog = ui_core_get_catalog_types_for_family(family, &catalog_count);
    if ((catalog != NULL) && (catalog_count > 0U))
    {
        return catalog[0];
    }

    return UI_TRACK_TYPE_AUDIO;
}

uint8_t ui_get_track_type_count_for_family(ui_track_family_t family)
{
    return ui_core_get_track_type_count_for_family_and_track(family, g_ui_track_state.active_track);
}

uint8_t ui_get_track_type_index_for_family(ui_track_family_t family, ui_track_type_t type)
{
    const uint8_t track = g_ui_track_state.active_track;
    if (!ui_track_type_is_available(track, family, type))
    {
        return 0U;
    }

    uint8_t catalog_count = 0U;
    const ui_track_type_t *const catalog = ui_core_get_catalog_types_for_family(family, &catalog_count);
    if ((catalog == NULL) || (catalog_count == 0U))
    {
        return 0U;
    }

    uint8_t index = 0U;
    for (uint8_t i = 0U; i < catalog_count; ++i)
    {
        const ui_track_type_t candidate = catalog[i];
        if (!ui_track_type_is_available(track, family, candidate))
        {
            continue;
        }

        if (candidate == type)
        {
            return index;
        }

        ++index;
    }

    return 0U;
}

ui_track_type_t ui_get_track_type_from_family_index(ui_track_family_t family, uint8_t index)
{
    const uint8_t track = g_ui_track_state.active_track;

    if ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
    {
        return UI_TRACK_TYPE_AUDIO;
    }

    if (family == UI_TRACK_FAMILY_OFF)
    {
        return UI_TRACK_TYPE_AUDIO;
    }

    uint8_t catalog_count = 0U;
    const ui_track_type_t *const catalog = ui_core_get_catalog_types_for_family(family, &catalog_count);
    if ((catalog == NULL) || (catalog_count == 0U))
    {
        return ui_get_default_track_type_for_family(family);
    }

    uint8_t current = 0U;
    for (uint8_t i = 0U; i < catalog_count; ++i)
    {
        const ui_track_type_t candidate = catalog[i];
        if (!ui_track_type_is_available(track, family, candidate))
        {
            continue;
        }

        if (current == index)
        {
            return candidate;
        }

        ++current;
    }

    return ui_get_default_track_type_for_family(family);
}

static bool ui_core_track_family_is_available(uint8_t track, ui_track_family_t family)
{
    if ((track >= UI_TRACK_COUNT) || ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT))
    {
        return false;
    }

    if (family == UI_TRACK_FAMILY_OFF)
    {
        return true;
    }

    if (!ui_track_family_is_input(family))
    {
        return true;
    }

    for (uint8_t other_track = 0U; other_track < UI_TRACK_COUNT; other_track++)
    {
        if (other_track == track)
        {
            continue;
        }

        if (g_ui_track_state.track_configs[other_track].family == family)
        {
            return false;
        }
    }

    return true;
}

static uint8_t ui_core_has_track_family(ui_track_family_t family)
{
    if ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
    {
        return 0U;
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; track++)
    {
        if (g_ui_track_state.track_configs[track].family == family)
        {
            return 1U;
        }
    }

    return 0U;
}

static void ui_core_sync_audio_runtime_enables(void)
{
#if UI_AUDIO_INPUT_PROTO_WIRED_COUNT > UI_AUDIO_INPUT_RESOURCE_COUNT
#error "UI proto wired input count cannot exceed product input resource count"
#endif

    /*
     * Physical DSP ingress lanes (MAX_TRACKS=4) are currently mapped as:
     *  - lane 0 <- Input1
     *  - lane 1 <- Input2
     *  - lane 2 <- Input3
     *  - lane 3 <- internal synth bus
     *
     * Product target remains Input1..Input4 as physical stereo resources.
     * On the current devboard proto, Input4 is not yet wired to a dedicated
     * ingress lane, so no track_enable slot is toggled here for Input4.
     * Input4 still remains a valid logical/runtime family for future hardware.
     */
    track_enable(0U, ui_core_has_track_family(UI_TRACK_FAMILY_INPUT1));
    track_enable(1U, ui_core_has_track_family(UI_TRACK_FAMILY_INPUT2));
    track_enable(2U, ui_core_has_track_family(UI_TRACK_FAMILY_INPUT3));
    const uint8_t has_engine_track = (uint8_t)(ui_core_has_track_family(UI_TRACK_FAMILY_SYNTH)
            || ui_core_has_track_family(UI_TRACK_FAMILY_DRUM));
    track_enable(3U, has_engine_track);
}

static void ui_core_sync_active_track_cfg_params(void)
{
    const uint8_t active_track = g_ui_track_state.active_track;
    const ui_track_config_t *active_config = &g_ui_track_state.track_configs[active_track];

    param_store_set_active(UI_CFG_TRACK_PARAM, (float)active_config->family);
    param_store_set_active(UI_CFG_TRACK_TYPE_PARAM, (float)ui_get_track_type_index_for_family(active_config->family, active_config->type));
    param_store_set_active(UI_CFG_TRACK_MIDI_CH_PARAM, (float)g_ui_track_state.track_midi_channel[active_track]);
    param_store_set_active(UI_CFG_TRACK_MIDI_SRC_PARAM, (float)g_ui_track_state.track_midi_source[active_track]);
    param_store_set_active(UI_CFG_REC_PARAM, (float)seq_runtime_get_rec_count_in_mode());
    param_store_set_active(UI_CFG_TEMPO_PARAM, (float)seq_runtime_get_tempo_bpm_milli() / 1000.0f);
    {
        float sync_value = 0.0f;
        switch (seq_runtime_get_clock_source())
        {
            case SEQ_CLOCK_SRC_EXTERNAL_MIDI:
                sync_value = 1.0f;
                break;
            case SEQ_CLOCK_SRC_EXTERNAL_USB:
                sync_value = 2.0f;
                break;
            case SEQ_CLOCK_SRC_INTERNAL:
            default:
                sync_value = 0.0f;
                break;
        }
        param_store_set_active(UI_CFG_SYNC_PARAM, sync_value);
    }
    param_store_set_active(UI_CFG_REC_LEN_PARAM, (float)seq_runtime_get_rec_len_mode());
    param_registry_sync_ui_for_active_track();
    if (active_config->type == UI_TRACK_TYPE_TB3)
    {
        g_ui_tb3_cfg_sync_seen++;
    }
}

static void ui_core_set_active_track(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return;
    }

    if (g_ui_track_state.active_track == track)
    {
        ui_core_sync_active_track_cfg_params();
        return;
    }

    keyboard_runtime_on_active_track_changed();
    g_ui_track_state.active_track = track;
    ui_core_sync_active_track_cfg_params();
}

static void ui_core_reset_track_configs(void)
{
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; track++)
    {
        g_ui_track_state.track_configs[track].family = UI_TRACK_FAMILY_OFF;
        g_ui_track_state.track_configs[track].type = UI_TRACK_TYPE_AUDIO;

        g_ui_track_state.track_midi_channel[track] = (uint8_t)((track < 16U) ? (track + 1U) : 16U);
        g_ui_track_state.track_midi_source[track] = (uint8_t)UI_TRACK_MIDI_SRC_ALL;
    }

    ui_core_sync_audio_runtime_enables();
}

uint8_t ui_get_track_midi_channel(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return 1U;
    }

    const uint8_t ch = g_ui_track_state.track_midi_channel[track];
    return (ch < 1U) ? 1U : ((ch > 16U) ? 16U : ch);
}

bool ui_set_track_midi_channel(uint8_t track, uint8_t channel_1_16)
{
    if ((track >= UI_TRACK_COUNT) || (channel_1_16 < 1U) || (channel_1_16 > 16U))
    {
        return false;
    }

    g_ui_track_state.track_midi_channel[track] = channel_1_16;
    if (track == g_ui_track_state.active_track)
    {
        param_store_set_active(UI_CFG_TRACK_MIDI_CH_PARAM, (float)channel_1_16);
    }
    return true;
}

ui_track_midi_source_t ui_get_track_midi_source(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return UI_TRACK_MIDI_SRC_ALL;
    }

    const uint8_t source = g_ui_track_state.track_midi_source[track];
    if (source >= (uint8_t)UI_TRACK_MIDI_SRC_COUNT)
    {
        return UI_TRACK_MIDI_SRC_ALL;
    }
    return (ui_track_midi_source_t)source;
}

bool ui_set_track_midi_source(uint8_t track, ui_track_midi_source_t source)
{
    if ((track >= UI_TRACK_COUNT) || ((uint8_t)source >= (uint8_t)UI_TRACK_MIDI_SRC_COUNT))
    {
        return false;
    }

    g_ui_track_state.track_midi_source[track] = (uint8_t)source;
    if (track == g_ui_track_state.active_track)
    {
        param_store_set_active(UI_CFG_TRACK_MIDI_SRC_PARAM, (float)source);
    }
    return true;
}

bool ui_restore_track_config_bulk(const uint8_t family[UI_TRACK_COUNT],
                                  const uint8_t type[UI_TRACK_COUNT],
                                  const uint8_t midi_channel[UI_TRACK_COUNT],
                                  const uint8_t midi_source[UI_TRACK_COUNT])
{
    if ((family == 0) || (type == 0) || (midi_channel == 0) || (midi_source == 0))
    {
        return false;
    }

    uint8_t input_family_count[(uint8_t)UI_TRACK_FAMILY_COUNT] = { 0U };

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const ui_track_family_t fam = (ui_track_family_t)family[track];
        const ui_track_type_t typ = (ui_track_type_t)type[track];
        const ui_track_midi_source_t src = (ui_track_midi_source_t)midi_source[track];

        if (((uint8_t)fam >= (uint8_t)UI_TRACK_FAMILY_COUNT)
                || ((uint8_t)src >= (uint8_t)UI_TRACK_MIDI_SRC_COUNT))
        {
            return false;
        }

        if (fam == UI_TRACK_FAMILY_OFF)
        {
            continue;
        }

        if (!ui_track_type_is_valid_for_family(fam, typ))
        {
            return false;
        }

        if (ui_track_family_is_input(fam))
        {
            input_family_count[(uint8_t)fam]++;
            if (input_family_count[(uint8_t)fam] > 1U)
            {
                return false;
            }
        }

    }

    /*
     * Compat: historical snapshots may contain several DX7/TB3 tracks.
     * Current runtime allows at most one instance for each.
     * Preserve the first requested slot and gracefully fold extras to MONOB
     * instead of rejecting the full snapshot load.
     */
    uint8_t dx7_kept = 0U;
    uint8_t tb3_kept = 0U;

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const ui_track_family_t fam = (ui_track_family_t)family[track];
        ui_track_type_t requested_type = (ui_track_type_t)type[track];

        if (fam == UI_TRACK_FAMILY_SYNTH)
        {
            if (requested_type == UI_TRACK_TYPE_DX7)
            {
                if (dx7_kept == 0U)
                {
                    dx7_kept = 1U;
                }
                else
                {
                    requested_type = UI_TRACK_TYPE_MONOB;
                }
            }
            else if (requested_type == UI_TRACK_TYPE_TB3)
            {
                if (tb3_kept == 0U)
                {
                    tb3_kept = 1U;
                }
                else
                {
                    requested_type = UI_TRACK_TYPE_MONOB;
                }
            }
        }

        g_ui_track_state.track_configs[track].family = fam;
        g_ui_track_state.track_configs[track].type = (fam == UI_TRACK_FAMILY_OFF)
            ? UI_TRACK_TYPE_AUDIO
            : requested_type;

        const uint8_t midi_ch = (midi_channel[track] < 1U)
            ? 1U
            : ((midi_channel[track] > 16U) ? 16U : midi_channel[track]);
        g_ui_track_state.track_midi_channel[track] = midi_ch;
        g_ui_track_state.track_midi_source[track] = midi_source[track];
    }

    ui_core_sync_audio_runtime_enables();
    keyboard_runtime_on_active_track_changed();
    ui_core_sync_active_track_cfg_params();
    return true;
}

uint8_t ui_track_midi_channel_used_by_other(uint8_t track, uint8_t channel_1_16)
{
    if ((track >= UI_TRACK_COUNT) || (channel_1_16 < 1U) || (channel_1_16 > 16U))
    {
        return 0U;
    }

    for (uint8_t other = 0U; other < UI_TRACK_COUNT; ++other)
    {
        if (other == track)
        {
            continue;
        }

        if (ui_get_track_midi_channel(other) == channel_1_16)
        {
            return 1U;
        }
    }

    return 0U;
}

static void ui_core_update_shift_state(uint8_t shift_down)
{
    if ((shift_down != 0U) && (g_ui_track_state.shift_down == 0U))
    {
        g_ui_track_state.shift_down = 1U;
        return;
    }

    if ((shift_down == 0U) && (g_ui_track_state.shift_down != 0U))
    {
        g_ui_track_state.shift_down = 0U;
    }
}

static void ui_core_update_track_modifier_state(uint8_t track_modifier_down)
{
    g_ui_track_state.track_select_armed = (track_modifier_down != 0U) ? 1U : 0U;
}

static const ui_hall_mode_trigger_t *ui_core_find_hall_mode_trigger(uint8_t hall)
{
    for (uint8_t i = 0U; i < (uint8_t)(sizeof(g_ui_hall_mode_triggers) / sizeof(g_ui_hall_mode_triggers[0])); ++i)
    {
        if (g_ui_hall_mode_triggers[i].hall_index == hall)
        {
            return &g_ui_hall_mode_triggers[i];
        }
    }

    return 0;
}

static void ui_core_activate_hall_mode_trigger(const ui_hall_mode_trigger_t *trigger, uint8_t open_target_page)
{
    if (trigger == 0)
    {
        return;
    }

    ui_set_hall_mode(trigger->target_mode);

    if (open_target_page != 0U)
    {
        ui_page_set(trigger->target_page);
    }
}

static void ui_core_handle_shift_hall_action(uint8_t hall)
{
    if (hall >= HALL_KEY_COUNT)
    {
        return;
    }

    const ui_hall_mode_trigger_t *trigger = ui_core_find_hall_mode_trigger(hall);
    if (trigger != 0)
    {
        g_ui_track_state.hall_note_suppressed[hall] = 1U;
        const uint32_t now = HAL_GetTick();
        const uint32_t last_tap = g_ui_track_state.mode_tap_ms[trigger->target_mode];
        const uint8_t is_double_tap = ((last_tap != 0U)
                                       && ((now - last_tap) <= UI_HALL_MODE_DOUBLE_TAP_MS)) ? 1U : 0U;

        g_ui_track_state.mode_tap_ms[trigger->target_mode] = now;
        ui_core_activate_hall_mode_trigger(trigger, is_double_tap);
        return;
    }

}

static void ui_core_handle_track_hall_action(uint8_t hall)
{
    if (hall >= HALL_KEY_COUNT)
    {
        return;
    }

    g_ui_track_state.hall_note_suppressed[hall] = 1U;

    if (hall >= UI_TRACK_COUNT)
    {
        return;
    }

    const uint32_t now = HAL_GetTick();
    const uint32_t last_tap = g_ui_track_state.cfg_tap_ms[hall];
    const uint8_t is_double_tap = ((last_tap != 0U)
                                   && ((now - last_tap) <= UI_HALL_MODE_DOUBLE_TAP_MS)) ? 1U : 0U;
    g_ui_track_state.cfg_tap_ms[hall] = now;

    ui_core_set_active_track(hall);
    if (is_double_tap != 0U)
    {
        ui_page_set(UI_PAGE_TEMPLATE_CFG);
    }
}

static void ui_core_handle_track_selection_event(const ui_event_t *ev)
{
    if (ev == 0)
    {
        return;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS) && (ev->id == (uint8_t)BTN_SHIFT))
    {
        g_ui_track_state.shift_down = 1U;
        return;
    }

    if ((ev->type == UI_EVENT_BUTTON_RELEASE) && (ev->id == (uint8_t)BTN_SHIFT))
    {
        g_ui_track_state.shift_down = 0U;
        return;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS) && (ev->id == (uint8_t)UI_TRACK_MOD_BUTTON))
    {
        g_ui_track_state.track_select_armed = 1U;
        return;
    }

    if ((ev->type == UI_EVENT_BUTTON_RELEASE) && (ev->id == (uint8_t)UI_TRACK_MOD_BUTTON))
    {
        g_ui_track_state.track_select_armed = 0U;
        return;
    }
}

/**
 * @brief Point d'entrée ui_core_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_core_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */


static void ui_core_set_feedback(const char *message)
{
    if (message == 0)
    {
        g_ui_track_state.feedback_message[0] = '\0';
        g_ui_track_state.feedback_until_ms = 0U;
        return;
    }

    (void)snprintf(g_ui_track_state.feedback_message,
                   sizeof(g_ui_track_state.feedback_message),
                   "%s",
                   message);
    g_ui_track_state.feedback_until_ms = HAL_GetTick() + UI_FEEDBACK_DURATION_MS;
}

static uint8_t ui_core_collect_held_seq_steps(seq_track_id_t *out_track,
                                              seq_step_id_t *out_steps,
                                              uint8_t max_steps,
                                              uint8_t promote_pending)
{
    return seq_edit_collect_held_steps(out_track, out_steps, max_steps, promote_pending);
}

static uint8_t ui_core_is_track_hall_event_consumed(const ui_event_t *ev)
{
    if ((ev == 0)
        || (g_ui_track_state.track_select_armed == 0U)
        || (g_ui_track_state.hall_mode == UI_HALL_MODE_PATTERN))
    {
        return 0U;
    }

    if ((ev->type != UI_EVENT_HALL_PRESS) && (ev->type != UI_EVENT_HALL_RELEASE))
    {
        return 0U;
    }

    return (ev->id < HALL_KEY_COUNT) ? 1U : 0U;
}

static uint8_t ui_core_handle_transport_event(const ui_event_t *ev)
{
    if (ev == 0)
    {
        return 0U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS) && (ev->id == (uint8_t)BTN_PLAY))
    {
        seq_runtime_toggle_play_stop();
        return 1U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS) && (ev->id == (uint8_t)BTN_REC))
    {
        if (g_ui_track_state.shift_down != 0U)
        {
            ui_page_template_rec_cfg_open_main();
            ui_page_set(UI_PAGE_TEMPLATE_REC_CFG);
            return 1U;
        }

        seq_runtime_rec_toggle_arm();
        return 1U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS)
        && (ev->id == (uint8_t)BTN_TRANSPOSE_DOWN)
        && (g_ui_track_state.shift_down != 0U))
    {
        ui_core_pattern_enter(UI_PATTERN_MODE_RECALL);
        return 1U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS)
        && (ev->id == (uint8_t)BTN_TRANSPOSE_DOWN)
        && (g_ui_track_state.track_select_armed != 0U))
    {
        ui_core_pattern_enter(UI_PATTERN_MODE_STORE);
        return 1U;
    }

    return 0U;
}

uint8_t ui_core_request_undo(void)
{
    const uint8_t ok = undo_v1_restore(0U);
    if (ok != 0U)
    {
        ui_core_set_feedback("UNDO");
    }
    else
    {
        ui_core_set_feedback("UNDO N/A");
    }
    return ok;
}

static uint8_t ui_core_clipboard_collect_params_from_subpage(const ui_template_subpage_t *subpage,
                                                             param_id_t *out_ids,
                                                             uint8_t *io_count)
{
    if ((subpage == 0) || (out_ids == 0) || (io_count == 0))
    {
        return 0U;
    }

    uint8_t count = *io_count;
    for (uint8_t slot = 0U; slot < 4U; ++slot)
    {
        const param_id_t id = subpage->param_bank.params[slot];
        if (id >= PARAM_COUNT)
        {
            continue;
        }

        uint8_t duplicate = 0U;
        for (uint8_t i = 0U; i < count; ++i)
        {
            if (out_ids[i] == id)
            {
                duplicate = 1U;
                break;
            }
        }
        if (duplicate != 0U)
        {
            continue;
        }

        if (count >= (uint8_t)PARAM_COUNT)
        {
            return 0U;
        }
        out_ids[count++] = id;
    }

    *io_count = count;
    return 1U;
}

static uint8_t ui_core_clipboard_get_held_param_button(button_id_t *out_button)
{
    if (out_button == 0)
    {
        return 0U;
    }

    for (button_id_t button = BTN_PARAM_1; button <= BTN_PARAM_5; ++button)
    {
        if (button_down(button) != 0U)
        {
            *out_button = button;
            return 1U;
        }
    }

    return 0U;
}

static uint8_t ui_core_clipboard_resolve_template_family_from_button(button_id_t button,
                                                                     ui_template_family_id_t *out_family_id)
{
    if (out_family_id == 0)
    {
        return 0U;
    }

    switch (button)
    {
        case BTN_PARAM_1: *out_family_id = UI_TEMPLATE_FAMILY_COLORS; return 1U;
        case BTN_PARAM_2: *out_family_id = UI_TEMPLATE_FAMILY_TONE; return 1U;
        case BTN_PARAM_3: *out_family_id = UI_TEMPLATE_FAMILY_MOD; return 1U;
        case BTN_PARAM_4: *out_family_id = UI_TEMPLATE_FAMILY_MIX; return 1U;
        case BTN_PARAM_5: *out_family_id = UI_TEMPLATE_FAMILY_PLAY; return 1U;
        default: break;
    }

    return 0U;
}

static uint8_t ui_core_clipboard_collect_ensemble_params(ui_template_family_id_t family_id,
                                                         param_id_t *out_ids,
                                                         uint8_t *out_count)
{
    if ((out_ids == 0) || (out_count == 0))
    {
        return 0U;
    }

    const uint8_t track = ui_get_active_track();
    const ui_track_config_t config = ui_get_track_config(track);
    const ui_template_family_t *family = ui_template_family_resolve(family_id, track, config.family, config.type);
    if (family == 0)
    {
        return 0U;
    }

    uint8_t count = 0U;
    for (uint8_t sp = 0U; sp < 4U; ++sp)
    {
        if (ui_core_clipboard_collect_params_from_subpage(&family->subpages[sp], out_ids, &count) == 0U)
        {
            return 0U;
        }
    }

    *out_count = count;
    return (count > 0U) ? 1U : 0U;
}

static uint8_t ui_core_clipboard_collect_active_page_params(param_id_t *out_ids, uint8_t *out_count)
{
    if ((out_ids == 0) || (out_count == 0))
    {
        return 0U;
    }

    const ui_page_t *const page = ui_page_get();
    if ((page == 0) || (page->context == 0))
    {
        return 0U;
    }

    const ui_template_page_state_t *const state = (const ui_template_page_state_t *)page->context;
    const ui_template_subpage_t *const subpage = ui_template_page_get_active_subpage(state);
    if (subpage == 0)
    {
        return 0U;
    }

    uint8_t count = 0U;
    if (ui_core_clipboard_collect_params_from_subpage(subpage, out_ids, &count) == 0U)
    {
        return 0U;
    }

    *out_count = count;
    return (count > 0U) ? 1U : 0U;
}

static void ui_core_clipboard_apply_param_list(uint8_t track,
                                               const param_id_t *params,
                                               const float *values,
                                               uint8_t count)
{
    param_registry_batch_begin();
    for (uint8_t i = 0U; i < count; ++i)
    {
        (void)param_registry_apply_track_value(params[i], track, values[i]);
    }
    param_registry_batch_end();
}

static void ui_core_clipboard_clear_param_list_to_min(uint8_t track,
                                                      const param_id_t *params,
                                                      uint8_t count)
{
    param_registry_batch_begin();
    for (uint8_t i = 0U; i < count; ++i)
    {
        const param_id_t id = params[i];
        if (id >= PARAM_COUNT)
        {
            continue;
        }

        (void)param_registry_apply_track_value(id, track, param_registry[id].min);
    }
    param_registry_batch_end();
}

static uint8_t ui_core_clipboard_copy_track(uint8_t track)
{
    ui_track_clipboard_t *const cb = &g_ui_clipboard.track;
    memset(cb, 0, sizeof(*cb));

    cb->config = ui_get_track_config(track);
    cb->midi_channel = ui_get_track_midi_channel(track);
    cb->midi_source = ui_get_track_midi_source(track);
    cb->seq_page = seq_model_get_track_page(track);
    cb->seq_length = seq_model_get_track_length(track);
    (void)seq_runtime_get_track_div(track, &cb->seq_div);
    (void)seq_runtime_get_track_quant(track, &cb->seq_quant);
    (void)seq_runtime_get_track_swing(track, &cb->seq_swing);

    uint8_t count = 0U;
    for (uint16_t raw_id = 0U; raw_id < (uint16_t)PARAM_COUNT; ++raw_id)
    {
        const param_id_t id = (param_id_t)raw_id;
        const track_runtime_param_status_t status = track_runtime_get_effective_param_status(track, id);
        if ((status != TRACK_RUNTIME_PARAM_ALLOWED) && (status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
        {
            continue;
        }

        float value = param_registry[id].default_value;
        if (param_registry_get_track_value(id, track, &value) == 0U)
        {
            continue;
        }

        cb->params[count] = id;
        cb->values[count] = value;
        ++count;
    }

    cb->param_count = count;
    cb->valid = 1U;
    return 1U;
}

static uint8_t ui_core_clipboard_clear_track(uint8_t track)
{
    for (uint16_t raw_id = 0U; raw_id < (uint16_t)PARAM_COUNT; ++raw_id)
    {
        const param_id_t id = (param_id_t)raw_id;
        (void)param_registry_apply_track_value(id, track, param_registry[id].default_value);
    }

    for (seq_step_id_t step = 0U; step < (seq_step_id_t)SEQ_MAX_STEPS; ++step)
    {
        seq_model_set_trig(track, step, 0U);
        seq_model_step_plock_clear(track, step);
    }

    seq_model_set_track_page(track, 0U);
    seq_model_set_track_length(track, (uint8_t)SEQ_MAX_STEPS);
    seq_runtime_set_track_div(track, 1U);
    seq_runtime_set_track_quant(track, 1U);
    seq_runtime_set_track_swing(track, 0U);
    (void)seq_runtime_set_playhead_step(track, 0U);

    (void)ui_set_track_midi_channel(track, (uint8_t)((track < 16U) ? (track + 1U) : 16U));
    (void)ui_set_track_midi_source(track, UI_TRACK_MIDI_SRC_ALL);
    (void)ui_set_track_family(track, UI_TRACK_FAMILY_OFF);
    (void)ui_set_track_type(track, UI_TRACK_TYPE_AUDIO);
    return 1U;
}

static uint8_t ui_core_clipboard_paste_track(uint8_t track)
{
    const ui_track_clipboard_t *const cb = &g_ui_clipboard.track;
    if (cb->valid == 0U)
    {
        return 0U;
    }

    if (ui_set_track_family(track, cb->config.family) == false)
    {
        return 0U;
    }
    if (ui_set_track_type(track, cb->config.type) == false)
    {
        return 0U;
    }

    (void)ui_set_track_midi_channel(track, cb->midi_channel);
    (void)ui_set_track_midi_source(track, cb->midi_source);
    seq_model_set_track_page(track, cb->seq_page);
    seq_model_set_track_length(track, cb->seq_length);
    seq_runtime_set_track_div(track, cb->seq_div);
    seq_runtime_set_track_quant(track, cb->seq_quant);
    seq_runtime_set_track_swing(track, cb->seq_swing);

    ui_core_clipboard_apply_param_list(track, cb->params, cb->values, cb->param_count);
    return 1U;
}

static uint8_t ui_core_clipboard_copy_param_scope(ui_param_clipboard_t *clipboard,
                                                  ui_clipboard_scope_t scope,
                                                  const param_id_t *params,
                                                  uint8_t count,
                                                  uint8_t track)
{
    if ((clipboard == 0) || (params == 0) || (count == 0U))
    {
        return 0U;
    }

    memset(clipboard, 0, sizeof(*clipboard));
    clipboard->scope = scope;
    clipboard->source_family = ui_get_track_family(track);
    clipboard->source_type = ui_get_track_type(track);

    for (uint8_t i = 0U; i < count; ++i)
    {
        const param_id_t id = params[i];
        float value = 0.0f;
        if ((id >= PARAM_COUNT) || (param_registry_get_track_value(id, track, &value) == 0U))
        {
            return 0U;
        }

        clipboard->params[i] = id;
        clipboard->values[i] = value;
    }

    clipboard->param_count = count;
    clipboard->valid = 1U;
    return 1U;
}

static uint8_t ui_core_clipboard_signature_matches(const ui_param_clipboard_t *clipboard,
                                                   const param_id_t *target_params,
                                                   uint8_t target_count)
{
    if ((clipboard == 0) || (target_params == 0) || (clipboard->valid == 0U))
    {
        return 0U;
    }

    if (clipboard->param_count != target_count)
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < target_count; ++i)
    {
        if (clipboard->params[i] != target_params[i])
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t ui_core_handle_track_clipboard_event(const ui_event_t *ev)
{
    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS))
    {
        return 0U;
    }

    if ((g_ui_track_state.track_select_armed == 0U)
        || ((ev->id != (uint8_t)BTN_COPY) && (ev->id != (uint8_t)BTN_PASTE)))
    {
        return 0U;
    }

    const uint8_t track = ui_get_active_track();
    if (ev->id == (uint8_t)BTN_COPY)
    {
        if (ui_core_clipboard_copy_track(track) != 0U)
        {
            ui_core_set_feedback("TRACK COPIED");
        }
        return 1U;
    }

    if (g_ui_track_state.shift_down != 0U)
    {
        if (ui_core_clipboard_clear_track(track) != 0U)
        {
            ui_core_set_feedback("TRACK CLEARED");
        }
        return 1U;
    }

    if (ui_core_clipboard_paste_track(track) != 0U)
    {
        ui_core_set_feedback("TRACK PASTED");
    }
    else
    {
        ui_core_set_feedback("TRACK INCOMP");
    }
    return 1U;
}

static uint8_t ui_core_handle_ensemble_clipboard_event(const ui_event_t *ev)
{
    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS))
    {
        return 0U;
    }

    if ((ev->id != (uint8_t)BTN_COPY) && (ev->id != (uint8_t)BTN_PASTE))
    {
        return 0U;
    }

    button_id_t held_button = BTN_COUNT;
    if (ui_core_clipboard_get_held_param_button(&held_button) == 0U)
    {
        return 0U;
    }

    ui_template_family_id_t family_id = UI_TEMPLATE_FAMILY_COUNT;
    if (ui_core_clipboard_resolve_template_family_from_button(held_button, &family_id) == 0U)
    {
        return 0U;
    }

    param_id_t params[PARAM_COUNT];
    uint8_t count = 0U;
    if (ui_core_clipboard_collect_ensemble_params(family_id, params, &count) == 0U)
    {
        ui_core_set_feedback("ENS N/A");
        return 1U;
    }

    const uint8_t track = ui_get_active_track();
    if (ev->id == (uint8_t)BTN_COPY)
    {
        if (ui_core_clipboard_copy_param_scope(&g_ui_clipboard.ensemble,
                                               UI_CLIPBOARD_SCOPE_ENSEMBLE,
                                               params,
                                               count,
                                               track) != 0U)
        {
            ui_core_set_feedback("ENS COPIED");
        }
        return 1U;
    }

    if (g_ui_track_state.shift_down != 0U)
    {
        ui_core_clipboard_clear_param_list_to_min(track, params, count);
        ui_core_set_feedback("ENS CLEARED");
        return 1U;
    }

    if (ui_core_clipboard_signature_matches(&g_ui_clipboard.ensemble, params, count) == 0U)
    {
        ui_core_set_feedback("ENS INCOMP");
        return 1U;
    }

    ui_core_clipboard_apply_param_list(track,
                                       g_ui_clipboard.ensemble.params,
                                       g_ui_clipboard.ensemble.values,
                                       g_ui_clipboard.ensemble.param_count);
    ui_core_set_feedback("ENS PASTED");
    return 1U;
}

static uint8_t ui_core_handle_page_clipboard_event(const ui_event_t *ev)
{
    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS))
    {
        return 0U;
    }

    if ((ev->id != (uint8_t)BTN_COPY) && (ev->id != (uint8_t)BTN_PASTE))
    {
        return 0U;
    }

    param_id_t params[PARAM_COUNT];
    uint8_t count = 0U;
    if (ui_core_clipboard_collect_active_page_params(params, &count) == 0U)
    {
        return 0U;
    }

    const uint8_t track = ui_get_active_track();
    if (ev->id == (uint8_t)BTN_COPY)
    {
        if (ui_core_clipboard_copy_param_scope(&g_ui_clipboard.page,
                                               UI_CLIPBOARD_SCOPE_PAGE,
                                               params,
                                               count,
                                               track) != 0U)
        {
            ui_core_set_feedback("PAGE COPIED");
        }
        return 1U;
    }

    if (g_ui_track_state.shift_down != 0U)
    {
        ui_core_clipboard_clear_param_list_to_min(track, params, count);
        ui_core_set_feedback("PAGE CLEARED");
        return 1U;
    }

    if (ui_core_clipboard_signature_matches(&g_ui_clipboard.page, params, count) == 0U)
    {
        ui_core_set_feedback("PAGE INCOMP");
        return 1U;
    }

    ui_core_clipboard_apply_param_list(track,
                                       g_ui_clipboard.page.params,
                                       g_ui_clipboard.page.values,
                                       g_ui_clipboard.page.param_count);
    ui_core_set_feedback("PAGE PASTED");
    return 1U;
}

static uint8_t ui_core_handle_pattern_mode_event(const ui_event_t *ev)
{
    if ((ev == 0) || (ui_get_hall_mode() != UI_HALL_MODE_PATTERN))
    {
        return 0U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS) && (ev->id == (uint8_t)BTN_TRANSPOSE_DOWN)
        && (((g_ui_track_state.shift_down != 0U) && (g_ui_track_state.pattern_mode == UI_PATTERN_MODE_RECALL))
            || ((g_ui_track_state.track_select_armed != 0U) && (g_ui_track_state.pattern_mode == UI_PATTERN_MODE_STORE))))
    {
        ui_core_pattern_exit_to_previous_mode();
        return 1U;
    }

    if ((ev->type != UI_EVENT_HALL_PRESS) || (ev->id >= HALL_KEY_COUNT))
    {
        return 0U;
    }

    if (g_ui_track_state.pattern_substate == UI_PATTERN_SUBSTATE_BANK_SELECT)
    {
        g_ui_track_state.pattern_selected_bank = ev->id;
        g_ui_track_state.pattern_substate = UI_PATTERN_SUBSTATE_PATTERN_SELECT;
        return 1U;
    }

    if (g_ui_track_state.pattern_mode == UI_PATTERN_MODE_STORE)
    {
        if (pattern_live_capture_to_slot(g_ui_track_state.pattern_selected_bank, ev->id) != 0U)
        {
            ui_core_set_feedback("PAT STORED");
            ui_core_pattern_exit_to_previous_mode();
            return 1U;
        }
    }
    else if (pattern_live_queue_slot(g_ui_track_state.pattern_selected_bank, ev->id) != 0U)
    {
        ui_core_set_feedback("PAT QUEUED");
        ui_core_pattern_exit_to_previous_mode();
        return 1U;
    }

    ui_core_set_feedback("PAT FAIL");
    ui_core_pattern_exit_to_previous_mode();
    return 1U;
}

static uint8_t ui_core_handle_global_shortcuts(const ui_event_t *ev)
{
    if (ev == 0)
    {
        return 0U;
    }

    if (ui_core_handle_track_clipboard_event(ev) != 0U)
    {
        return 1U;
    }

    if (ui_core_handle_ensemble_clipboard_event(ev) != 0U)
    {
        return 1U;
    }

    if (ui_core_handle_page_clipboard_event(ev) != 0U)
    {
        return 1U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS) && (ev->id == (uint8_t)BTN_SETTINGS))
    {
        if (ui_page_settings_is_open() == 0U)
        {
            ui_page_settings_open(ui_page_get_id());
        }
        return 1U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS)
        && (ev->id == (uint8_t)BTN_COPY)
        && (g_ui_track_state.shift_down != 0U))
    {
        (void)ui_core_request_undo();
        return 1U;
    }

    return 0U;
}

static uint8_t ui_core_handle_seq_mode_event(const ui_event_t *ev)
{
    if (ev == 0)
    {
        return 0U;
    }

    if (ui_get_hall_mode() != UI_HALL_MODE_SEQ)
    {
        return 0U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS)
        && ((ev->id == (uint8_t)BTN_COPY) || (ev->id == (uint8_t)BTN_PASTE)))
    {
        seq_step_id_t held_steps[SEQ_STEPS_PER_PAGE];
        seq_track_id_t held_track = 0U;
        const uint8_t held_count = ui_core_collect_held_seq_steps(&held_track,
                                                                  held_steps,
                                                                  (uint8_t)SEQ_STEPS_PER_PAGE,
                                                                  1U);
        if (held_count == 0U)
        {
            return 1U;
        }

        if (ev->id == (uint8_t)BTN_COPY)
        {
            (void)seq_edit_copy_steps(held_track, held_steps, held_count);
            return 1U;
        }

        if (g_ui_track_state.shift_down != 0U)
        {
            seq_edit_clear_steps(held_track, held_steps, held_count);
            return 1U;
        }

        seq_clipboard_paste_result_t paste_result;
        if (seq_edit_paste_steps(held_track, held_steps, held_count, &paste_result) != 0U)
        {
            if (paste_result.trunc != 0U)
            {
                ui_core_set_feedback("PASTE TRUNC");
            }
            else if (paste_result.partial != 0U)
            {
                ui_core_set_feedback("PASTE PARTIAL");
            }
        }

        return 1U;
    }

    if (g_ui_track_state.shift_down != 0U)
    {
        return 0U;
    }

    const uint8_t track = ui_get_active_track();

    if ((ev->type == UI_EVENT_HALL_PRESS) && (ev->id < SEQ_STEPS_PER_PAGE))
    {
        seq_edit_step_press(ui_get_active_track(), ev->id);
        return 1U;
    }

    if ((ev->type == UI_EVENT_HALL_RELEASE) && (ev->id < SEQ_STEPS_PER_PAGE))
    {
        seq_edit_step_release(ui_get_active_track(), ev->id);
        return 1U;
    }

    if (ev->type == UI_EVENT_BUTTON_PRESS)
    {
        if (ev->id == (uint8_t)BTN_TRANSPOSE_UP)
        {
            seq_edit_change_page(track, 1);
            return 1U;
        }

        if (ev->id == (uint8_t)BTN_TRANSPOSE_DOWN)
        {
            seq_edit_change_page(track, -1);
            return 1U;
        }
    }

    return 0U;
}

void ui_core_init(void)
{
    memset(&g_ui_clipboard, 0, sizeof(g_ui_clipboard));
    g_ui_track_state.active_track = 0U;
    g_ui_track_state.shift_down = 0U;
    g_ui_track_state.track_select_armed = 0U;
    g_ui_track_state.hall_mode = UI_HALL_MODE_SEQ;
    ui_core_set_feedback(0);
    for (uint8_t mode = 0U; mode < (uint8_t)UI_HALL_MODE_COUNT; ++mode)
    {
        g_ui_track_state.mode_tap_ms[mode] = 0U;
    }

    for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
    {
        g_ui_track_state.hall_prev_pressed[hall] = 0U;
        g_ui_track_state.hall_note_suppressed[hall] = 0U;
    }

    ui_core_sync_active_track_cfg_params();

    ui_template_family_registry_init();
    ui_page_template_colors_register_families();
    ui_page_template_cfg_register_families();
    ui_page_template_dx7_register_families();
    ui_page_template_mod_register_families();
    ui_page_template_keyboard_register_families();
    ui_page_template_arp_register_families();
    ui_page_template_seq_register_families();
    ui_page_template_mix_register_families();
    ui_page_template_play_register_families();

    ui_page_manager_init();

    /*
     * Register pages once at boot. Registration order defines stable page IDs
     * used by the navigation rule table.
     */
    ui_page_manager_register(&g_ui_page_param_test);
    ui_page_manager_register(&g_ui_page_debug_hall);
    ui_page_manager_register(&g_ui_page_calibration);
    ui_page_manager_register(&g_ui_page_user_calibration);
    ui_page_manager_register(&g_ui_page_template_colors);
    ui_page_manager_register(&g_ui_page_template_cfg);
    ui_page_manager_register(&g_ui_page_template_rec_cfg);
    ui_page_manager_register(&g_ui_page_template_dx7);
    ui_page_manager_register(&g_ui_page_template_mod);
    ui_page_manager_register(&g_ui_page_template_keyboard);
    ui_page_manager_register(&g_ui_page_template_arp);
    ui_page_manager_register(&g_ui_page_template_seq);
    ui_page_manager_register(&g_ui_page_template_mix);
    ui_page_manager_register(&g_ui_page_template_play);
    ui_page_manager_register(&g_ui_page_settings);

    if (hall_calibration_load() != 0U)
    {
        ui_page_set(UI_PAGE_TEMPLATE_COLORS);
    }
    else
    {
        ui_page_set(UI_PAGE_CALIBRATION);
    }
}

void ui_core_service_track_selection_inputs(void)
{
    ui_core_update_shift_state(button_down(BTN_SHIFT));
    ui_core_update_track_modifier_state(button_down(UI_TRACK_MOD_BUTTON));

    for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
    {
        const uint8_t pressed = hall_engine_is_pressed(hall);
        const uint8_t was_pressed = g_ui_track_state.hall_prev_pressed[hall];

        if ((was_pressed == 0U) && (pressed != 0U)
                && (g_ui_track_state.shift_down != 0U)
                && (g_ui_track_state.track_select_armed == 0U))
        {
            ui_core_handle_shift_hall_action(hall);
        }
        else if ((was_pressed == 0U) && (pressed != 0U)
                 && (g_ui_track_state.track_select_armed != 0U))
        {
            ui_core_handle_track_hall_action(hall);
        }

        g_ui_track_state.hall_prev_pressed[hall] = pressed;
    }

    if (((ui_get_hall_mode() == UI_HALL_MODE_KEYBOARD) || (ui_get_hall_mode() == UI_HALL_MODE_ARP)) && (g_ui_track_state.shift_down == 0U))
    {
        if (button_pressed(BTN_TRANSPOSE_UP) != 0U)
        {
            keyboard_runtime_step_octave(1);
        }

        if (button_pressed(BTN_TRANSPOSE_DOWN) != 0U)
        {
            keyboard_runtime_step_octave(-1);
        }
    }
}

/**
 * @brief Point d'entrée ui_core_tick.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_core_tick.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_core_tick(void)
{
    ui_event_t ev;

    for (uint8_t encoder = 0U; encoder < (uint8_t)ENC_COUNT; encoder++)
    {
        const int16_t delta = encoder_consume_delta(encoder);
        if (ui_page_settings_is_open() != 0U)
        {
            ui_page_settings_handle_encoder(encoder, delta);
        }
        else
        {
            ui_param_handle_encoder(encoder, delta);
        }
    }

    ui_event_from_inputs();
    seq_edit_step_hold_update();

    while (ui_event_pop(&ev))
    {
        ui_core_handle_track_selection_event(&ev);

        if (ui_core_is_track_hall_event_consumed(&ev) != 0U)
        {
            continue;
        }

        if (ui_core_handle_transport_event(&ev) != 0U)
        {
            continue;
        }

        if (ui_page_settings_handle_event(&ev) != 0U)
        {
            continue;
        }

        if (ui_core_handle_global_shortcuts(&ev) != 0U)
        {
            continue;
        }

        if (ui_core_handle_pattern_mode_event(&ev) != 0U)
        {
            continue;
        }

        if (ui_core_handle_seq_mode_event(&ev) != 0U)
        {
            continue;
        }

        ui_navigation_handle_event(&ev);

        const ui_page_t *active_page = ui_page_get();
        if ((active_page != 0) && (active_page->handle_event != 0))
        {
            active_page->handle_event(&ev);
        }
    }

    const ui_page_t *active_page = ui_page_get();
    if ((active_page != 0) && (active_page->tick != 0))
    {
        active_page->tick();
    }
}

uint8_t ui_get_active_track(void)
{
    return g_ui_track_state.active_track;
}

bool ui_resolve_filter_target_track(uint8_t *out_track_id)
{
    track_runtime_refresh_track(ui_get_active_track());
    return (track_runtime_resolve_filter_target_track(ui_get_active_track(), out_track_id) != 0U) ? true : false;
}

ui_track_config_t ui_get_track_config(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return ui_core_get_default_track_config();
    }

    return g_ui_track_state.track_configs[track];
}

ui_track_family_t ui_get_track_family(uint8_t track)
{
    return ui_get_track_config(track).family;
}

ui_track_type_t ui_get_track_type(uint8_t track)
{
    return ui_get_track_config(track).type;
}

bool ui_set_track_family(uint8_t track, ui_track_family_t family)
{
    if ((track >= UI_TRACK_COUNT) || ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT))
    {
        return false;
    }

    if (!ui_core_track_family_is_available(track, family))
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_core_sync_active_track_cfg_params();
        }
        return false;
    }

    ui_track_config_t *config = &g_ui_track_state.track_configs[track];

    if (config->family == family)
    {
        if (!ui_track_type_is_valid_for_family(config->family, config->type))
        {
            config->type = ui_core_get_first_available_track_type(config->family, track);
        }

        if (track == g_ui_track_state.active_track)
        {
            ui_core_sync_active_track_cfg_params();
        }
        return true;
    }

    config->family = family;
    if (!ui_track_type_is_available(track, config->family, config->type))
    {
        config->type = ui_core_get_first_available_track_type(config->family, track);
    }

    ui_core_sync_audio_runtime_enables();
    track_runtime_invalidate_all();

    if (track == g_ui_track_state.active_track)
    {
        keyboard_runtime_on_active_track_changed();
        ui_core_sync_active_track_cfg_params();
    }

    return true;
}

bool ui_set_track_type(uint8_t track, ui_track_type_t type)
{
    g_ui_tb3_type_switch_stage = 1U;
    g_ui_tb3_type_switch_track = track;
    g_ui_tb3_type_switch_type = (uint32_t)type;

    if ((track >= UI_TRACK_COUNT) || ((uint8_t)type >= (uint8_t)UI_TRACK_TYPE_COUNT))
    {
        g_ui_tb3_type_switch_stage = 2U;
        return false;
    }

    ui_track_config_t *config = &g_ui_track_state.track_configs[track];
    if (!ui_track_type_is_valid_for_family(config->family, type))
    {
        g_ui_tb3_type_switch_stage = 3U;
        if (track == g_ui_track_state.active_track)
        {
            ui_core_sync_active_track_cfg_params();
        }
        return false;
    }

    if (!ui_track_type_is_available(track, config->family, type))
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_core_sync_active_track_cfg_params();
        }
        return false;
    }

    config->type = type;
    track_runtime_invalidate_all();
    g_ui_tb3_type_switch_stage = 4U;

    if (track == g_ui_track_state.active_track)
    {
        keyboard_runtime_on_active_track_changed();
        g_ui_tb3_type_switch_stage = 5U;
        ui_core_sync_active_track_cfg_params();
        g_ui_tb3_type_switch_stage = 6U;
    }

    g_ui_tb3_type_switch_stage = 7U;
    return true;
}

uint8_t ui_count_tracks_with_family(ui_track_family_t family)
{
    uint8_t count = 0U;

    if ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
    {
        return 0U;
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; track++)
    {
        if (g_ui_track_state.track_configs[track].family == family)
        {
            count++;
        }
    }

    return count;
}

const char *ui_get_track_family_display_name(ui_track_family_t family)
{
    switch (family)
    {
        case UI_TRACK_FAMILY_OFF:
            return "Off";

        case UI_TRACK_FAMILY_INPUT1:
            return "Input1";

        case UI_TRACK_FAMILY_INPUT2:
            return "Input2";

        case UI_TRACK_FAMILY_INPUT3:
            return "Input3";

        case UI_TRACK_FAMILY_INPUT4:
            return "Input4";

        case UI_TRACK_FAMILY_SYNTH:
            return "Synth";
        case UI_TRACK_FAMILY_DRUM:
            return "Drum";

        default:
            return "Track";
    }
}

const char *ui_get_track_family_short_name(ui_track_family_t family)
{
    switch (family)
    {
        case UI_TRACK_FAMILY_OFF:
            return "Off";

        case UI_TRACK_FAMILY_INPUT1:
            return "In1";

        case UI_TRACK_FAMILY_INPUT2:
            return "In2";

        case UI_TRACK_FAMILY_INPUT3:
            return "In3";

        case UI_TRACK_FAMILY_INPUT4:
            return "In4";

        case UI_TRACK_FAMILY_SYNTH:
            return "Syn";
        case UI_TRACK_FAMILY_DRUM:
            return "Drm";

        default:
            return "---";
    }
}

const char *ui_get_track_type_display_name(ui_track_family_t family, ui_track_type_t type)
{
    if (!ui_track_type_is_valid_for_family(family, type))
    {
        return "-";
    }

    switch (type)
    {
        case UI_TRACK_TYPE_AUDIO:
            return "Audio";

        case UI_TRACK_TYPE_HYBRID:
            return "Hybrid";

        case UI_TRACK_TYPE_DX7:
            return "DX7";

        case UI_TRACK_TYPE_MONOB:
            return "MonoB";

        case UI_TRACK_TYPE_TB3:
            return "TB-3";
        case UI_TRACK_TYPE_DRUM_TRX_BD:
            return "TRX BD";
        case UI_TRACK_TYPE_DRUM_TRX_CLAVES:
            return "TRX Claves";
        case UI_TRACK_TYPE_DRUM_TRX_HIHAT:
            return "TRX HiHat";
        case UI_TRACK_TYPE_DRUM_TRX_SNARE:
            return "TRX Snare";
        case UI_TRACK_TYPE_DRUM_FM_KICK:
            return "FM Kick";
        case UI_TRACK_TYPE_DRUM_FM_SNARE:
            return "FM Snare";
        case UI_TRACK_TYPE_DRUM_FM_TOM:
            return "FM Tom";
        case UI_TRACK_TYPE_DRUM_FM_RIMSHOT:
            return "FM Rim";
        case UI_TRACK_TYPE_DRUM_FM_CLAP:
            return "FM Clap";
        case UI_TRACK_TYPE_DRUM_FM_COWBELL:
            return "FM Cow";
        case UI_TRACK_TYPE_DRUM_FM_CYMBAL:
            return "FM Cym";

        default:
            return "-";
    }
}

const char *ui_get_track_type_short_name(ui_track_family_t family, ui_track_type_t type)
{
    if (!ui_track_type_is_valid_for_family(family, type))
    {
        return "---";
    }

    switch (type)
    {
        case UI_TRACK_TYPE_AUDIO:
            return "Aud";

        case UI_TRACK_TYPE_HYBRID:
            return "Hyb";

        case UI_TRACK_TYPE_DX7:
            return "DX7";

        case UI_TRACK_TYPE_MONOB:
            return "MB";

        case UI_TRACK_TYPE_TB3:
            return "TB3";
        case UI_TRACK_TYPE_DRUM_TRX_BD:
            return "TBD";
        case UI_TRACK_TYPE_DRUM_TRX_CLAVES:
            return "TCL";
        case UI_TRACK_TYPE_DRUM_TRX_HIHAT:
            return "THH";
        case UI_TRACK_TYPE_DRUM_TRX_SNARE:
            return "TSN";
        case UI_TRACK_TYPE_DRUM_FM_KICK:
            return "FMK";
        case UI_TRACK_TYPE_DRUM_FM_SNARE:
            return "FMS";
        case UI_TRACK_TYPE_DRUM_FM_TOM:
            return "FMT";
        case UI_TRACK_TYPE_DRUM_FM_RIMSHOT:
            return "FMR";
        case UI_TRACK_TYPE_DRUM_FM_CLAP:
            return "FMC";
        case UI_TRACK_TYPE_DRUM_FM_COWBELL:
            return "FMW";
        case UI_TRACK_TYPE_DRUM_FM_CYMBAL:
            return "FMY";

        default:
            return "---";
    }
}

void ui_get_track_runtime_header_label(uint8_t track, char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    if ((track == g_ui_track_state.active_track)
        && (g_ui_track_state.feedback_message[0] != '\0')
        && ((int32_t)(g_ui_track_state.feedback_until_ms - HAL_GetTick()) > 0))
    {
        (void)snprintf(out, out_len, "%s", g_ui_track_state.feedback_message);
        return;
    }

    const ui_track_config_t config = ui_get_track_config(track);

    if (config.family == UI_TRACK_FAMILY_OFF)
    {
        (void)snprintf(out, out_len, "Off");
        return;
    }

    if (ui_track_family_is_engine(config.family))
    {
        (void)snprintf(out, out_len, "%s", ui_get_track_type_display_name(config.family, config.type));
        return;
    }

    if (config.type == UI_TRACK_TYPE_HYBRID)
    {
        (void)snprintf(out, out_len, "%s %s",
                       ui_get_track_family_short_name(config.family),
                       ui_get_track_type_short_name(config.family, config.type));
        return;
    }

    (void)snprintf(out, out_len, "%s", ui_get_track_family_short_name(config.family));
}

ui_hall_mode_t ui_get_hall_mode(void)
{
    return g_ui_track_state.hall_mode;
}

void ui_set_hall_mode(ui_hall_mode_t mode)
{
    if ((uint8_t)mode >= (uint8_t)UI_HALL_MODE_COUNT)
    {
        return;
    }

    if (g_ui_track_state.hall_mode == mode)
    {
        return;
    }

    if ((g_ui_track_state.hall_mode == UI_HALL_MODE_PATTERN)
        && (mode != UI_HALL_MODE_PATTERN))
    {
        ui_core_pattern_abort_internal();
    }

    keyboard_runtime_on_hall_mode_changed(g_ui_track_state.hall_mode, mode);
    g_ui_track_state.hall_mode = mode;
}

const char *ui_get_hall_mode_short_label(void)
{
    if (g_ui_track_state.track_select_armed != 0U)
    {
        return "TRACK";
    }

    if (g_ui_track_state.hall_mode == UI_HALL_MODE_KEYBOARD)
    {
        return "KBD";
    }

    if (g_ui_track_state.hall_mode == UI_HALL_MODE_ARP)
    {
        return "ARP";
    }

    if (g_ui_track_state.hall_mode == UI_HALL_MODE_PATTERN)
    {
        return "PAT";
    }

    return "SEQ";
}

const char *ui_get_hall_mode_suffix_label(void)
{
    static char label[6];

    if (g_ui_track_state.track_select_armed != 0U)
    {
        return "";
    }

    if (g_ui_track_state.hall_mode == UI_HALL_MODE_SEQ)
    {
        const uint8_t page = seq_edit_get_page(ui_get_active_track());
        (void)snprintf(label, sizeof(label), "P%u", (unsigned int)(page + 1U));
        return label;
    }

    if (g_ui_track_state.hall_mode == UI_HALL_MODE_PATTERN)
    {
        return (g_ui_track_state.pattern_mode == UI_PATTERN_MODE_STORE) ? "STR" : "RCL";
    }

    const int8_t octave_shift = keyboard_runtime_get_octave_shift();
    if (octave_shift == 0)
    {
        return "";
    }

    (void)snprintf(label, sizeof(label), "%+d", (int)octave_shift);
    return label;
}

void ui_get_pattern_stub_state(ui_pattern_stub_state_t *out_state)
{
    if (out_state == 0)
    {
        return;
    }

    uint8_t active_bank = 0U;
    uint8_t active_pattern = 0U;
    uint8_t queued_valid = 0U;
    uint8_t queued_bank = 0U;
    uint8_t queued_pattern = 0U;

    (void)pattern_live_get_active(&active_bank, &active_pattern);
    (void)pattern_live_get_queued(&queued_valid, &queued_bank, &queued_pattern);

    out_state->active_bank = active_bank;
    out_state->active_pattern = active_pattern;
    out_state->queued_valid = queued_valid;
    out_state->queued_bank = queued_bank;
    out_state->queued_pattern = queued_pattern;
    out_state->substate = g_ui_track_state.pattern_substate;
    out_state->selected_bank = g_ui_track_state.pattern_selected_bank;
    out_state->mode = g_ui_track_state.pattern_mode;
}

uint8_t ui_is_track_modifier_held(void)
{
    return g_ui_track_state.track_select_armed;
}

uint8_t ui_core_hall_note_is_suppressed(uint8_t hall)
{
    if (hall >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_ui_track_state.hall_note_suppressed[hall];
}

void ui_core_clear_hall_note_suppression(uint8_t hall)
{
    if (hall >= HALL_KEY_COUNT)
    {
        return;
    }

    g_ui_track_state.hall_note_suppressed[hall] = 0U;
}
