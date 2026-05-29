#include "Storage/kit_v1.h"

#include <stdio.h>
#include <string.h>

#include "Mod/mod_lfo_v1.h"
#include "Storage/kit_sd_bank.h"
#include "Storage/memory_layout.h"

static uint16_t g_kit_v1_current_slot = KIT_V1_INVALID_SLOT;
STORAGE_STATE_SDRAM static KitSaveV1 g_kit_v1_work;

static void kit_v1_copy_text(char *dst, uint32_t dst_size, const char *src)
{
    if ((dst == 0) || (dst_size == 0U))
    {
        return;
    }
    memset(dst, 0, dst_size);
    if (src == 0)
    {
        return;
    }
    (void)snprintf(dst, dst_size, "%s", src);
}

static kit_v1_label_code_t kit_v1_resolve_label_code(ui_track_family_t family,
                                                      ui_track_type_t type)
{
    if (family == UI_TRACK_FAMILY_OFF)
    {
        return KIT_V1_LABEL_OFF;
    }
    if (ui_track_family_is_input(family) != false)
    {
        return KIT_V1_LABEL_IN;
    }

    switch (family)
    {
        case UI_TRACK_FAMILY_SYNTH:
            return (type == UI_TRACK_TYPE_WAVE) ? KIT_V1_LABEL_WV : KIT_V1_LABEL_UNKNOWN;

        case UI_TRACK_FAMILY_SAMPLER:
            switch (type)
            {
                case UI_TRACK_TYPE_RAM: return KIT_V1_LABEL_RM;
                case UI_TRACK_TYPE_STREAM: return KIT_V1_LABEL_ST;
                case UI_TRACK_TYPE_MULTI: return KIT_V1_LABEL_ML;
                case UI_TRACK_TYPE_LOOPER: return KIT_V1_LABEL_LP;
                default: return KIT_V1_LABEL_UNKNOWN;
            }

        case UI_TRACK_FAMILY_DRUM:
            switch (type)
            {
                case UI_TRACK_TYPE_DRUM_TRX_BD:
                case UI_TRACK_TYPE_DRUM_BD_ANALOG:
                    return KIT_V1_LABEL_BD;
                default:
                    return KIT_V1_LABEL_UNKNOWN;
            }

        case UI_TRACK_FAMILY_MASTER:
            return (type == UI_TRACK_TYPE_MASTER_FX) ? KIT_V1_LABEL_FX : KIT_V1_LABEL_UNKNOWN;

        default:
            return KIT_V1_LABEL_UNKNOWN;
    }
}

