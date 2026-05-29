#include "Storage/patch_v1.h"

#include <stdio.h>
#include <string.h>

#include "Core/brick6_sampler_runtime.h"
#include "Core/track_runtime.h"
#include "Keyboard/keyboard_engine.h"
#include "Mod/mod_lfo_v1.h"
#include "Param/param_registry.h"
#include "Storage/patch_sd_bank.h"

static uint16_t g_patch_v1_current_slot = PATCH_V1_INVALID_SLOT;

static void patch_v1_copy_text(char *dst, uint32_t dst_size, const char *src)
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

static void patch_v1_capture_lfo(uint8_t track, uint8_t lfo, patch_v1_lfo_lane_t *out)
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

static void patch_v1_capture_sampler_asset(const track_tone_sound_state_t *tone,
                                           patch_v1_asset_ref_t *out_asset)
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
    patch_v1_copy_text(out_asset->path, sizeof(out_asset->path), slot->path);
}

static uint8_t patch_v1_text_equal(const char *a, const char *b)
{
    if ((a == 0) || (b == 0))
    {
        return 0U;
    }

    for (uint32_t i = 0U; i < PATCH_V1_ASSET_PATH_MAX; ++i)
    {
        if (a[i] != b[i])
        {
            return 0U;
        }
        if (a[i] == '\0')
        {
            return 1U;
        }
    }

    return 1U;
}

static uint8_t patch_v1_resolve_loaded_asset(const patch_v1_asset_ref_t *asset,
                                             uint16_t *out_global_slot)
{
    if ((asset == 0) || (out_global_slot == 0))
    {
        return 0U;
    }
    if ((asset->has_asset == 0U) || (asset->path[0] == '\0'))
    {
        return 1U;
    }

    const uint16_t capacity = sample_global_pool_get_active_slot_capacity();
    if (asset->global_slot < capacity)
    {
        const sample_global_slot_t *const slot = sample_global_pool_get_slot(asset->global_slot);
        if ((slot != 0)
                && (slot->kind == (sample_global_kind_t)asset->kind)
                && (slot->state == SAMPLE_GLOBAL_STATE_READY)
                && (patch_v1_text_equal(slot->path, asset->path) != 0U))
        {
            *out_global_slot = asset->global_slot;
            return 1U;
        }
    }

    for (uint16_t i = 0U; i < capacity; ++i)
    {
        const sample_global_slot_t *const slot = sample_global_pool_get_slot(i);
        if ((slot != 0)
                && (slot->kind == (sample_global_kind_t)asset->kind)
                && (slot->state == SAMPLE_GLOBAL_STATE_READY)
                && (patch_v1_text_equal(slot->path, asset->path) != 0U))
        {
            *out_global_slot = i;
            return 1U;
        }
    }

    return 0U;
}

static void patch_v1_apply_lfo(uint8_t track, uint8_t lfo, const patch_v1_lfo_lane_t *lane)
{
    if ((track >= UI_TRACK_COUNT) || (lfo >= 2U) || (lane == 0))
    {
        return;
    }

    (void)mod_lfo_v1_set_track_param(track, lfo, MOD_LFO_PARAM_DEST, (float)lane->dest);
    (void)mod_lfo_v1_set_track_param(track, lfo, MOD_LFO_PARAM_RATE, lane->rate);
    (void)mod_lfo_v1_set_track_param(track, lfo, MOD_LFO_PARAM_DEPTH, (float)lane->depth);
    (void)mod_lfo_v1_set_track_param(track, lfo, MOD_LFO_PARAM_SHAPE, (float)lane->shape);
    (void)mod_lfo_v1_set_track_param(track, lfo, MOD_LFO_PARAM_DELAY, lane->delay);
    (void)mod_lfo_v1_set_track_param(track, lfo, MOD_LFO_PARAM_TRIG, (float)lane->trig);
    (void)mod_lfo_v1_set_track_param(track, lfo, MOD_LFO_PARAM_FADE, lane->fade);
    (void)mod_lfo_v1_set_track_param(track, lfo, MOD_LFO_PARAM_PHASE_SLEW, lane->phase_slew);
}

static void patch_v1_reapply_track_params(uint8_t track)
{
    param_registry_batch_begin();
    for (uint16_t raw_id = 0U; raw_id < (uint16_t)PARAM_COUNT; ++raw_id)
    {
        const param_id_t id = (param_id_t)raw_id;
        if (track_runtime_get_effective_param_status(track, id) != TRACK_RUNTIME_PARAM_ALLOWED)
        {
            continue;
        }

        float value = 0.0f;
        if (param_registry_get_track_value(id, track, &value) != 0U)
        {
            (void)param_registry_apply_track_value(id, track, value);
        }
    }
    param_registry_batch_end();
}

void patch_v1_init(void)
{
    g_patch_v1_current_slot = PATCH_V1_INVALID_SLOT;
    patch_sd_bank_init();
}

