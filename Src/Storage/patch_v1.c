#include "Storage/patch_v1.h"

#include <stdio.h>
#include <string.h>

#include "Core/brick6_sampler_runtime.h"
#include "Core/brick6_deluge_runtime.h"
#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "Core/synth_polyphony.h"
#include "Keyboard/keyboard_engine.h"
#include "Param/param_registry.h"
#include "Storage/patch_sd_bank.h"
#include "UI/ui_active_track_sync.h"

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

static uint8_t patch_v1_track_uses_sampler_asset(ui_track_family_t family,
                                                 ui_track_type_t type)
{
    if (family != UI_TRACK_FAMILY_SAMPLER)
    {
        return 0U;
    }

    return ((type == UI_TRACK_TYPE_RAM)
            || (type == UI_TRACK_TYPE_STREAM)
            || (type == UI_TRACK_TYPE_MULTI)) ? 1U : 0U;
}

static uint8_t patch_v1_sampler_asset_kind_matches_type(uint8_t kind,
                                                        ui_track_type_t type)
{
    switch (type)
    {
        case UI_TRACK_TYPE_RAM:
            return (kind == (uint8_t)SAMPLE_GLOBAL_KIND_RAM) ? 1U : 0U;
        case UI_TRACK_TYPE_STREAM:
            return (kind == (uint8_t)SAMPLE_GLOBAL_KIND_STREAM) ? 1U : 0U;
        case UI_TRACK_TYPE_MULTI:
            return (kind == (uint8_t)SAMPLE_GLOBAL_KIND_MULTI) ? 1U : 0U;
        default:
            return 1U;
    }
}

