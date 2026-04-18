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
#include "ui_active_track_sync.h"
#include "ui_core_clipboard.h"
#include "ui_core_feedback.h"
#include "ui_core_mute.h"
#include "ui_core_pattern.h"
#include "ui_core_seq_transport.h"
#include "ui_core_shortcuts.h"
#include "ui_edit_context_sync.h"
#include "ui_page_manager.h"
#include "ui_param.h"
#include "ui_system_sync_internal.h"
#include "ui_template_page.h"
#include "App/Hall/hall_engine.h"
#include "Keyboard/keyboard_runtime.h"
#include "param_registry.h"
#include "audio_float.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_runtime.h"
#include "Core/track_runtime.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/undo_v1.h"
#include "Core/brick6_master_buffer.h"

#define UI_HALL_KEYBOARD_MODE_TRIGGER 8U
#define UI_HALL_ARP_MODE_TRIGGER 9U
#define UI_HALL_SEQ_MODE_TRIGGER 10U
#define UI_HALL_MODE_DOUBLE_TAP_MS 400U
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
} ui_track_state_t;

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
        { UI_TRACK_FAMILY_OFF, UI_TRACK_TYPE_AUDIO },
        { UI_TRACK_FAMILY_OFF, UI_TRACK_TYPE_AUDIO },
        { UI_TRACK_FAMILY_OFF, UI_TRACK_TYPE_AUDIO },
        { UI_TRACK_FAMILY_OFF, UI_TRACK_TYPE_AUDIO },
        { UI_TRACK_FAMILY_OFF, UI_TRACK_TYPE_AUDIO },
        { UI_TRACK_FAMILY_OFF, UI_TRACK_TYPE_AUDIO },
    },
    .track_midi_channel = {
        1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U,
        9U, 10U, 11U, 12U, 13U, 14U
    },
    .track_midi_source = {
        (uint8_t)UI_TRACK_MIDI_SRC_ALL, (uint8_t)UI_TRACK_MIDI_SRC_ALL,
        (uint8_t)UI_TRACK_MIDI_SRC_ALL, (uint8_t)UI_TRACK_MIDI_SRC_ALL,
        (uint8_t)UI_TRACK_MIDI_SRC_ALL, (uint8_t)UI_TRACK_MIDI_SRC_ALL,
        (uint8_t)UI_TRACK_MIDI_SRC_ALL, (uint8_t)UI_TRACK_MIDI_SRC_ALL,
        (uint8_t)UI_TRACK_MIDI_SRC_ALL, (uint8_t)UI_TRACK_MIDI_SRC_ALL,
        (uint8_t)UI_TRACK_MIDI_SRC_ALL, (uint8_t)UI_TRACK_MIDI_SRC_ALL,
        (uint8_t)UI_TRACK_MIDI_SRC_ALL, (uint8_t)UI_TRACK_MIDI_SRC_ALL
    },
    .hall_prev_pressed = { 0U },
    .hall_note_suppressed = { 0U },
};



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

static void ui_core_mute_suppress_hall_note(uint8_t hall)
{
    if (hall < HALL_KEY_COUNT)
    {
        g_ui_track_state.hall_note_suppressed[hall] = 1U;
    }
}

