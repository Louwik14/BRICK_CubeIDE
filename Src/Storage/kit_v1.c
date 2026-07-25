#include "Storage/kit_v1.h"

#include <stdio.h>
#include <string.h>

#include "Core/brick6_sampler_runtime.h"
#include "Core/track_runtime.h"
#include "Keyboard/keyboard_engine.h"
#include "Param/param_registry.h"
#include "Sampler/multi_sample_pool.h"
#include "Storage/kit_sd_bank.h"
#include "Storage/memory_layout.h"
#include "UI/ui_active_track_sync.h"

static uint16_t g_kit_v1_current_slot = KIT_V1_INVALID_SLOT;
static uint8_t g_kit_v1_dirty;
static uint8_t g_kit_v1_dirty_suspended;
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

static uint8_t kit_v1_track_uses_sampler_asset(ui_track_family_t family,
                                               ui_track_type_t type)
{
    return (uint8_t)((family == UI_TRACK_FAMILY_SAMPLER)
                    && ((type == UI_TRACK_TYPE_RAM)
                        || (type == UI_TRACK_TYPE_STREAM)
                        || (type == UI_TRACK_TYPE_MULTI)));
}

static uint8_t kit_v1_sampler_asset_kind_matches_type(uint8_t kind, ui_track_type_t type)
{
    if (type == UI_TRACK_TYPE_RAM)
    {
        return (kind == (uint8_t)SAMPLE_GLOBAL_KIND_RAM) ? 1U : 0U;
    }
    if (type == UI_TRACK_TYPE_STREAM)
    {
        return (kind == (uint8_t)SAMPLE_GLOBAL_KIND_STREAM) ? 1U : 0U;
    }
    if (type == UI_TRACK_TYPE_MULTI)
    {
        return (kind == (uint8_t)SAMPLE_GLOBAL_KIND_MULTI) ? 1U : 0U;
    }
    return 0U;
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

static uint8_t kit_v1_text_equal(const char *a, const char *b)
{
    if ((a == 0) || (b == 0))
    {
        return 0U;
    }

    for (uint32_t i = 0U; i < KIT_V1_ASSET_PATH_MAX; ++i)
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

static uint8_t kit_v1_resolve_loaded_asset(const kit_v1_asset_ref_t *asset,
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
                && (kit_v1_text_equal(slot->path, asset->path) != 0U))
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
                && (kit_v1_text_equal(slot->path, asset->path) != 0U))
        {
            *out_global_slot = i;
            return 1U;
        }
    }

    return 0U;
}

static uint8_t kit_v1_is_reapply_domain(param_id_t id)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    return (uint8_t)((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_COLORS)
                    || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
                    || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX));
}

static uint8_t kit_v1_multi_selector_for_global_slot(uint16_t global_slot, float *out_selector)
{
    uint16_t backend = MULTI_SAMPLE_POOL_INVALID_ID;
    uint8_t selector = 1U;

    if (out_selector == 0)
    {
        return 0U;
    }
    *out_selector = 0.0f;

    if (sample_global_pool_resolve_backend(global_slot,
                                           SAMPLE_GLOBAL_KIND_MULTI,
                                           &backend) == 0U)
    {
        return 0U;
    }

    for (uint16_t id = 0U; id < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++id)
    {
        if (multi_sample_pool_get_instrument(id) == 0)
        {
            continue;
        }
        if (id == backend)
        {
            *out_selector = (float)selector;
            return 1U;
        }
        selector++;
    }

    return 0U;
}

static uint8_t kit_v1_track_sample_param_should_apply(const kit_v1_track_payload_t *payload)
{
    if (payload == 0)
    {
        return 0U;
    }
    if (kit_v1_track_uses_sampler_asset((ui_track_family_t)payload->family,
                                        (ui_track_type_t)payload->type) == 0U)
    {
        return 1U;
    }
    return (payload->asset.has_asset != 0U) ? 1U : 0U;
}

static uint8_t kit_v1_reapply_track_params_from_payload(uint8_t track,
                                                        const kit_v1_track_payload_t *payload)
{
    uint8_t ok = 1U;

    param_registry_batch_begin();
    for (uint16_t raw_id = 0U; raw_id < (uint16_t)PARAM_COUNT; ++raw_id)
    {
        const param_id_t id = (param_id_t)raw_id;
        if (kit_v1_is_reapply_domain(id) == 0U)
        {
            continue;
        }
        if ((id == PARAM_SAMPLER_SAMPLE)
                && (kit_v1_track_sample_param_should_apply(payload) == 0U))
        {
            continue;
        }
        if (track_runtime_get_effective_param_status(track, id) != TRACK_RUNTIME_PARAM_ALLOWED)
        {
            continue;
        }

        float value = 0.0f;
        if ((id == PARAM_SAMPLER_SAMPLE)
                && (payload != 0)
                && (payload->asset.has_asset != 0U)
                && (payload->asset.kind == (uint8_t)SAMPLE_GLOBAL_KIND_MULTI))
        {
            if (kit_v1_multi_selector_for_global_slot((uint16_t)payload->tone.sample, &value) == 0U)
            {
                ok = 0U;
                continue;
            }
        }
        else if (param_registry_get_track_value(id, track, &value) == 0U)
        {
            continue;
        }

        if (param_registry_apply_track_value(id, track, value) == 0U)
        {
            ok = 0U;
        }
    }
    param_registry_batch_end();

    return ok;
}