patch_v1_result_t patch_v1_capture_track(uint8_t track, PatchSaveV1 *out_patch)
{
    if ((track >= UI_TRACK_COUNT) || (out_patch == 0))
    {
        return PATCH_V1_RESULT_INVALID_ARG;
    }

    const track_sound_state_t *const sound = track_sound_state_get_const(track);
    const track_tone_sound_state_t *const tone = track_tone_sound_state_get_const(track);
    if ((sound == 0) || (tone == 0))
    {
        return PATCH_V1_RESULT_CAPTURE_FAIL;
    }

    memset(out_patch, 0, sizeof(*out_patch));
    out_patch->meta.family = (uint8_t)ui_get_track_family(track);
    out_patch->meta.type = (uint8_t)ui_get_track_type(track);
    out_patch->meta.source_track = track;
    (void)snprintf(out_patch->meta.name,
                   sizeof(out_patch->meta.name),
                   "T%02u %s",
                   (unsigned)(track + 1U),
                   ui_get_track_family_short_name((ui_track_family_t)out_patch->meta.family));

    memcpy(&out_patch->sound, sound, sizeof(out_patch->sound));
    memcpy(&out_patch->tone, tone, sizeof(out_patch->tone));
    patch_v1_capture_lfo(track, 0U, &out_patch->lfo[0]);
    patch_v1_capture_lfo(track, 1U, &out_patch->lfo[1]);
    patch_v1_capture_sampler_asset(tone, &out_patch->asset);

    return PATCH_V1_RESULT_OK;
}

patch_v1_result_t patch_v1_save_track_direct(uint8_t track, uint16_t *out_slot)
{
    PatchSaveV1 patch;
    patch_v1_result_t result = patch_v1_capture_track(track, &patch);
    if (result != PATCH_V1_RESULT_OK)
    {
        return result;
    }

    uint16_t slot = g_patch_v1_current_slot;
    if ((slot >= PATCH_V1_SLOT_COUNT)
            || (patch_sd_bank_get_slot_state(slot) == PATCH_SD_SLOT_INVALID))
    {
        slot = patch_sd_bank_find_first_empty_slot();
    }
    if (slot == PATCH_V1_INVALID_SLOT)
    {
        return PATCH_V1_RESULT_SLOT_FULL;
    }

    if (patch_sd_bank_store_slot(slot, &patch) == 0U)
    {
        return (patch_sd_bank_get_last_error() == PATCH_SD_BANK_ERR_GATE_BUSY)
            ? PATCH_V1_RESULT_SD_BUSY
            : PATCH_V1_RESULT_SD_FAIL;
    }

    g_patch_v1_current_slot = slot;
    if (out_slot != 0)
    {
        *out_slot = slot;
    }
    return PATCH_V1_RESULT_OK;
}