static void kit_v1_capture_lfo(uint8_t track, uint8_t lfo, kit_v1_lfo_lane_t *out)
{
    float value = 0.0f;
    if (out == 0)
    {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (mod_lfo_v1_get_track_param(track, lfo, MOD_LFO_PARAM_DEST, &value) != 0U)
    {
        out->dest = (uint16_t)value;
    }
    if (mod_lfo_v1_get_track_param(track, lfo, MOD_LFO_PARAM_RATE, &value) != 0U)
    {
        out->rate = value;
    }
    if (mod_lfo_v1_get_track_param(track, lfo, MOD_LFO_PARAM_DEPTH, &value) != 0U)
    {
        out->depth = (uint8_t)value;
    }
    if (mod_lfo_v1_get_track_param(track, lfo, MOD_LFO_PARAM_SHAPE, &value) != 0U)
    {
        out->shape = (uint8_t)value;
    }
    if (mod_lfo_v1_get_track_param(track, lfo, MOD_LFO_PARAM_DELAY, &value) != 0U)
    {
        out->delay = value;
    }
    if (mod_lfo_v1_get_track_param(track, lfo, MOD_LFO_PARAM_TRIG, &value) != 0U)
    {
        out->trig = (uint8_t)value;
    }
    if (mod_lfo_v1_get_track_param(track, lfo, MOD_LFO_PARAM_FADE, &value) != 0U)
    {
        out->fade = value;
    }
    if (mod_lfo_v1_get_track_param(track, lfo, MOD_LFO_PARAM_PHASE_SLEW, &value) != 0U)
    {
        out->phase_slew = value;
    }
}

static void kit_v1_capture_sampler_asset(const track_tone_sound_state_t *tone,
                                          kit_v1_asset_ref_t *out_asset)
{
    if (out_asset == 0)
    {
        return;
    }
    memset(out_asset, 0, sizeof(*out_asset));
    if (tone == 0)
    {
        return;
    }

    const int sample_index = (int)(tone->sample + 0.5f);
    if ((sample_index < 0) || (sample_index >= (int)sample_global_pool_get_active_slot_capacity()))
    {
        return;
    }

    const sample_global_slot_t *const slot = sample_global_pool_get_slot((uint16_t)sample_index);
    if ((slot == 0)
            || (slot->kind == SAMPLE_GLOBAL_KIND_EMPTY)
            || (slot->path[0] == '\0'))
    {
        return;
    }

    out_asset->has_asset = 1U;
    out_asset->kind = (uint8_t)slot->kind;
    out_asset->global_slot = (uint16_t)sample_index;
    out_asset->backend_index = slot->backend_index;
    kit_v1_copy_text(out_asset->path, sizeof(out_asset->path), slot->path);
}

void kit_v1_init(void)
{
    g_kit_v1_current_slot = KIT_V1_INVALID_SLOT;
    memset(&g_kit_v1_work, 0, sizeof(g_kit_v1_work));
    kit_sd_bank_init();
}

kit_v1_result_t kit_v1_capture_current(KitSaveV1 *out_kit)
{
    if (out_kit == 0)
    {
        return KIT_V1_RESULT_INVALID_ARG;
    }

    memset(out_kit, 0, sizeof(*out_kit));
    out_kit->meta.track_count = UI_TRACK_COUNT;
    (void)snprintf(out_kit->meta.name, sizeof(out_kit->meta.name), "KIT");

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const track_sound_state_t *const sound = track_sound_state_get_const(track);
        const track_tone_sound_state_t *const tone = track_tone_sound_state_get_const(track);
        if ((sound == 0) || (tone == 0))
        {
            return KIT_V1_RESULT_CAPTURE_FAIL;
        }

        const ui_track_family_t family = ui_get_track_family(track);
        const ui_track_type_t type = ui_get_track_type(track);
        kit_v1_track_payload_t *const dst = &out_kit->tracks[track];
        kit_v1_track_summary_t *const summary = &out_kit->meta.summary[track];

        dst->family = (uint8_t)family;
        dst->type = (uint8_t)type;
        memcpy(&dst->sound, sound, sizeof(dst->sound));
        memcpy(&dst->tone, tone, sizeof(dst->tone));
        kit_v1_capture_lfo(track, 0U, &dst->lfo[0]);
        kit_v1_capture_lfo(track, 1U, &dst->lfo[1]);
        kit_v1_capture_sampler_asset(tone, &dst->asset);

        summary->family = (uint8_t)family;
        summary->type = (uint8_t)type;
        summary->label_code = (uint8_t)kit_v1_resolve_label_code(family, type);
        summary->off = (family == UI_TRACK_FAMILY_OFF) ? 1U : 0U;
    }

    return KIT_V1_RESULT_OK;
}

kit_v1_result_t kit_v1_save_direct(uint16_t *out_slot)
{
    kit_v1_result_t result = kit_v1_capture_current(&g_kit_v1_work);
    if (result != KIT_V1_RESULT_OK)
    {
        return result;
    }

    uint16_t slot = g_kit_v1_current_slot;
    if ((slot >= KIT_V1_SLOT_COUNT)
            || (kit_sd_bank_get_slot_state(slot) == KIT_SD_SLOT_INVALID))
    {
        slot = kit_sd_bank_find_first_empty_slot();
    }
    if (slot == KIT_V1_INVALID_SLOT)
    {
        return KIT_V1_RESULT_SLOT_FULL;
    }

    (void)snprintf(g_kit_v1_work.meta.name,
                   sizeof(g_kit_v1_work.meta.name),
                   "KIT %03u",
                   (unsigned)slot);

    if (kit_sd_bank_store_slot(slot, &g_kit_v1_work) == 0U)
    {
        return (kit_sd_bank_get_last_error() == KIT_SD_BANK_ERR_GATE_BUSY)
            ? KIT_V1_RESULT_SD_BUSY
            : KIT_V1_RESULT_SD_FAIL;
    }

    g_kit_v1_current_slot = slot;
    if (out_slot != 0)
    {
        *out_slot = slot;
    }
    return KIT_V1_RESULT_OK;
}


