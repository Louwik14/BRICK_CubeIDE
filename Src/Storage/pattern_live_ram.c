#include "Storage/pattern_live_ram.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/undo_v1.h"
#include "Core/track_runtime.h"
#include "UI/ui_core.h"
#include "UI/ui_active_track_sync.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_param_iface.h"
#include "Param/param_registry.h"
#include "Mod/mod_lfo_v1.h"
#include "Storage/pattern_sd_bank.h"

#define PATTERN_BANK_COUNT 16U
#define PATTERN_PER_BANK   16U

typedef struct
{
    uint8_t has_snapshot;
    uint8_t dirty_pending_persist;
} pattern_slot_meta_t;

UI_SDRAM static PatternSaveV1 g_current_pattern;
UI_SDRAM static PatternSaveV1 g_next_pattern;
UI_SDRAM static PatternSaveV1 g_boot_pattern;
static pattern_slot_meta_t g_pattern_slot_meta[PATTERN_BANK_COUNT][PATTERN_PER_BANK];

static uint8_t g_active_bank;
static uint8_t g_active_pattern;
static uint8_t g_queued_valid;
static uint8_t g_queued_bank;
static uint8_t g_queued_pattern;
static uint8_t g_queued_boundary_track;
static uint32_t g_queued_boundary_generation;
static uint8_t g_apply_in_progress;
static uint8_t pattern_live_slot_is_valid(uint8_t bank, uint8_t pattern);
static uint8_t pattern_live_apply_track_config_block(const pattern_v1_track_cfg_block_t *track_cfg);
static uint8_t pattern_live_step_required_lock_count(const pattern_v1_step_t *step);
static uint8_t pattern_live_seq_block_validate_plock_budget(const pattern_v1_seq_block_t *seq,
                                                            uint8_t *out_track,
                                                            uint16_t *out_required);

static uint8_t pattern_live_slot_is_valid(uint8_t bank, uint8_t pattern)
{
    return (bank < PATTERN_BANK_COUNT) && (pattern < PATTERN_PER_BANK);
}

static uint8_t pattern_live_apply_track_config_block(const pattern_v1_track_cfg_block_t *track_cfg)
{
    if (track_cfg == 0)
    {
        return 0U;
    }

    return (ui_apply_track_config_bulk_mutation(track_cfg->family,
                                                track_cfg->type,
                                                track_cfg->midi_channel,
                                                track_cfg->midi_source) != false)
        ? 1U
        : 0U;
}

static uint8_t pattern_live_is_param_in_sound_domain(param_id_t id)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    return (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_COLORS)
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_BUFFER)
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY);
}

static void pattern_live_plock_set_value16(pattern_v1_plock_t *plock, seq_value16_t value16)
{
    if (plock == 0)
    {
        return;
    }

    uint8_t *const dst = (uint8_t *)&plock->value16;
    dst[0] = (uint8_t)(value16 & 0xFFu);
    dst[1] = (uint8_t)((value16 >> 8) & 0xFFu);
}

static seq_value16_t pattern_live_plock_get_value16(const pattern_v1_plock_t *plock)
{
    if (plock == 0)
    {
        return 0U;
    }

    const uint8_t *const src = (const uint8_t *)&plock->value16;
    return (seq_value16_t)((seq_value16_t)src[0]
                         | (seq_value16_t)((seq_value16_t)src[1] << 8));
}

static uint8_t pattern_live_is_param_in_mix_domain(param_id_t id)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    return (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX);
}

static uint8_t pattern_live_is_track_scoped_param(param_id_t id)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    return ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_NONE)
            && (rule.status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) ? 1U : 0U;
}