patch_v1_result_t patch_v1_apply_slot_to_track(uint16_t slot, uint8_t target_track)
{
    if ((slot >= PATCH_V1_SLOT_COUNT) || (target_track >= UI_TRACK_COUNT))
    {
        return PATCH_V1_RESULT_INVALID_ARG;
    }
    if (patch_sd_bank_slot_has_data(slot) == 0U)
    {
        return PATCH_V1_RESULT_EMPTY;
    }

    PatchSaveV1 patch;
    if (patch_sd_bank_load_slot(slot, &patch) == 0U)
    {
        const patch_sd_bank_error_t sd_err = patch_sd_bank_get_last_error();
        if (sd_err == PATCH_SD_BANK_ERR_GATE_BUSY)
        {
            return PATCH_V1_RESULT_SD_BUSY;
        }
        if ((sd_err == PATCH_SD_BANK_ERR_INVALID_HEADER)
                || (sd_err == PATCH_SD_BANK_ERR_CHECKSUM_FAIL))
        {
            return PATCH_V1_RESULT_BAD_PATCH;
        }
        return PATCH_V1_RESULT_SD_FAIL;
    }

    if ((patch.meta.family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
            || (patch.meta.type >= (uint8_t)UI_TRACK_TYPE_COUNT))
    {
        return PATCH_V1_RESULT_BAD_PATCH;
    }

    uint16_t resolved_asset_slot = (uint16_t)patch.asset.global_slot;
    if (patch_v1_resolve_loaded_asset(&patch.asset, &resolved_asset_slot) == 0U)
    {
        return PATCH_V1_RESULT_ASSET_MISS;
    }
    if (patch.asset.has_asset != 0U)
    {
        patch.tone.sample = (float)resolved_asset_slot;
    }

    keyboard_engine_all_notes_off_for_track(target_track);
    brick6_sampler_runtime_reset_track(target_track);

    if (ui_set_track_family(target_track, (ui_track_family_t)patch.meta.family) == false)
    {
        return PATCH_V1_RESULT_APPLY_FAIL;
    }
    if (ui_set_track_type(target_track, (ui_track_type_t)patch.meta.type) == false)
    {
        return PATCH_V1_RESULT_APPLY_FAIL;
    }
    track_runtime_refresh_track(target_track);

    track_sound_state_t *const dst_sound = track_sound_state_get(target_track);
    track_tone_sound_state_t *const dst_tone = track_tone_sound_state_get(target_track);
    if ((dst_sound == 0) || (dst_tone == 0))
    {
        return PATCH_V1_RESULT_APPLY_FAIL;
    }

    memcpy(dst_sound, &patch.sound, sizeof(*dst_sound));
    memcpy(dst_tone, &patch.tone, sizeof(*dst_tone));
    patch_v1_apply_lfo(target_track, 0U, &patch.lfo[0]);
    patch_v1_apply_lfo(target_track, 1U, &patch.lfo[1]);
    patch_v1_reapply_track_params(target_track);
    track_runtime_refresh_track(target_track);
    g_patch_v1_current_slot = slot;

    return PATCH_V1_RESULT_OK;
}

patch_v1_result_t patch_v1_rename_slot(uint16_t slot, const char *name)
{
    if ((slot >= PATCH_V1_SLOT_COUNT) || (name == 0))
    {
        return PATCH_V1_RESULT_INVALID_ARG;
    }
    if (patch_sd_bank_get_slot_state(slot) == PATCH_SD_SLOT_EMPTY)
    {
        return PATCH_V1_RESULT_EMPTY;
    }
    if (patch_sd_bank_get_slot_state(slot) == PATCH_SD_SLOT_INVALID)
    {
        return PATCH_V1_RESULT_BAD_PATCH;
    }
    if (patch_sd_bank_rename_slot(slot, name) == 0U)
    {
        const patch_sd_bank_error_t sd_err = patch_sd_bank_get_last_error();
        if (sd_err == PATCH_SD_BANK_ERR_GATE_BUSY)
        {
            return PATCH_V1_RESULT_SD_BUSY;
        }
        if ((sd_err == PATCH_SD_BANK_ERR_INVALID_HEADER)
                || (sd_err == PATCH_SD_BANK_ERR_CHECKSUM_FAIL))
        {
            return PATCH_V1_RESULT_BAD_PATCH;
        }
        return PATCH_V1_RESULT_RENAME_FAIL;
    }

    g_patch_v1_current_slot = slot;
    return PATCH_V1_RESULT_OK;
}

patch_v1_result_t patch_v1_delete_slot(uint16_t slot, uint16_t *out_next_slot)
{
    if (slot >= PATCH_V1_SLOT_COUNT)
    {
        return PATCH_V1_RESULT_INVALID_ARG;
    }
    if (patch_sd_bank_get_slot_state(slot) == PATCH_SD_SLOT_EMPTY)
    {
        return PATCH_V1_RESULT_EMPTY;
    }
    if (patch_sd_bank_get_slot_state(slot) == PATCH_SD_SLOT_INVALID)
    {
        return PATCH_V1_RESULT_BAD_PATCH;
    }
    if (patch_sd_bank_delete_slot(slot) == 0U)
    {
        const patch_sd_bank_error_t sd_err = patch_sd_bank_get_last_error();
        if (sd_err == PATCH_SD_BANK_ERR_GATE_BUSY)
        {
            return PATCH_V1_RESULT_SD_BUSY;
        }
        return PATCH_V1_RESULT_DELETE_FAIL;
    }

    uint16_t next_slot = slot;
    for (uint16_t offset = 1U; offset < PATCH_V1_SLOT_COUNT; ++offset)
    {
        const uint16_t candidate =
            (uint16_t)((slot + offset) % PATCH_V1_SLOT_COUNT);
        if (patch_sd_bank_get_slot_state(candidate) == PATCH_SD_SLOT_VALID)
        {
            next_slot = candidate;
            break;
        }
    }

    g_patch_v1_current_slot = next_slot;
    if (out_next_slot != 0)
    {
        *out_next_slot = next_slot;
    }
    return PATCH_V1_RESULT_OK;
}

void patch_v1_set_current_slot(uint16_t slot)
{
    if (slot < PATCH_V1_SLOT_COUNT)
    {
        g_patch_v1_current_slot = slot;
    }
}

uint16_t patch_v1_get_current_slot(void)
{
    return g_patch_v1_current_slot;
}

const char *patch_v1_result_label(patch_v1_result_t result)
{
    switch (result)
    {
        case PATCH_V1_RESULT_OK: return "PATCH SAVED";
        case PATCH_V1_RESULT_INVALID_ARG: return "ERROR";
        case PATCH_V1_RESULT_CAPTURE_FAIL: return "ERROR";
        case PATCH_V1_RESULT_SLOT_FULL: return "PATCH FULL";
        case PATCH_V1_RESULT_SD_BUSY: return "SD BUSY";
        case PATCH_V1_RESULT_SD_FAIL: return "ERROR";
        case PATCH_V1_RESULT_EMPTY: return "EMPTY";
        case PATCH_V1_RESULT_BAD_PATCH: return "BAD PATCH";
        case PATCH_V1_RESULT_ASSET_MISS: return "ASSET MISS";
        case PATCH_V1_RESULT_APPLY_FAIL: return "APPLY FAIL";
        case PATCH_V1_RESULT_RENAME_FAIL: return "RENAME FAIL";
        case PATCH_V1_RESULT_DELETE_FAIL: return "DELETE FAIL";
        default: return "ERROR";
    }
}