kit_v1_result_t kit_v1_rename_slot(uint16_t slot, const char *name)
{
    if ((slot >= KIT_V1_SLOT_COUNT) || (name == 0))
    {
        return KIT_V1_RESULT_INVALID_ARG;
    }
    if (kit_sd_bank_get_slot_state(slot) != KIT_SD_SLOT_VALID)
    {
        return KIT_V1_RESULT_EMPTY;
    }
    if (kit_sd_bank_rename_slot(slot, name) == 0U)
    {
        return (kit_sd_bank_get_last_error() == KIT_SD_BANK_ERR_GATE_BUSY)
            ? KIT_V1_RESULT_SD_BUSY
            : KIT_V1_RESULT_RENAME_FAIL;
    }
    g_kit_v1_current_slot = slot;
    return KIT_V1_RESULT_OK;
}

kit_v1_result_t kit_v1_delete_slot(uint16_t slot, uint16_t *out_next_slot)
{
    if (slot >= KIT_V1_SLOT_COUNT)
    {
        return KIT_V1_RESULT_INVALID_ARG;
    }
    if (kit_sd_bank_get_slot_state(slot) != KIT_SD_SLOT_VALID)
    {
        return KIT_V1_RESULT_EMPTY;
    }
    if (kit_sd_bank_delete_slot(slot) == 0U)
    {
        return (kit_sd_bank_get_last_error() == KIT_SD_BANK_ERR_GATE_BUSY)
            ? KIT_V1_RESULT_SD_BUSY
            : KIT_V1_RESULT_DELETE_FAIL;
    }

    uint16_t next = KIT_V1_INVALID_SLOT;
    for (uint16_t candidate = slot; candidate < KIT_V1_SLOT_COUNT; ++candidate)
    {
        if (kit_sd_bank_get_slot_state(candidate) == KIT_SD_SLOT_VALID)
        {
            next = candidate;
            break;
        }
    }
    if (next == KIT_V1_INVALID_SLOT)
    {
        for (uint16_t candidate = 0U; candidate < slot; ++candidate)
        {
            if (kit_sd_bank_get_slot_state(candidate) == KIT_SD_SLOT_VALID)
            {
                next = candidate;
                break;
            }
        }
    }

    g_kit_v1_current_slot = next;
    if (out_next_slot != 0)
    {
        *out_next_slot = next;
    }
    return KIT_V1_RESULT_OK;
}

void kit_v1_set_current_slot(uint16_t slot)
{
    g_kit_v1_current_slot = (slot < KIT_V1_SLOT_COUNT) ? slot : KIT_V1_INVALID_SLOT;
}

uint16_t kit_v1_get_current_slot(void)
{
    return g_kit_v1_current_slot;
}

const char *kit_v1_result_label(kit_v1_result_t result)
{
    switch (result)
    {
        case KIT_V1_RESULT_OK: return "KIT SAVED";
        case KIT_V1_RESULT_INVALID_ARG: return "ERROR";
        case KIT_V1_RESULT_CAPTURE_FAIL: return "ERROR";
        case KIT_V1_RESULT_SLOT_FULL: return "KIT FULL";
        case KIT_V1_RESULT_SD_BUSY: return "SD BUSY";
        case KIT_V1_RESULT_SD_FAIL: return "ERROR";
        case KIT_V1_RESULT_EMPTY: return "NO KIT";
        case KIT_V1_RESULT_BAD_KIT: return "BAD KIT";
        case KIT_V1_RESULT_APPLY_TODO: return "APPLY TODO";
        case KIT_V1_RESULT_RENAME_TODO: return "REN TODO";
        case KIT_V1_RESULT_DELETE_TODO: return "DEL TODO";
        case KIT_V1_RESULT_RENAME_FAIL: return "RENAME FAIL";
        case KIT_V1_RESULT_DELETE_FAIL: return "DELETE FAIL";
        default: return "ERROR";
    }
}

const char *kit_v1_label_code_short_name(uint8_t label_code)
{
    switch ((kit_v1_label_code_t)label_code)
    {
        case KIT_V1_LABEL_OFF: return "X";
        case KIT_V1_LABEL_WV: return "WV";
        case KIT_V1_LABEL_RM: return "RM";
        case KIT_V1_LABEL_ST: return "ST";
        case KIT_V1_LABEL_ML: return "ML";
        case KIT_V1_LABEL_LP: return "LP";
        case KIT_V1_LABEL_IN: return "IN";
        case KIT_V1_LABEL_FX: return "FX";
        case KIT_V1_LABEL_BD: return "BD";
        case KIT_V1_LABEL_SN: return "SN";
        case KIT_V1_LABEL_HH: return "HH";
        case KIT_V1_LABEL_UNKNOWN:
        default:
            return "??";
    }
}