static uint8_t pattern_live_is_global_param_useful(param_id_t id)
{
    if ((id == PARAM_MASTER_GAIN) || (id == PARAM_CFG_TRACK) || (id == PARAM_CFG_TRACK_TYPE)
        || (id == PARAM_CFG_MIDI_CH) || (id == PARAM_CFG_MIDI_SRC))
    {
        return 0U;
    }

    if (((id >= PARAM_MIX_TRACK0_GAIN) && (id <= PARAM_MIX_TRACK3_SEND1))
            && (id != PARAM_MIX_MUTE))
    {
        return 0U;
    }

    if (pattern_live_is_track_scoped_param(id) != 0U)
    {
        return 0U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
    {
        return 1U;
    }

    switch (id)
    {
        case PARAM_CFG_REC:
        case PARAM_CFG_TEMPO:
        case PARAM_CFG_SYNC:
        case PARAM_CFG_REC_LEN:
        case PARAM_MIX_SEND0_FX:
        case PARAM_MIX_SEND1_FX:
            return 1U;

        default:
            return 0U;
    }
}

static uint8_t pattern_live_map_legacy_mix_global(param_id_t id, uint8_t *out_track, param_id_t *out_param)
{
    if ((out_track == 0) || (out_param == 0))
    {
        return 0U;
    }

    if ((id >= PARAM_MIX_TRACK0_GAIN) && (id <= PARAM_MIX_TRACK3_GAIN))
    {
        *out_track = (uint8_t)(id - PARAM_MIX_TRACK0_GAIN);
        *out_param = PARAM_MIX_LEVEL;
        return 1U;
    }

    if ((id >= PARAM_MIX_TRACK0_PAN) && (id <= PARAM_MIX_TRACK3_PAN))
    {
        *out_track = (uint8_t)(id - PARAM_MIX_TRACK0_PAN);
        *out_param = PARAM_MIX_PAN;
        return 1U;
    }

    if ((id >= PARAM_MIX_TRACK0_MUTE) && (id <= PARAM_MIX_TRACK3_MUTE))
    {
        *out_track = (uint8_t)(id - PARAM_MIX_TRACK0_MUTE);
        *out_param = PARAM_MIX_MUTE;
        return 1U;
    }

    if ((id >= PARAM_MIX_TRACK0_SEND0) && (id <= PARAM_MIX_TRACK3_SEND1))
    {
        const uint8_t offset = (uint8_t)(id - PARAM_MIX_TRACK0_SEND0);
        *out_track = (uint8_t)(offset / 2U);
        *out_param = ((offset % 2U) == 0U) ? PARAM_MIX_SEND1 : PARAM_MIX_SEND2;
        return 1U;
    }

    return 0U;
}

static void pattern_live_apply_legacy_mix_globals_as_track_values(const PatternSaveV1 *pattern)
{
    for (uint16_t id_raw = 0U; id_raw < (uint16_t)PARAM_COUNT; ++id_raw)
    {
        const param_id_t id = (param_id_t)id_raw;
        uint8_t track = 0U;
        param_id_t target = PARAM_COUNT;

        if ((pattern->globals.global_valid[id] == 0U)
            || (pattern_live_map_legacy_mix_global(id, &track, &target) == 0U)
            || (track >= SEQ_TRACK_COUNT)
            || (target >= PARAM_COUNT)
            || (pattern->mix.track_valid[track][target] != 0U))
        {
            continue;
        }

        (void)param_registry_apply_track_value(target, track, pattern->globals.global_values[id]);
    }
}

static uint8_t pattern_live_step_required_lock_count(const pattern_v1_step_t *step)
{
    if (step == 0)
    {
        return 0U;
    }

    const uint8_t lock_count = (step->lock_count > SEQ_STEP_MAX_LOCKS)
        ? SEQ_STEP_MAX_LOCKS
        : step->lock_count;
    uint8_t unique_count = 0U;

    for (uint8_t i = 0U; i < lock_count; ++i)
    {
        const pattern_v1_plock_t *const current = &step->locks[i];
        uint8_t seen = 0U;

        for (uint8_t j = 0U; j < i; ++j)
        {
            const pattern_v1_plock_t *const prior = &step->locks[j];
            if ((prior->set_id == current->set_id) && (prior->param8 == current->param8))
            {
                seen = 1U;
                break;
            }
        }

        if (seen == 0U)
        {
            unique_count++;
        }
    }

    return unique_count;
}

static uint8_t pattern_live_seq_block_validate_plock_budget(const pattern_v1_seq_block_t *seq,
                                                            uint8_t *out_track,
                                                            uint16_t *out_required)
{
    if (seq == 0)
    {
        return 0U;
    }

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        uint16_t required = 0U;

        for (uint8_t step = 0U; step < SEQ_MAX_STEPS; ++step)
        {
            required = (uint16_t)(required + pattern_live_step_required_lock_count(&seq->tracks[track].steps[step]));
            if (required > (uint16_t)SEQ_PLOCK_BUDGET_PER_TRACK)
            {
                if (out_track != 0)
                {
                    *out_track = track;
                }
                if (out_required != 0)
                {
                    *out_required = required;
                }
                return 0U;
            }
        }
    }

    if (out_track != 0)
    {
        *out_track = 0U;
    }
    if (out_required != 0)
    {
        *out_required = 0U;
    }

    return 1U;
}