static kit_v1_result_t kit_v1_restore_loaded_kit_state(const KitSaveV1 *kit)
{
    if (kit == 0)
    {
        return KIT_V1_RESULT_INVALID_ARG;
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        track_sound_state_t *const dst_sound = track_sound_state_get(track);
        track_tone_sound_state_t *const dst_tone = track_tone_sound_state_get(track);
        const kit_v1_track_payload_t *const src = &kit->tracks[track];
        if ((dst_sound == 0) || (dst_tone == 0))
        {
            return KIT_V1_RESULT_APPLY_FAIL;
        }

        memcpy(dst_sound, &src->sound, sizeof(*dst_sound));
        memcpy(dst_tone, &src->tone, sizeof(*dst_tone));
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        if (kit_v1_reapply_track_params_from_payload(track, &kit->tracks[track]) == 0U)
        {
            return KIT_V1_RESULT_APPLY_FAIL;
        }
    }

    return KIT_V1_RESULT_OK;
}

static kit_v1_result_t kit_v1_validate_loaded_kit(KitSaveV1 *kit)
{
    if (kit == 0)
    {
        return KIT_V1_RESULT_INVALID_ARG;
    }
    if (kit->meta.track_count != UI_TRACK_COUNT)
    {
        return KIT_V1_RESULT_BAD_KIT;
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        kit_v1_track_payload_t *const payload = &kit->tracks[track];
        if ((payload->family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
                || (payload->type >= (uint8_t)UI_TRACK_TYPE_COUNT)
                || (ui_track_type_is_valid_for_family((ui_track_family_t)payload->family,
                                                      (ui_track_type_t)payload->type) == false))
        {
            return KIT_V1_RESULT_BAD_KIT;
        }

        if (kit_v1_track_uses_sampler_asset((ui_track_family_t)payload->family,
                                            (ui_track_type_t)payload->type) == 0U)
        {
            memset(&payload->asset, 0, sizeof(payload->asset));
        }
        else
        {
            uint16_t resolved_asset_slot = payload->asset.global_slot;
            if ((payload->asset.has_asset != 0U)
                    && (kit_v1_sampler_asset_kind_matches_type(payload->asset.kind,
                                                               (ui_track_type_t)payload->type) == 0U))
            {
                return KIT_V1_RESULT_BAD_KIT;
            }
            if (kit_v1_resolve_loaded_asset(&payload->asset, &resolved_asset_slot) == 0U)
            {
                return KIT_V1_RESULT_ASSET_MISS;
            }
            if (payload->asset.has_asset != 0U)
            {
                payload->tone.sample = (float)resolved_asset_slot;
            }
        }
    }

    return KIT_V1_RESULT_OK;
}

static uint8_t kit_v1_apply_track_structure(const KitSaveV1 *kit)
{
    uint8_t family[UI_TRACK_COUNT];
    uint8_t type[UI_TRACK_COUNT];
    uint8_t midi_channel[UI_TRACK_COUNT];
    uint8_t midi_source[UI_TRACK_COUNT];

    if (kit == 0)
    {
        return 0U;
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        family[track] = kit->tracks[track].family;
        type[track] = kit->tracks[track].type;
        midi_channel[track] = ui_get_track_midi_channel(track);
        midi_source[track] = (uint8_t)ui_get_track_midi_source(track);
    }

    if (ui_apply_track_config_bulk_mutation(family, type, midi_channel, midi_source) == false)
    {
        return 0U;
    }

    track_runtime_invalidate_all();
    return 1U;
}

static uint8_t kit_v1_transition_mutate(void *ctx_ptr)
{
    const KitSaveV1 *const kit = (const KitSaveV1 *)ctx_ptr;
    return kit_v1_apply_track_structure(kit);
}

static uint8_t kit_v1_transition_reapply(void *ctx_ptr)
{
    const KitSaveV1 *const kit = (const KitSaveV1 *)ctx_ptr;
    return (kit_v1_restore_loaded_kit_state(kit) == KIT_V1_RESULT_OK) ? 1U : 0U;
}

static uint8_t kit_v1_transition_ui_sync(void *ctx_ptr)
{
    (void)ctx_ptr;
    ui_active_track_sync_full_after_global_restore();
    return 1U;
}

void kit_v1_init(void)
{
    g_kit_v1_current_slot = KIT_V1_INVALID_SLOT;
    g_kit_v1_dirty = 0U;
    g_kit_v1_dirty_suspended = 0U;
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
        if (kit_v1_track_uses_sampler_asset(family, type) != 0U)
        {
            kit_v1_capture_sampler_asset(tone, &dst->asset);
        }

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
    g_kit_v1_dirty = 0U;
    if (out_slot != 0)
    {
        *out_slot = slot;
    }
    return KIT_V1_RESULT_OK;
}

kit_v1_result_t kit_v1_apply_slot(uint16_t slot)
{
    if (slot >= KIT_V1_SLOT_COUNT)
    {
        return KIT_V1_RESULT_INVALID_ARG;
    }
    if (kit_sd_bank_slot_has_data(slot) == 0U)
    {
        return KIT_V1_RESULT_EMPTY;
    }

    if (kit_sd_bank_load_slot(slot, &g_kit_v1_work) == 0U)
    {
        const kit_sd_bank_error_t sd_err = kit_sd_bank_get_last_error();
        if (sd_err == KIT_SD_BANK_ERR_GATE_BUSY)
        {
            return KIT_V1_RESULT_SD_BUSY;
        }
        if ((sd_err == KIT_SD_BANK_ERR_INVALID_HEADER)
                || (sd_err == KIT_SD_BANK_ERR_CHECKSUM_FAIL))
        {
            return KIT_V1_RESULT_BAD_KIT;
        }
        return KIT_V1_RESULT_SD_FAIL;
    }

    kit_v1_result_t result = kit_v1_validate_loaded_kit(&g_kit_v1_work);
    if (result != KIT_V1_RESULT_OK)
    {
        return result;
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        keyboard_engine_all_notes_off_for_track(track);
        brick6_sampler_runtime_reset_track(track);
    }

    const param_registry_track_transition_pipeline_cmd_t transition_cmd = {
        .prepare_fn = 0,
        .mutate_fn = kit_v1_transition_mutate,
        .reapply_fn = kit_v1_transition_reapply,
        .seq_runtime_sync_fn = 0,
        .ui_sync_fn = kit_v1_transition_ui_sync,
        .resume_fn = 0,
        .ctx = (void *)&g_kit_v1_work
    };

    g_kit_v1_dirty_suspended++;
    const uint8_t apply_ok = param_registry_run_track_transition_pipeline(&transition_cmd);
    g_kit_v1_dirty_suspended--;
    if (apply_ok == 0U)
    {
        return KIT_V1_RESULT_APPLY_FAIL;
    }

    track_runtime_refresh_all();
    g_kit_v1_current_slot = slot;
    g_kit_v1_dirty = 0U;
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

    if (g_kit_v1_current_slot == slot)
    {
        g_kit_v1_current_slot = KIT_V1_INVALID_SLOT;
        g_kit_v1_dirty = 0U;
    }
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

uint8_t kit_v1_get_current_name(char *out_name, uint32_t out_size)
{
    if ((out_name == 0) || (out_size == 0U) || (g_kit_v1_current_slot >= KIT_V1_SLOT_COUNT))
    {
        return 0U;
    }

    kit_v1_metadata_t meta;
    if (kit_sd_bank_get_slot_metadata(g_kit_v1_current_slot, &meta) == 0U)
    {
        return 0U;
    }

    kit_v1_copy_text(out_name, out_size, meta.name);
    if (out_name[0] == '\0')
    {
        (void)snprintf(out_name, out_size, "KIT %03u", (unsigned)g_kit_v1_current_slot);
    }
    return 1U;
}

uint8_t kit_v1_is_dirty(void)
{
    return ((g_kit_v1_current_slot < KIT_V1_SLOT_COUNT) && (g_kit_v1_dirty != 0U)) ? 1U : 0U;
}

void kit_v1_mark_dirty(void)
{
    if ((g_kit_v1_current_slot < KIT_V1_SLOT_COUNT) && (g_kit_v1_dirty_suspended == 0U))
    {
        g_kit_v1_dirty = 1U;
    }
}

void kit_v1_clear_dirty(void)
{
    g_kit_v1_dirty = 0U;
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
        case KIT_V1_RESULT_ASSET_MISS: return "ASSET MISS";
        case KIT_V1_RESULT_APPLY_FAIL: return "ERROR";
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