static uint8_t ui_core_handle_mute_event(const ui_event_t *ev)
{
    return ui_core_mute_handle_event(ev,
                                     &g_ui_track_state.shift_down,
                                     g_ui_track_state.track_select_armed,
                                     ui_get_hall_mode,
                                     ui_set_hall_mode,
                                     ui_core_mute_suppress_hall_note);
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
    static const ui_track_type_t k_synth_types[] = { UI_TRACK_TYPE_DX7, UI_TRACK_TYPE_MONOB, UI_TRACK_TYPE_SAMPLER };
    static const ui_track_type_t k_master_types[] = { UI_TRACK_TYPE_BUFFER };
    static const ui_track_type_t k_midi_types[] = { UI_TRACK_TYPE_MIDI };
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

        case UI_TRACK_FAMILY_MASTER:
            *out_count = (uint8_t)(sizeof(k_master_types) / sizeof(k_master_types[0]));
            return k_master_types;

        case UI_TRACK_FAMILY_MIDI:
            *out_count = (uint8_t)(sizeof(k_midi_types) / sizeof(k_midi_types[0]));
            return k_midi_types;

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

static uint8_t ui_core_track_uses_master_type(uint8_t track, ui_track_type_t type)
{
    if (track >= UI_TRACK_COUNT)
    {
        return 0U;
    }

    const ui_track_config_t *const config = &g_ui_track_state.track_configs[track];
    return (uint8_t)((config->family == UI_TRACK_FAMILY_MASTER) && (config->type == type));
}

bool ui_track_type_is_available(uint8_t track, ui_track_family_t family, ui_track_type_t type)
{
    if ((track >= UI_TRACK_COUNT) || !ui_track_type_is_valid_for_family(family, type))
    {
        return false;
    }

    if ((family != UI_TRACK_FAMILY_SYNTH) && (family != UI_TRACK_FAMILY_MASTER))
    {
        return true;
    }

    if ((family == UI_TRACK_FAMILY_SYNTH) && (type != UI_TRACK_TYPE_DX7))
    {
        return true;
    }

    if ((family == UI_TRACK_FAMILY_MASTER) && (type != UI_TRACK_TYPE_BUFFER))
    {
        return true;
    }

    for (uint8_t other_track = 0U; other_track < UI_TRACK_COUNT; ++other_track)
    {
        if (other_track == track)
        {
            continue;
        }

        if ((family == UI_TRACK_FAMILY_SYNTH)
                && (ui_core_track_uses_synth_type(other_track, type) != 0U))
        {
            return false;
        }

        if ((family == UI_TRACK_FAMILY_MASTER)
                && (ui_core_track_uses_master_type(other_track, type) != 0U))
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

    if (family == UI_TRACK_FAMILY_MASTER)
    {
        return (uint8_t)((ui_count_tracks_with_family(family) == 0U) ? 1U : 0U);
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

static void ui_core_sync_active_track_ui_context(uint8_t include_keyboard_focus_sync)
{
    ui_active_track_sync_mirror();
    ui_edit_context_sync_active_track(include_keyboard_focus_sync);
}

static uint8_t ui_core_select_active_track(uint8_t track)
{
    if ((track >= UI_TRACK_COUNT) || (g_ui_track_state.active_track == track))
    {
        return 0U;
    }

    g_ui_track_state.active_track = track;
    return 1U;
}

static void ui_core_sync_adapter_notify_keyboard_active_track_changed(void)
{
    keyboard_runtime_sync_track_focus_context();
}

static void ui_core_sync_adapter_invalidate_runtime_all(void)
{
    track_runtime_invalidate_all();
}

static const ui_system_sync_adapter_t g_ui_core_system_sync_adapter = {
    .notify_keyboard_active_track_changed = ui_core_sync_adapter_notify_keyboard_active_track_changed,
    .invalidate_runtime_all = ui_core_sync_adapter_invalidate_runtime_all,
    .sync_audio_runtime_enables = ui_core_sync_audio_runtime_enables
};

static void ui_core_reconfigure_track_runtime(const ui_system_sync_request_t *request,
                                              uint8_t sync_active_track_ui_context)
{
    if (request == 0)
    {
        return;
    }

    ui_system_sync_apply_track_context_change(request, &g_ui_core_system_sync_adapter);
    ui_active_track_sync_after_track_structure_change(sync_active_track_ui_context);
}

static void ui_core_set_active_track(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return;
    }

    if (g_ui_track_state.active_track == track)
    {
        ui_core_sync_active_track_ui_context(0U);
        return;
    }

    (void)ui_core_select_active_track(track);
    ui_core_sync_active_track_ui_context(1U);
}

static void ui_core_post_restore_global_sync(void)
{
    const ui_system_sync_request_t request = ui_system_sync_make_request_restore_bulk();
    ui_core_reconfigure_track_runtime(&request, 1U);
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
        ui_active_track_sync_mirror_cfg_midi_channel();
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
        ui_active_track_sync_mirror_cfg_midi_source();
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
    uint8_t master_family_count = 0U;

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

        if (fam == UI_TRACK_FAMILY_MASTER)
        {
            master_family_count++;
            if (master_family_count > 1U)
            {
                return false;
            }
        }

    }

    /*
     * Compat: historical snapshots may contain several DX7 tracks.
     * Current runtime allows at most one instance.
     * Preserve the first requested slot and gracefully fold extras to MONOB
     * instead of rejecting the full snapshot load.
     */
    uint8_t dx7_kept = 0U;

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

    ui_core_post_restore_global_sync();
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

typedef enum
{
    UI_HALL_DIRECT_ACTION_NONE = 0,
    UI_HALL_DIRECT_ACTION_SHIFT_MODE,
    UI_HALL_DIRECT_ACTION_TRACK_SELECT
} ui_hall_direct_action_t;

static ui_hall_direct_action_t ui_core_resolve_hall_direct_action(uint8_t was_pressed, uint8_t pressed)
{
    if ((was_pressed == 0U) && (pressed != 0U))
    {
        /*
         * Priority contract: SHIFT+HALL wins over TRACK_MOD+HALL.
         * Keep this ordering stable to preserve user-visible behavior.
         */
        if ((g_ui_track_state.shift_down != 0U) && (g_ui_track_state.track_select_armed == 0U))
        {
            return UI_HALL_DIRECT_ACTION_SHIFT_MODE;
        }

        if (g_ui_track_state.track_select_armed != 0U)
        {
            return UI_HALL_DIRECT_ACTION_TRACK_SELECT;
        }
    }

    return UI_HALL_DIRECT_ACTION_NONE;
}

static void ui_core_handle_track_selection_event(const ui_event_t *ev)
{
    /*
     * Modifier-mirror contract:
     * - This queued path mirrors SHIFT/TRACK_MOD button events.
     * - It intentionally coexists with out-of-queue mirror updates in
     *   ui_core_service_track_selection_inputs() so both direct hall actions
     *   and queued handlers observe fresh modifier state.
     */
    if (ev == 0)
    {
        return;
    }

    if (ui_core_mute_is_active() != 0U)
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
    ui_core_feedback_set(message, HAL_GetTick());
}

static void ui_core_transport_enter_pattern(ui_pattern_mode_t mode)
{
    ui_core_pattern_enter(mode, ui_get_hall_mode(), ui_set_hall_mode);
}

static uint8_t ui_core_is_track_hall_event_consumed(const ui_event_t *ev)
{
    if ((ev == 0)
        || (ui_core_mute_is_active() != 0U)
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

static uint8_t ui_core_handle_master_buffer_routing_event(const ui_event_t *ev)
{
    const uint8_t active_track = ui_get_active_track();
    const uint8_t is_master_buffer = ((ui_get_track_family(active_track) == UI_TRACK_FAMILY_MASTER)
                                      && (ui_get_track_type(active_track) == UI_TRACK_TYPE_BUFFER))
                                     ? 1U
                                     : 0U;

    if ((ev == 0)
            || (is_master_buffer == 0U)
            || (ui_get_hall_mode() != UI_HALL_MODE_ARP)
            || (g_ui_track_state.track_select_armed != 0U)
            || (ev->type != UI_EVENT_HALL_PRESS)
            || (ev->id >= UI_TRACK_COUNT))
    {
        return 0U;
    }

    const uint8_t hall = (uint8_t)ev->id;
    const uint8_t enabled = brick6_master_buffer_get_source_enabled(hall);
    brick6_master_buffer_set_source_enabled(hall, (enabled == 0U) ? 1U : 0U);
    g_ui_track_state.hall_note_suppressed[hall] = 1U;
    return 1U;
}

static uint8_t ui_core_handle_transport_event(const ui_event_t *ev)
{
    return ui_core_seq_transport_handle_transport_event(ev,
                                                        ui_core_mute_is_active(),
                                                        g_ui_track_state.shift_down,
                                                        g_ui_track_state.track_select_armed,
                                                        ui_core_transport_enter_pattern,
                                                        ui_core_set_feedback);
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

static uint8_t ui_core_handle_pattern_mode_event(const ui_event_t *ev)
{
    return ui_core_pattern_handle_mode_event(ev,
                                             ui_get_hall_mode(),
                                             g_ui_track_state.shift_down,
                                             g_ui_track_state.track_select_armed,
                                             ui_set_hall_mode,
                                             ui_core_set_feedback);
}

static uint8_t ui_core_handle_global_shortcuts(const ui_event_t *ev)
{
    return ui_core_shortcuts_handle_global_event(ev,
                                                 g_ui_track_state.shift_down,
                                                 g_ui_track_state.track_select_armed,
                                                 ui_core_mute_is_active(),
                                                 ui_core_request_undo,
                                                 ui_core_set_feedback);
}

static uint8_t ui_core_handle_seq_mode_event(const ui_event_t *ev)
{
    return ui_core_seq_transport_handle_seq_mode_event(ev,
                                                       ui_get_hall_mode(),
                                                       g_ui_track_state.shift_down,
                                                       ui_core_set_feedback);
}

void ui_core_init(void)
{
    ui_core_clipboard_init();
    ui_core_feedback_init();
    ui_core_pattern_init();
    g_ui_track_state.active_track = 0U;
    g_ui_track_state.shift_down = 0U;
    g_ui_track_state.track_select_armed = 0U;
    g_ui_track_state.hall_mode = UI_HALL_MODE_SEQ;
    ui_core_mute_init();
    ui_core_set_feedback(0);
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        g_ui_track_state.track_configs[track].family = UI_TRACK_FAMILY_OFF;
        g_ui_track_state.track_configs[track].type = UI_TRACK_TYPE_AUDIO;
        g_ui_track_state.track_midi_channel[track] = (uint8_t)((track < 16U) ? (track + 1U) : 16U);
        g_ui_track_state.track_midi_source[track] = (uint8_t)UI_TRACK_MIDI_SRC_ALL;
    }
    for (uint8_t mode = 0U; mode < (uint8_t)UI_HALL_MODE_COUNT; ++mode)
    {
        g_ui_track_state.mode_tap_ms[mode] = 0U;
    }

    for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
    {
        g_ui_track_state.hall_prev_pressed[hall] = 0U;
        g_ui_track_state.hall_note_suppressed[hall] = 0U;
    }

    ui_active_track_sync_mirror();

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
    ui_page_template_vca_register_families();

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
    ui_page_manager_register(&g_ui_page_template_vca);
    ui_page_manager_register(&g_ui_page_settings);

    ui_page_set(UI_PAGE_CALIBRATION);
}

void ui_core_service_track_selection_inputs(void)
{
    /*
     * Out-of-queue contract:
     * - Runs in superloop before hall_keyboard_bridge_process() and before ui event
     *   queue dispatch in ui_core_tick().
     * - Owns direct edge-triggered hall actions for:
     *   1) SHIFT+HALL mode triggers (hall_mode/page)
     *   2) TRACK_MOD+HALL active-track selection
     * - Keeps modifier mirrors (shift_down / track_select_armed) coherent with raw
     *   button state, so downstream queued handlers read fresh flags.
     */
    if (ui_core_mute_is_active() != 0U)
    {
        /* While mute is active, this path is fully suspended. */
        return;
    }

    ui_core_update_shift_state(button_down(BTN_SHIFT));
    ui_core_update_track_modifier_state(button_down(UI_TRACK_MOD_BUTTON));

    for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)
    {
        const uint8_t pressed = hall_engine_is_pressed(hall);
        const uint8_t was_pressed = g_ui_track_state.hall_prev_pressed[hall];
        const ui_hall_direct_action_t action = ui_core_resolve_hall_direct_action(was_pressed, pressed);

        /* Rising-edge only: prevent retrigger while a hall remains held. */
        if (action == UI_HALL_DIRECT_ACTION_SHIFT_MODE)
        {
            ui_core_handle_shift_hall_action(hall);
        }
        else if (action == UI_HALL_DIRECT_ACTION_TRACK_SELECT)
        {
            ui_core_handle_track_hall_action(hall);
        }

        g_ui_track_state.hall_prev_pressed[hall] = pressed;
    }

    if (((ui_get_hall_mode() == UI_HALL_MODE_KEYBOARD)
            || ((ui_get_hall_mode() == UI_HALL_MODE_ARP)
                && ((ui_get_track_family(ui_get_active_track()) != UI_TRACK_FAMILY_MASTER)
                    || (ui_get_track_type(ui_get_active_track()) != UI_TRACK_TYPE_BUFFER))))
        && (g_ui_track_state.shift_down == 0U)
        && (g_ui_track_state.track_select_armed == 0U))
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
    typedef uint8_t (*ui_core_tick_stage_fn_t)(const ui_event_t *ev);
    typedef struct
    {
        ui_core_tick_stage_fn_t handler;
        uint8_t consumes_event;
        uint8_t blocks_downstream;
    } ui_core_tick_stage_t;

    static const ui_core_tick_stage_t k_event_stages[] = {
        { ui_core_handle_mute_event, 1U, 1U },
        { ui_core_is_track_hall_event_consumed, 1U, 1U },
        { ui_core_handle_master_buffer_routing_event, 1U, 1U },
        { ui_core_handle_transport_event, 1U, 1U },
        { ui_page_settings_handle_event, 1U, 1U },
        /* Intentionally before pattern/seq: global shortcuts can fully mask them. */
        { ui_core_handle_global_shortcuts, 1U, 1U },
        { ui_core_handle_pattern_mode_event, 1U, 1U },
        { ui_core_handle_seq_mode_event, 1U, 1U },
    };

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
        /* Must stay first: updates shift/track modifier state consumed by later stages. */
        ui_core_handle_track_selection_event(&ev);

        for (uint8_t stage = 0U; stage < (uint8_t)(sizeof(k_event_stages) / sizeof(k_event_stages[0])); ++stage)
        {
            const ui_core_tick_stage_t *const s = &k_event_stages[stage];
            if ((s->consumes_event != 0U)
                    && (s->blocks_downstream != 0U)
                    && (s->handler(&ev) != 0U))
            {
                goto next_event;
            }
        }

        /*
         * Navigation/page dispatch contract:
         * - navigation may change current page through ui_page_set()
         * - this same event is then dispatched to the page active after navigation
         * - navigation is intentionally non-consuming in this pipeline
         */
        ui_navigation_handle_event(&ev);

        const ui_page_t *dispatch_page = ui_page_get();
        if ((dispatch_page != 0) && (dispatch_page->handle_event != 0))
        {
            dispatch_page->handle_event(&ev);
        }

next_event:
        ;
    }

    const ui_page_t *active_page = ui_page_get();
    if ((active_page != 0) && (active_page->tick != 0))
    {
        /* Page-local periodic work only; track context sync is explicit in dedicated sync APIs. */
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
            ui_core_sync_active_track_ui_context(0U);
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
            ui_core_sync_active_track_ui_context(0U);
        }
        return true;
    }

    if ((family != UI_TRACK_FAMILY_OFF)
            && (ui_core_get_track_type_count_for_family_and_track(family, track) == 0U))
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_core_sync_active_track_ui_context(0U);
        }
        return false;
    }

    config->family = family;
    if (!ui_track_type_is_available(track, config->family, config->type))
    {
        config->type = ui_core_get_first_available_track_type(config->family, track);
    }

    const uint8_t active_track_touched = (track == g_ui_track_state.active_track) ? 1U : 0U;
    const ui_system_sync_request_t request =
        ui_system_sync_make_request_track_family_change(active_track_touched);
    ui_core_reconfigure_track_runtime(&request, active_track_touched);

    return true;
}