uint8_t pattern_live_capture_current(PatternSaveV1 *out_pattern)
{
    if (out_pattern == 0)
    {
        return 0U;
    }

    memset(out_pattern, 0, sizeof(*out_pattern));

    const seq_project_data_t *const project = seq_model_get_project();
    if (project == 0)
    {
        return 0U;
    }

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        out_pattern->track_cfg.family[track] = (uint8_t)ui_get_track_family(track);
        out_pattern->track_cfg.type[track] = (uint8_t)ui_get_track_type(track);
        out_pattern->track_cfg.midi_channel[track] = ui_get_track_midi_channel(track);
        out_pattern->track_cfg.midi_source[track] = (uint8_t)ui_get_track_midi_source(track);

        out_pattern->seq.tracks[track].length_steps = project->tracks[track].length_steps;
        out_pattern->seq.tracks[track].ui_page = project->tracks[track].ui_page;

        for (uint8_t step = 0U; step < SEQ_MAX_STEPS; ++step)
        {
            pattern_v1_step_t *const out_step = &out_pattern->seq.tracks[track].steps[step];
            out_step->trig = seq_model_get_trig(track, step);

            const uint8_t lock_count = seq_model_step_plock_count(track, step);
            out_step->lock_count = (lock_count > SEQ_STEP_MAX_LOCKS) ? SEQ_STEP_MAX_LOCKS : lock_count;

            for (uint8_t i = 0U; i < out_step->lock_count; ++i)
            {
                seq_plock_entry_t entry;
                if (seq_model_step_plock_get_at(track, step, i, &entry) == 0U)
                {
                    continue;
                }

                out_step->locks[i].set_id = entry.set_id;
                out_step->locks[i].param8 = entry.param8;
                pattern_live_plock_set_value16(&out_step->locks[i], entry.value16);
                out_step->locks[i].flags = entry.flags;
            }
        }

        float lfo_value = 0.0f;
        if (mod_lfo_v1_get_track_param(track, 0U, MOD_LFO_PARAM_DEST, &lfo_value) != 0U)
        {
            out_pattern->mod.tracks[track].lfo1.dest = (uint16_t)lfo_value;
        }
        if (mod_lfo_v1_get_track_param(track, 0U, MOD_LFO_PARAM_RATE, &lfo_value) != 0U)
        {
            out_pattern->mod.tracks[track].lfo1.rate = (uint8_t)lfo_value;
        }
        if (mod_lfo_v1_get_track_param(track, 0U, MOD_LFO_PARAM_DEPTH, &lfo_value) != 0U)
        {
            out_pattern->mod.tracks[track].lfo1.depth = (uint8_t)lfo_value;
        }
        if (mod_lfo_v1_get_track_param(track, 0U, MOD_LFO_PARAM_SHAPE, &lfo_value) != 0U)
        {
            out_pattern->mod.tracks[track].lfo1.shape = (uint8_t)lfo_value;
        }

        if (mod_lfo_v1_get_track_param(track, 1U, MOD_LFO_PARAM_DEST, &lfo_value) != 0U)
        {
            out_pattern->mod.tracks[track].lfo2.dest = (uint16_t)lfo_value;
        }
        if (mod_lfo_v1_get_track_param(track, 1U, MOD_LFO_PARAM_RATE, &lfo_value) != 0U)
        {
            out_pattern->mod.tracks[track].lfo2.rate = (uint8_t)lfo_value;
        }
        if (mod_lfo_v1_get_track_param(track, 1U, MOD_LFO_PARAM_DEPTH, &lfo_value) != 0U)
        {
            out_pattern->mod.tracks[track].lfo2.depth = (uint8_t)lfo_value;
        }
        if (mod_lfo_v1_get_track_param(track, 1U, MOD_LFO_PARAM_SHAPE, &lfo_value) != 0U)
        {
            out_pattern->mod.tracks[track].lfo2.shape = (uint8_t)lfo_value;
        }
    }

    for (uint16_t id_raw = 0U; id_raw < (uint16_t)PARAM_COUNT; ++id_raw)
    {
        const param_id_t id = (param_id_t)id_raw;

        if (pattern_live_is_global_param_useful(id) != 0U)
        {
            out_pattern->globals.global_values[id] = param_get(id);
            out_pattern->globals.global_valid[id] = 1U;
        }

        for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
        {
            float value = 0.0f;
            if (param_registry_get_track_value(id, track, &value) == 0U)
            {
                continue;
            }

            if (pattern_live_is_param_in_sound_domain(id) != 0U)
            {
                out_pattern->sound.track_values[track][id] = value;
                out_pattern->sound.track_valid[track][id] = 1U;
            }
            else if (pattern_live_is_param_in_mix_domain(id) != 0U)
            {
                out_pattern->mix.track_values[track][id] = value;
                out_pattern->mix.track_valid[track][id] = 1U;
            }
        }
    }

    out_pattern->globals.tempo_bpm_milli = seq_runtime_get_tempo_bpm_milli();
    out_pattern->globals.clock_src = (uint8_t)seq_runtime_get_clock_source();
    out_pattern->globals.rec_count_in_mode = seq_runtime_get_rec_count_in_mode();
    out_pattern->globals.rec_len_mode = seq_runtime_get_rec_len_mode();

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        uint8_t div = 1U;
        uint8_t quant = 0U;
        uint8_t swing = 0U;
        (void)seq_runtime_get_track_div(track, &div);
        (void)seq_runtime_get_track_quant(track, &quant);
        (void)seq_runtime_get_track_swing(track, &swing);
        out_pattern->globals.track_div[track] = div;
        out_pattern->globals.track_quant[track] = quant;
        out_pattern->globals.track_swing[track] = swing;
    }

    return 1U;
}

