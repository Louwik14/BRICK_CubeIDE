#include "ui_active_track_sync.h"

#include "ui_core.h"
#include "ui_edit_context_sync.h"
#include "ui_param.h"
#include "param_store.h"
#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"

#define UI_CFG_TRACK_PARAM ((param_id_t)PARAM_CFG_TRACK)
#define UI_CFG_TRACK_TYPE_PARAM ((param_id_t)PARAM_CFG_TRACK_TYPE)
#define UI_CFG_TRACK_MIDI_CH_PARAM ((param_id_t)PARAM_CFG_MIDI_CH)
#define UI_CFG_TRACK_MIDI_SRC_PARAM ((param_id_t)PARAM_CFG_MIDI_SRC)
#define UI_CFG_GROUP_SPREAD_PARAM ((param_id_t)PARAM_CFG_GROUP_SPREAD)
#define UI_CFG_GROUP_LINK_PARAM ((param_id_t)PARAM_CFG_GROUP_LINK)
#define UI_CFG_START_PARAM ((param_id_t)PARAM_CFG_START)
#define UI_CFG_TEMPO_PARAM ((param_id_t)PARAM_CFG_TEMPO)
#define UI_CFG_SYNC_PARAM ((param_id_t)PARAM_CFG_SYNC)
#define UI_CFG_REC_LEN_PARAM ((param_id_t)PARAM_CFG_REC_LEN)

static float ui_active_track_sync_clock_source_to_ui_value(void)
{
    switch (seq_runtime_get_clock_source())
    {
        case SEQ_CLOCK_SRC_EXTERNAL_MIDI:
            return 1.0f;
        case SEQ_CLOCK_SRC_EXTERNAL_USB:
            return 2.0f;
        case SEQ_CLOCK_SRC_INTERNAL:
        default:
            return 0.0f;
    }
}

void ui_active_track_sync_mirror(void)
{
    const uint8_t active_track = ui_get_active_track();
    const ui_track_config_t active_config = ui_get_track_config(active_track);

    /* Mirror surface: UI store is resynced from explicit runtime projection reads. */
    param_store_set_active(UI_CFG_TRACK_PARAM, (float)active_config.family);
    param_store_set_active(UI_CFG_TRACK_TYPE_PARAM,
                           (float)ui_get_track_type_index_for_family(active_config.family, active_config.type));
    param_store_set_active(UI_CFG_TRACK_MIDI_CH_PARAM, (float)ui_get_track_midi_channel(active_track));
    param_store_set_active(UI_CFG_TRACK_MIDI_SRC_PARAM, (float)ui_get_track_midi_source(active_track));
    {
        uint8_t master_track = active_track;
        if (track_runtime_get_voice_group_effective_master(active_track, &master_track) == 0U)
        {
            master_track = active_track;
        }
        param_store_set_active(UI_CFG_GROUP_SPREAD_PARAM, track_state_get_voice_group_spread(master_track));
        param_store_set_active(UI_CFG_GROUP_LINK_PARAM, (float)track_state_get_voice_group_link(master_track));
    }
    param_store_set_active(UI_CFG_START_PARAM, (float)seq_runtime_get_rec_start_mode());
    param_store_set_active(UI_CFG_TEMPO_PARAM, (float)seq_runtime_get_tempo_bpm_milli() / 1000.0f);
    param_store_set_active(UI_CFG_SYNC_PARAM, ui_active_track_sync_clock_source_to_ui_value());
    param_store_set_active(UI_CFG_REC_LEN_PARAM, (float)seq_runtime_get_rec_len_mode());
}

void ui_active_track_sync_mirror_cfg_midi_channel(void)
{
    param_store_set_active(UI_CFG_TRACK_MIDI_CH_PARAM, (float)ui_get_track_midi_channel(ui_get_active_track()));
}

void ui_active_track_sync_mirror_cfg_midi_source(void)
{
    param_store_set_active(UI_CFG_TRACK_MIDI_SRC_PARAM, (float)ui_get_track_midi_source(ui_get_active_track()));
}

void ui_active_track_sync_full_after_reconfigure(void)
{
    ui_active_track_sync_mirror();
    ui_param_sync_active_track_mirror_from_runtime();
}

void ui_active_track_sync_after_track_structure_change(uint8_t sync_active_track_ui_context)
{
    if (sync_active_track_ui_context == 0U)
    {
        return;
    }

    ui_active_track_sync_full_after_reconfigure();
    ui_edit_context_sync_active_track(sync_active_track_ui_context);
}

void ui_active_track_sync_after_track_creation_from_off(uint8_t sync_active_track_ui_context)
{
    if (sync_active_track_ui_context == 0U)
    {
        return;
    }

    ui_active_track_sync_full_after_reconfigure();
    ui_edit_context_sync_active_track_created_from_off(sync_active_track_ui_context);
}

void ui_active_track_sync_full_after_global_restore(void)
{
    ui_active_track_sync_after_track_structure_change(1U);
}