static uint8_t patch_v1_family_type_is_valid(uint8_t family, uint8_t type)
{
    switch ((ui_track_family_t)family)
    {
        case UI_TRACK_FAMILY_OFF:
            return (type == (uint8_t)UI_TRACK_TYPE_AUDIO) ? 1U : 0U;

        case UI_TRACK_FAMILY_SYNTH:
            return ((type == (uint8_t)UI_TRACK_TYPE_PRISM)
                    || (type == (uint8_t)UI_TRACK_TYPE_WAVE)
                    || (type == (uint8_t)UI_TRACK_TYPE_STACK)
                    || (type == (uint8_t)UI_TRACK_TYPE_DELUGE)) ? 1U : 0U;

        case UI_TRACK_FAMILY_SAMPLER:
            return ((type == (uint8_t)UI_TRACK_TYPE_RAM)
                    || (type == (uint8_t)UI_TRACK_TYPE_STREAM)
                    || (type == (uint8_t)UI_TRACK_TYPE_MULTI)) ? 1U : 0U;

        case UI_TRACK_FAMILY_DRUM:
            return ((type == (uint8_t)UI_TRACK_TYPE_DRUM_MD)
                    || (type == (uint8_t)UI_TRACK_TYPE_DRUM_BD_ANALOG)) ? 1U : 0U;

        case UI_TRACK_FAMILY_MIDI:
            return (type == (uint8_t)UI_TRACK_TYPE_MIDI) ? 1U : 0U;

        case UI_TRACK_FAMILY_EXTERNAL:
            return (type == (uint8_t)UI_TRACK_TYPE_EXTERNAL) ? 1U : 0U;

        default:
            return 0U;
    }
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

static patch_v1_result_t patch_v1_capture_payload(uint8_t track,
                                                  patch_v1_track_t *out_track)
{
    if ((track >= UI_TRACK_COUNT) || (out_track == 0))
    {
        return PATCH_V1_RESULT_INVALID_ARG;
    }

    const track_sound_state_t *const sound = track_sound_state_get_const(track);
    const track_tone_sound_state_t *const tone = track_tone_sound_state_get_const(track);
    if ((sound == 0) || (tone == 0))
    {
        return PATCH_V1_RESULT_CAPTURE_FAIL;
    }

    memset(out_track, 0, sizeof(*out_track));
    out_track->family = (uint8_t)ui_get_track_family(track);
    out_track->type = (uint8_t)ui_get_track_type(track);
    out_track->synth_voice_count = (out_track->family == (uint8_t)UI_TRACK_FAMILY_SYNTH)
        ? synth_polyphony_get_voice_count(track) : 1U;
    memcpy(&out_track->sound, sound, sizeof(out_track->sound));
    memcpy(&out_track->tone, tone, sizeof(out_track->tone));
    patch_v1_capture_sampler_asset(tone, &out_track->asset);
    return PATCH_V1_RESULT_OK;
}

void patch_v1_init(void)
{
    g_patch_v1_current_slot = PATCH_V1_INVALID_SLOT;
    patch_sd_bank_init();
}

patch_v1_result_t patch_v1_capture_track(uint8_t track, PatchSaveV1 *out_patch)
{
    if ((track >= UI_TRACK_COUNT) || (out_patch == 0)
            || (track_topology_is_play(track) == 0U))
    {
        return PATCH_V1_RESULT_INVALID_ARG;
    }

    memset(out_patch, 0, sizeof(*out_patch));
    out_patch->meta.family = (uint8_t)ui_get_track_family(track);
    out_patch->meta.type = (uint8_t)ui_get_track_type(track);
    out_patch->meta.source_track = track;
    out_patch->meta.summary_family = out_patch->meta.family;
    out_patch->meta.summary_type = out_patch->meta.type;
    out_patch->meta.topology_role = (uint8_t)TRACK_TOPOLOGY_ROLE_PLAY;
    (void)snprintf(out_patch->meta.name,
                   sizeof(out_patch->meta.name),
                   "T%02u %s",
                   (unsigned)(track + 1U),
                   ui_get_track_family_short_name((ui_track_family_t)out_patch->meta.family));
    return patch_v1_capture_payload(track, &out_patch->track);
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

static patch_v1_result_t patch_v1_validate_loaded_patch(PatchSaveV1 *patch)
{
    if ((patch == 0)
            || (patch_v1_family_type_is_valid(patch->meta.family, patch->meta.type) == 0U)
            || (patch->meta.topology_role != (uint8_t)TRACK_TOPOLOGY_ROLE_PLAY)
            || (patch_v1_family_type_is_valid(patch->track.family, patch->track.type) == 0U)
            || (patch->meta.family != patch->track.family)
            || (patch->meta.type != patch->track.type))
    {
        return PATCH_V1_RESULT_BAD_PATCH;
    }

    if (patch_v1_track_uses_sampler_asset((ui_track_family_t)patch->track.family,
                                          (ui_track_type_t)patch->track.type) == 0U)
    {
        memset(&patch->track.asset, 0, sizeof(patch->track.asset));
        return PATCH_V1_RESULT_OK;
    }

    uint16_t resolved_asset_slot = patch->track.asset.global_slot;
    if ((patch->track.asset.has_asset != 0U)
            && (patch_v1_sampler_asset_kind_matches_type(patch->track.asset.kind,
                                                         (ui_track_type_t)patch->track.type) == 0U))
    {
        return PATCH_V1_RESULT_BAD_PATCH;
    }
    if (patch_v1_resolve_loaded_asset(&patch->track.asset, &resolved_asset_slot) == 0U)
    {
        return PATCH_V1_RESULT_ASSET_MISS;
    }
    if (patch->track.asset.has_asset != 0U)
    {
        patch->track.tone.sample = (float)resolved_asset_slot;
    }
    return PATCH_V1_RESULT_OK;
}

static patch_v1_result_t patch_v1_apply_loaded_patch(const PatchSaveV1 *patch, uint8_t target)
{
    if ((patch == 0) || (target >= UI_TRACK_COUNT) || (track_topology_is_play(target) == 0U))
    {
        return PATCH_V1_RESULT_INVALID_ARG;
    }

    uint8_t applied_voice_count = 0U;
    uint8_t voice_limited = 0U;
    uint8_t available = synth_polyphony_get_free_count();
    if (synth_polyphony_get_track_active(target) != 0U)
    {
        available = (uint8_t)(available + synth_polyphony_get_voice_count(target));
    }
    if ((patch->track.family == (uint8_t)UI_TRACK_FAMILY_SYNTH)
            || (patch->track.family == (uint8_t)UI_TRACK_FAMILY_DRUM))
    {
        if (available == 0U) return PATCH_V1_RESULT_VOICE_MAX;
        applied_voice_count = 1U;
        available--;
    }
    if (patch->track.family == (uint8_t)UI_TRACK_FAMILY_SYNTH)
    {
        uint8_t requested = patch->track.synth_voice_count;
        if (requested < 1U) requested = 1U;
        if (requested > SYNTH_POLYPHONY_MAX_VOICES) requested = SYNTH_POLYPHONY_MAX_VOICES;
        while ((applied_voice_count < requested) && (available > 0U))
        {
            applied_voice_count++;
            available--;
        }
        if (applied_voice_count < requested) voice_limited = 1U;
    }

    uint8_t family[UI_TRACK_COUNT];
    uint8_t type[UI_TRACK_COUNT];
    uint8_t midi_channel[UI_TRACK_COUNT];
    uint8_t midi_source[UI_TRACK_COUNT];
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        family[track] = (uint8_t)ui_get_track_family(track);
        type[track] = (uint8_t)ui_get_track_type(track);
        midi_channel[track] = ui_get_track_midi_channel(track);
        midi_source[track] = (uint8_t)ui_get_track_midi_source(track);
    }
    family[target] = patch->track.family;
    type[target] = patch->track.type;

    keyboard_engine_all_notes_off_for_track(target);
    brick6_sampler_runtime_reset_track(target);
    synth_polyphony_reset_track(target);
    if (ui_apply_track_config_bulk_mutation(family, type, midi_channel, midi_source) == false)
    {
        return PATCH_V1_RESULT_APPLY_FAIL;
    }
    track_runtime_invalidate_all();
    track_runtime_refresh_all();
    if (patch->track.family == (uint8_t)UI_TRACK_FAMILY_SYNTH)
    {
        (void)synth_polyphony_set_voice_count(target, applied_voice_count);
    }

    track_sound_state_t *const dst_sound = track_sound_state_get(target);
    track_tone_sound_state_t *const dst_tone = track_tone_sound_state_get(target);
    if ((dst_sound == 0) || (dst_tone == 0)) return PATCH_V1_RESULT_APPLY_FAIL;
    memcpy(dst_sound, &patch->track.sound, sizeof(*dst_sound));
    memcpy(dst_tone, &patch->track.tone, sizeof(*dst_tone));
    patch_v1_reapply_track_params(target);
    track_runtime_refresh_track(target);
    ui_active_track_sync_full_after_reconfigure();
    return (voice_limited != 0U) ? PATCH_V1_RESULT_VOICE_LIMITED : PATCH_V1_RESULT_OK;
}
patch_v1_result_t patch_v1_apply_slot_to_track(uint16_t slot, uint8_t target_track)
{
    if ((slot >= PATCH_V1_SLOT_COUNT) || (target_track >= UI_TRACK_COUNT)
            || (track_topology_is_play(target_track) == 0U))
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

    patch_v1_result_t result = patch_v1_validate_loaded_patch(&patch);
    if ((result != PATCH_V1_RESULT_OK) && (result != PATCH_V1_RESULT_VOICE_LIMITED))
    {
        return result;
    }

    result = patch_v1_apply_loaded_patch(&patch, target_track);
    if ((result != PATCH_V1_RESULT_OK) && (result != PATCH_V1_RESULT_VOICE_LIMITED))
    {
        return result;
    }
    g_patch_v1_current_slot = slot;

    return result;
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
        case PATCH_V1_RESULT_VOICE_LIMITED: return "VOICE LIMITED";
        case PATCH_V1_RESULT_VOICE_MAX: return "VOICE MAX";
        case PATCH_V1_RESULT_RENAME_FAIL: return "RENAME FAIL";
        case PATCH_V1_RESULT_DELETE_FAIL: return "DELETE FAIL";
        default: return "ERROR";
    }
}