static uint8_t pattern_live_apply_seq_block(const pattern_v1_seq_block_t *seq)
{
    if (seq == 0)
    {
        return 0U;
    }

    uint8_t overflow_track = 0U;
    uint16_t overflow_required = 0U;
    if (pattern_live_seq_block_validate_plock_budget(seq, &overflow_track, &overflow_required) == 0U)
    {        return 0U;
    }

    seq_model_init_defaults();

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        seq_model_set_track_length(track, seq->tracks[track].length_steps);
        seq_model_set_track_page(track, seq->tracks[track].ui_page);

        for (uint8_t step = 0U; step < SEQ_MAX_STEPS; ++step)
        {
            const pattern_v1_step_t *const in_step = &seq->tracks[track].steps[step];
            seq_model_set_trig(track, step, in_step->trig);
            seq_model_step_plock_clear(track, step);

            const uint8_t lock_count = (in_step->lock_count > SEQ_STEP_MAX_LOCKS)
                ? SEQ_STEP_MAX_LOCKS
                : in_step->lock_count;

            for (uint8_t i = 0U; i < lock_count; ++i)
            {
                const pattern_v1_plock_t *const pl = &in_step->locks[i];
                const seq_plock_op_status_t status = seq_model_step_plock_upsert(track,
                                                                                 step,
                                                                                 pl->set_id,
                                                                                 pl->param8,
                                                                                 pattern_live_plock_get_value16(pl),
                                                                                 pl->flags);
                if ((status != SEQ_PLOCK_OP_CREATED) && (status != SEQ_PLOCK_OP_UPDATED))
                {                    return 0U;
                }
            }
        }
    }

    return 1U;
}