bool ui_set_track_type(uint8_t track, ui_track_type_t type)
{
    if ((track >= UI_TRACK_COUNT) || ((uint8_t)type >= (uint8_t)UI_TRACK_TYPE_COUNT))
    {
        return false;
    }

    ui_track_config_t *config = &g_ui_track_state.track_configs[track];
    if (!ui_track_type_is_valid_for_family(config->family, type))
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_core_sync_active_track_ui_context(0U);
        }
        return false;
    }

    if (!ui_track_type_is_available(track, config->family, type))
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_core_sync_active_track_ui_context(0U);
        }
        return false;
    }

    if (config->type == type)
    {
        if (track == g_ui_track_state.active_track)
        {
            ui_core_sync_active_track_ui_context(0U);
        }
        return true;
    }

    config->type = type;
    const uint8_t active_track_touched = (track == g_ui_track_state.active_track) ? 1U : 0U;
    const ui_system_sync_request_t request =
        ui_system_sync_make_request_track_type_change(active_track_touched);
    ui_core_reconfigure_track_runtime(&request, active_track_touched);

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
        case UI_TRACK_FAMILY_MASTER:
            return "Master";
        case UI_TRACK_FAMILY_MIDI:
            return "MIDI";

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
        case UI_TRACK_FAMILY_MASTER:
            return "Mst";
        case UI_TRACK_FAMILY_MIDI:
            return "MID";

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

        case UI_TRACK_TYPE_SAMPLER:
            return "Sampler";

        case UI_TRACK_TYPE_BUFFER:
            return "Buffer";

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
        case UI_TRACK_TYPE_MIDI:
            return "MIDI";

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

        case UI_TRACK_TYPE_SAMPLER:
            return "Smp";

        case UI_TRACK_TYPE_BUFFER:
            return "Buf";

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
        case UI_TRACK_TYPE_MIDI:
            return "MID";

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

    if (ui_core_feedback_try_get_for_track(g_ui_track_state.active_track,
                                           track,
                                           HAL_GetTick(),
                                           out,
                                           out_len) != 0U)
    {
        return;
    }

    const ui_track_config_t config = ui_get_track_config(track);

    if (config.family == UI_TRACK_FAMILY_OFF)
    {
        (void)snprintf(out, out_len, "Off");
        return;
    }

    if ((config.family == UI_TRACK_FAMILY_MASTER) && (config.type == UI_TRACK_TYPE_BUFFER))
    {
        (void)snprintf(out, out_len, "%s", ui_get_track_type_display_name(config.family, config.type));
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
    /*
     * Hall mode contract reference:
     * - This is the single transition authority for hall_mode.
     * - It owns cross-mode side effects/hooks:
     *   1) forced mute clear when leaving MUTE
     *   2) forced pattern abort when leaving PATTERN
     *   3) keyboard runtime transition callback
     *   4) hall_mode state commit
     */
    if ((uint8_t)mode >= (uint8_t)UI_HALL_MODE_COUNT)
    {
        return;
    }

    if (g_ui_track_state.hall_mode == mode)
    {
        return;
    }

    if ((g_ui_track_state.hall_mode == UI_HALL_MODE_MUTE)
        && (mode != UI_HALL_MODE_MUTE))
    {
        ui_core_mute_reset();
    }

    if ((g_ui_track_state.hall_mode == UI_HALL_MODE_PATTERN)
        && (mode != UI_HALL_MODE_PATTERN))
    {
        ui_core_pattern_abort();
    }

    keyboard_runtime_on_hall_mode_changed(g_ui_track_state.hall_mode, mode);
    g_ui_track_state.hall_mode = mode;
}