typedef struct
{
    const PatternSaveV1 *pattern;
    uint8_t resume_transport;
    uint8_t was_running;
} pattern_live_transition_ctx_t;

static uint8_t pattern_live_transition_prepare(void *ctx_ptr)
{
    pattern_live_transition_ctx_t *const ctx = (pattern_live_transition_ctx_t *)ctx_ptr;
    if ((ctx == 0) || (ctx->pattern == 0))
    {
        return 0U;
    }

    ctx->was_running = seq_runtime_is_running();
    seq_runtime_stop();
    return 1U;
}

static uint8_t pattern_live_transition_mutate(void *ctx_ptr)
{
    pattern_live_transition_ctx_t *const ctx = (pattern_live_transition_ctx_t *)ctx_ptr;
    if ((ctx == 0) || (ctx->pattern == 0))
    {
        return 0U;
    }

    return pattern_live_apply_track_config_block(&ctx->pattern->track_cfg);
}

static uint8_t pattern_live_transition_reapply(void *ctx_ptr)
{
    pattern_live_transition_ctx_t *const ctx = (pattern_live_transition_ctx_t *)ctx_ptr;
    if ((ctx == 0) || (ctx->pattern == 0))
    {
        return 0U;
    }

    if (pattern_live_apply_seq_block(&ctx->pattern->seq) == 0U)
    {
        return 0U;
    }

    param_registry_batch_begin();
    pattern_live_apply_legacy_mix_globals_as_track_values(ctx->pattern);

    for (uint16_t id_raw = 0U; id_raw < (uint16_t)PARAM_COUNT; ++id_raw)
    {
        const param_id_t id = (param_id_t)id_raw;

        for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
        {
            if (ctx->pattern->sound.track_valid[track][id] != 0U)
            {
                (void)param_registry_apply_track_value(id, track, ctx->pattern->sound.track_values[track][id]);
            }

            if (ctx->pattern->mix.track_valid[track][id] != 0U)
            {
                (void)param_registry_apply_track_value(id, track, ctx->pattern->mix.track_values[track][id]);
            }
        }

        if (ctx->pattern->globals.global_valid[id] != 0U)
        {
            if ((((id >= PARAM_MIX_TRACK0_GAIN) && (id <= PARAM_MIX_TRACK3_SEND1))
                    && (id != PARAM_MIX_MUTE))
                    || (pattern_live_is_track_scoped_param(id) != 0U))
            {
                continue;
            }

            param_set(id, ctx->pattern->globals.global_values[id]);
        }
    }

    param_registry_batch_end();

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        /* LFO restore authority: config is restored only via mod_lfo_v1_set_track_param. */
        (void)mod_lfo_v1_set_track_param(track, 0U, MOD_LFO_PARAM_DEST, (float)ctx->pattern->mod.tracks[track].lfo1.dest);
        (void)mod_lfo_v1_set_track_param(track, 0U, MOD_LFO_PARAM_RATE, (float)ctx->pattern->mod.tracks[track].lfo1.rate);
        (void)mod_lfo_v1_set_track_param(track, 0U, MOD_LFO_PARAM_DEPTH, (float)ctx->pattern->mod.tracks[track].lfo1.depth);
        (void)mod_lfo_v1_set_track_param(track, 0U, MOD_LFO_PARAM_SHAPE, (float)ctx->pattern->mod.tracks[track].lfo1.shape);
        (void)mod_lfo_v1_set_track_param(track, 1U, MOD_LFO_PARAM_DEST, (float)ctx->pattern->mod.tracks[track].lfo2.dest);
        (void)mod_lfo_v1_set_track_param(track, 1U, MOD_LFO_PARAM_RATE, (float)ctx->pattern->mod.tracks[track].lfo2.rate);
        (void)mod_lfo_v1_set_track_param(track, 1U, MOD_LFO_PARAM_DEPTH, (float)ctx->pattern->mod.tracks[track].lfo2.depth);
        (void)mod_lfo_v1_set_track_param(track, 1U, MOD_LFO_PARAM_SHAPE, (float)ctx->pattern->mod.tracks[track].lfo2.shape);
    }

    return 1U;
}

static uint8_t pattern_live_transition_seq_runtime_sync(void *ctx_ptr)
{
    pattern_live_transition_ctx_t *const ctx = (pattern_live_transition_ctx_t *)ctx_ptr;
    if ((ctx == 0) || (ctx->pattern == 0))
    {
        return 0U;
    }

    seq_runtime_set_tempo_bpm_milli(ctx->pattern->globals.tempo_bpm_milli);
    seq_runtime_set_clock_source((seq_clock_src_t)ctx->pattern->globals.clock_src);
    seq_runtime_set_rec_count_in_mode(ctx->pattern->globals.rec_count_in_mode);
    seq_runtime_set_rec_len_mode(ctx->pattern->globals.rec_len_mode);

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        seq_runtime_set_track_div(track, ctx->pattern->globals.track_div[track]);
        seq_runtime_set_track_quant(track, ctx->pattern->globals.track_quant[track]);
        seq_runtime_set_track_swing(track, ctx->pattern->globals.track_swing[track]);
    }

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        (void)seq_runtime_set_playhead_step(track, 0U);
    }

    return 1U;
}

static uint8_t pattern_live_transition_ui_sync(void *ctx_ptr)
{
    (void)ctx_ptr;
    ui_active_track_sync_full_after_global_restore();
    return 1U;
}

static uint8_t pattern_live_transition_resume(void *ctx_ptr)
{
    pattern_live_transition_ctx_t *const ctx = (pattern_live_transition_ctx_t *)ctx_ptr;
    if (ctx == 0)
    {
        return 0U;
    }

    if ((ctx->resume_transport != 0U) && (ctx->was_running != 0U))
    {
        seq_runtime_start();
    }

    return 1U;
}

uint8_t pattern_live_apply_snapshot(const PatternSaveV1 *pattern, uint8_t resume_transport)
{
    if ((pattern == 0) || (g_apply_in_progress != 0U))
    {
        return 0U;
    }

    if (pattern_live_seq_block_validate_plock_budget(&pattern->seq, 0, 0) == 0U)
    {
        return 0U;
    }

    g_apply_in_progress = 1U;

    pattern_live_transition_ctx_t transition_ctx = {
        .pattern = pattern,
        .resume_transport = resume_transport,
        .was_running = 0U
    };
    const param_registry_track_transition_pipeline_cmd_t transition_cmd = {
        .prepare_fn = pattern_live_transition_prepare,
        .mutate_fn = pattern_live_transition_mutate,
        .reapply_fn = pattern_live_transition_reapply,
        .seq_runtime_sync_fn = pattern_live_transition_seq_runtime_sync,
        .ui_sync_fn = pattern_live_transition_ui_sync,
        .resume_fn = pattern_live_transition_resume,
        .ctx = (void *)&transition_ctx
    };

    /* Structural mutation seam: snapshot restore runs through the explicit transition pipeline. */
    if (param_registry_run_track_transition_pipeline(&transition_cmd) == 0U)
    {
        g_apply_in_progress = 0U;
        return 0U;
    }

    g_apply_in_progress = 0U;
    return 1U;
}