const char *ui_get_hall_mode_short_label(void)
{
    const uint8_t active_track = ui_get_active_track();

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
        if ((ui_get_track_family(active_track) == UI_TRACK_FAMILY_MASTER)
                && (ui_get_track_type(active_track) == UI_TRACK_TYPE_BUFFER))
        {
            return "ROUT";
        }
        return "ARP";
    }

    if (g_ui_track_state.hall_mode == UI_HALL_MODE_PATTERN)
    {
        return "PAT";
    }

    if (g_ui_track_state.hall_mode == UI_HALL_MODE_MUTE)
    {
        return "MUTE";
    }

    return "SEQ";
}

const char *ui_get_hall_mode_suffix_label(void)
{
    static char label[6];
    const uint8_t active_track = ui_get_active_track();

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
        return (ui_core_pattern_get_mode() == UI_PATTERN_MODE_STORE) ? "STR" : "RCL";
    }

    if (g_ui_track_state.hall_mode == UI_HALL_MODE_MUTE)
    {
        if (ui_core_mute_get_submode() == UI_MUTE_SUBMODE_PREPARE)
        {
            return "PRE";
        }

        if (ui_core_mute_get_submode() == UI_MUTE_SUBMODE_HOLD_QUICK)
        {
            return "HLD";
        }

        return "";
    }

    if ((g_ui_track_state.hall_mode == UI_HALL_MODE_ARP)
            && (ui_get_track_family(active_track) == UI_TRACK_FAMILY_MASTER)
            && (ui_get_track_type(active_track) == UI_TRACK_TYPE_BUFFER))
    {
        return "";
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
    out_state->substate = ui_core_pattern_get_substate();
    out_state->selected_bank = ui_core_pattern_get_selected_bank();
    out_state->mode = ui_core_pattern_get_mode();
}

ui_mute_state_t ui_get_mute_state(void)
{
    return ui_core_mute_get_state();
}

uint8_t ui_get_mute_hall_led(uint8_t hall, ui_mute_hall_led_t *out_led)
{
    return ui_core_mute_get_hall_led(hall, out_led);
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