uint8_t pattern_live_apply_boot_snapshot(uint8_t resume_transport)
{
    if (pattern_live_apply_snapshot(&g_boot_pattern, resume_transport) == 0U)
    {
        return 0U;
    }

    memcpy(&g_current_pattern, &g_boot_pattern, sizeof(g_current_pattern));
    memcpy(&g_next_pattern, &g_boot_pattern, sizeof(g_next_pattern));
    g_active_bank = 0U;
    g_active_pattern = 0U;
    g_queued_valid = 0U;
    g_queued_bank = 0U;
    g_queued_pattern = 0U;
    g_queued_boundary_track = 0U;
    g_queued_boundary_generation = 0U;
    return 1U;
}

uint8_t pattern_live_capture_to_slot(uint8_t bank, uint8_t pattern)
{
    if (pattern_live_slot_is_valid(bank, pattern) == 0U)
    {
        return 0U;
    }

    if (pattern_live_capture_current(&g_current_pattern) == 0U)
    {
        return 0U;
    }

    if (pattern_sd_bank_store_slot(bank, pattern, &g_current_pattern) == 0U)
    {
        return 0U;
    }

    g_pattern_slot_meta[bank][pattern].has_snapshot = 1U;
    g_pattern_slot_meta[bank][pattern].dirty_pending_persist = 1U;

    if ((bank == g_queued_bank) && (pattern == g_queued_pattern) && (g_queued_valid != 0U))
    {
        memcpy(&g_next_pattern, &g_current_pattern, sizeof(g_next_pattern));
    }

    return 1U;
}

uint8_t pattern_live_queue_slot(uint8_t bank, uint8_t pattern)
{
    if (pattern_live_slot_is_valid(bank, pattern) == 0U)
    {
        return 0U;
    }

    const uint8_t has_snapshot = pattern_sd_bank_slot_has_data(bank, pattern);
    g_pattern_slot_meta[bank][pattern].has_snapshot = has_snapshot;

    if (has_snapshot == 0U)
    {
        memcpy(&g_next_pattern, &g_boot_pattern, sizeof(g_next_pattern));
    }
    else if (pattern_sd_bank_load_slot(bank, pattern, &g_next_pattern) == 0U)
    {
        return 0U;
    }

    if (seq_runtime_is_running() == 0U)
    {
        if (pattern_live_apply_snapshot(&g_next_pattern, 0U) == 0U)
        {
            return 0U;
        }
        memcpy(&g_current_pattern, &g_next_pattern, sizeof(g_current_pattern));
        g_active_bank = bank;
        g_active_pattern = pattern;
        g_queued_valid = 0U;
        g_queued_boundary_track = 0U;
        g_queued_boundary_generation = 0U;
        undo_v1_clear_history();
        return 1U;
    }

    g_queued_bank = bank;
    g_queued_pattern = pattern;
    g_queued_valid = 1U;
    g_queued_boundary_track = ui_get_active_track();
    if (g_queued_boundary_track >= SEQ_TRACK_COUNT)
    {
        g_queued_boundary_track = 0U;
    }
    (void)seq_runtime_get_track_loop_generation(g_queued_boundary_track, &g_queued_boundary_generation);
    undo_v1_clear_history();
    return 1U;
}

uint8_t pattern_live_capture_boot_snapshot(void)
{
    if (pattern_live_capture_current(&g_boot_pattern) == 0U)
    {
        return 0U;
    }

    return 1U;
}

void pattern_live_service(void)
{
    if ((g_queued_valid == 0U) || (seq_runtime_is_running() == 0U) || (g_apply_in_progress != 0U))
    {
        return;
    }

    uint32_t current_generation = 0U;
    if (seq_runtime_get_track_loop_generation(g_queued_boundary_track, &current_generation) == 0U)
    {
        return;
    }

    if (current_generation == g_queued_boundary_generation)
    {
        return;
    }

    if (pattern_live_apply_snapshot(&g_next_pattern, 1U) != 0U)
    {
        memcpy(&g_current_pattern, &g_next_pattern, sizeof(g_current_pattern));
        g_active_bank = g_queued_bank;
        g_active_pattern = g_queued_pattern;
        g_queued_valid = 0U;
        g_queued_boundary_track = 0U;
        g_queued_boundary_generation = 0U;
        undo_v1_clear_history();
    }
}

void pattern_live_init(void)
{
    memset(&g_current_pattern, 0, sizeof(g_current_pattern));
    memset(&g_next_pattern, 0, sizeof(g_next_pattern));
    memset(&g_boot_pattern, 0, sizeof(g_boot_pattern));
    memset(&g_pattern_slot_meta, 0, sizeof(g_pattern_slot_meta));
    g_active_bank = 0U;
    g_active_pattern = 0U;
    g_queued_valid = 0U;
    g_queued_bank = 0U;
    g_queued_pattern = 0U;
    g_queued_boundary_track = 0U;
    g_queued_boundary_generation = 0U;
    g_apply_in_progress = 0U;

    if (pattern_live_capture_current(&g_boot_pattern) != 0U)
    {
        memcpy(&g_current_pattern, &g_boot_pattern, sizeof(g_current_pattern));
        memcpy(&g_next_pattern, &g_boot_pattern, sizeof(g_next_pattern));
        pattern_sd_bank_init(&g_boot_pattern);
    }
    else
    {
        pattern_sd_bank_init(0);
    }

    for (uint8_t bank = 0U; bank < PATTERN_BANK_COUNT; ++bank)
    {
        for (uint8_t pattern = 0U; pattern < PATTERN_PER_BANK; ++pattern)
        {
            g_pattern_slot_meta[bank][pattern].has_snapshot = pattern_sd_bank_slot_has_data(bank, pattern);
            g_pattern_slot_meta[bank][pattern].dirty_pending_persist = 0U;
        }
    }
}

uint8_t pattern_live_get_active(uint8_t *out_bank, uint8_t *out_pattern)
{
    if ((out_bank == 0) || (out_pattern == 0))
    {
        return 0U;
    }

    *out_bank = g_active_bank;
    *out_pattern = g_active_pattern;
    return 1U;
}

uint8_t pattern_live_get_queued(uint8_t *out_valid, uint8_t *out_bank, uint8_t *out_pattern)
{
    if ((out_valid == 0) || (out_bank == 0) || (out_pattern == 0))
    {
        return 0U;
    }

    *out_valid = g_queued_valid;
    *out_bank = g_queued_bank;
    *out_pattern = g_queued_pattern;
    return 1U;
}

void pattern_live_set_active_state(uint8_t active_bank,
                                   uint8_t active_pattern,
                                   uint8_t queued_valid,
                                   uint8_t queued_bank,
                                   uint8_t queued_pattern)
{
    if (pattern_live_slot_is_valid(active_bank, active_pattern) != 0U)
    {
        g_active_bank = active_bank;
        g_active_pattern = active_pattern;
    }

    if ((queued_valid != 0U) && (pattern_live_slot_is_valid(queued_bank, queued_pattern) != 0U))
    {
        g_queued_valid = 1U;
        g_queued_bank = queued_bank;
        g_queued_pattern = queued_pattern;
        g_queued_boundary_track = ui_get_active_track();
        if (g_queued_boundary_track >= SEQ_TRACK_COUNT)
        {
            g_queued_boundary_track = 0U;
        }
        (void)seq_runtime_get_track_loop_generation(g_queued_boundary_track, &g_queued_boundary_generation);
    }
    else
    {
        g_queued_valid = 0U;
        g_queued_boundary_track = 0U;
        g_queued_boundary_generation = 0U;
    }
}

uint8_t pattern_live_is_apply_in_progress(void)
{
    return g_apply_in_progress;
}
