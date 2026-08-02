#include "Storage/pattern_live_ram.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/looper_storage.h"
#include "Storage/kit_v1.h"
#include "Storage/multi_record_writer.h"
#include "Storage/sd_preview.h"
#include "Storage/undo_v2.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/synth_polyphony.h"
#include "Core/track_runtime.h"
#include "Core/track_input_ownership.h"
#include "Core/track_state.h"
#include "UI/ui_core.h"
#include "UI/ui_core_runtime_bridge.h"
#include "UI/ui_active_track_sync.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_param_iface.h"
#include "Param/param_registry.h"
#include "NoteFx/note_fx_pipeline.h"
#include "NoteFx/note_fx_state.h"
#include "Storage/pattern_sd_bank.h"

#define PATTERN_BANK_COUNT 16U
#define PATTERN_PER_BANK   16U

#if (UI_TRACK_COUNT != SEQ_TRACK_COUNT)
#error "pattern_live_ram requires UI_TRACK_COUNT == SEQ_TRACK_COUNT."
#endif

typedef struct
{
    uint8_t has_snapshot;
} pattern_slot_meta_t;

typedef enum
{
    PATTERN_LOAD_IDLE = 0,
    PATTERN_LOAD_REQUESTED,
    PATTERN_LOAD_LOADING,
    PATTERN_LOAD_READY,
    PATTERN_LOAD_ERROR
} pattern_load_state_t;

UI_SDRAM static PatternSaveV1 g_current_pattern;
UI_SDRAM static PatternSaveV1 g_next_pattern;
UI_SDRAM static PatternSaveV1 g_boot_pattern;
UI_SDRAM static PatternSaveV1 g_pattern_load_ready;
UI_SDRAM static PatternSaveV1 g_pattern_apply_normalized;
static uint8_t g_pattern_voice_limited;

uint8_t pattern_live_last_voice_limited(void) { return g_pattern_voice_limited; }

static uint8_t pattern_live_resolve_voice_budget(const PatternSaveV1 *pattern,
                                                 uint8_t assigned[SEQ_TRACK_COUNT],
                                                 uint8_t *out_limited)
{
    if ((pattern == 0) || (assigned == 0) || (out_limited == 0)) return 0U;
    memset(assigned, 0, SEQ_TRACK_COUNT);
    uint8_t remaining = SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET;
    uint8_t limited = 0U;
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const uint8_t family = pattern->track_cfg.family[track];
        const uint8_t type = pattern->track_cfg.type[track];
        if ((family == (uint8_t)UI_TRACK_FAMILY_SYNTH)
                || (family == (uint8_t)UI_TRACK_FAMILY_DRUM))
        {
            if (remaining == 0U) return 0U;
            assigned[track] = 1U;
            remaining--;
        }
        else if ((family == (uint8_t)UI_TRACK_FAMILY_SAMPLER)
                && (type == (uint8_t)UI_TRACK_TYPE_MULTI))
        {
            float raw = 1.0f;
            if (pattern->sound.track_valid[track][PARAM_CFG_POLY_VOICES] != 0U)
            {
                raw = pattern->sound.track_values[track][PARAM_CFG_POLY_VOICES];
            }
            assigned[track] = (uint8_t)((raw < 1.0f)
                ? 1U
                : ((raw > (float)SAMPLER_MULTI_MAX_VOICES_PER_TRACK)
                    ? SAMPLER_MULTI_MAX_VOICES_PER_TRACK
                    : (uint8_t)raw));
        }
    }
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        if (pattern->track_cfg.family[track] != (uint8_t)UI_TRACK_FAMILY_SYNTH) continue;
        float raw = 1.0f;
        if (pattern->sound.track_valid[track][PARAM_CFG_POLY_VOICES] != 0U)
            raw = pattern->sound.track_values[track][PARAM_CFG_POLY_VOICES];
        uint8_t requested = (uint8_t)((raw < 1.0f) ? 1U : ((raw > 8.0f) ? 8U : (uint8_t)raw));
        while ((assigned[track] < requested) && (remaining > 0U))
        { assigned[track]++; remaining--; }
        if (assigned[track] < requested) limited = 1U;
    }
    *out_limited = limited;
    return 1U;
}
STORAGE_STATE_SDRAM static pattern_slot_meta_t g_pattern_slot_meta[PATTERN_BANK_COUNT][PATTERN_PER_BANK];

static uint8_t g_active_bank;
static uint8_t g_active_pattern;
static uint8_t g_queued_valid;
static uint8_t g_queued_bank;
static uint8_t g_queued_pattern;
static uint8_t g_queued_boundary_track;
static uint32_t g_queued_boundary_generation;
static uint8_t g_pending_queue_valid;
static uint8_t g_pending_queue_bank;
static uint8_t g_pending_queue_pattern;
static uint8_t g_pending_boundary_track;
static uint32_t g_pending_boundary_generation;
static uint8_t g_apply_in_progress;
static pattern_load_state_t g_pattern_load_state;
static uint8_t g_pattern_load_bank;
static uint8_t g_pattern_load_pattern;
static uint8_t g_pattern_load_last_error;

#define PATTERN_LOAD_ERR_INVALID_SLOT 1U
#define PATTERN_LOAD_ERR_SD_LOAD 2U
#define PATTERN_LOAD_ERR_RECORD_ACTIVE 3U

static uint8_t pattern_live_slot_is_valid(uint8_t bank, uint8_t pattern);
static uint8_t pattern_live_apply_track_config_block(const pattern_v1_track_cfg_block_t *track_cfg);
static uint8_t pattern_live_step_required_lock_count(const pattern_v1_step_t *step);
static uint8_t pattern_live_seq_block_validate_plock_budget(const pattern_v1_seq_block_t *seq,
                                                            uint8_t *out_track,
                                                            uint16_t *out_required);
static uint8_t pattern_live_seq_block_validate_plock_slots(const pattern_v1_seq_block_t *seq);
static uint8_t pattern_live_arm_ready_queue(uint8_t bank,
                                            uint8_t pattern,
                                            const PatternSaveV1 *snapshot,
                                            uint8_t boundary_track,
                                            uint32_t boundary_generation);
static uint8_t pattern_live_try_take_pending_ready(void);
static void pattern_live_apply_linked_kit_for_snapshot(const PatternSaveV1 *pattern);

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

    if (ui_apply_track_config_bulk_mutation_with_inputs(track_cfg->family,
                                                        track_cfg->type,
                                                        track_cfg->midi_channel,
                                                        track_cfg->midi_source,
                                                        track_cfg->external_input) == false)
    {
        return 0U;
    }

    for (uint8_t looper_track = 0U; looper_track < SEQ_TRACK_COUNT; ++looper_track)
    {
        for (uint8_t source_track = 0U; source_track < SEQ_TRACK_COUNT; ++source_track)
        {
            ui_core_runtime_bridge_set_looper_route_enabled(
                looper_track,
                source_track,
                track_cfg->looper_route_enabled[looper_track][source_track]);
        }
    }

    return 1U;
}

static uint8_t pattern_live_is_param_in_sound_domain(param_id_t id)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    return (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV)
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_CFG)
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

static uint8_t pattern_live_param_is_storable_for_track(uint8_t track, param_id_t id)
{
    (void)id;
    return track_topology_is_active(track);
}

static uint8_t pattern_live_is_track_scoped_param(param_id_t id)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    return ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_NONE)
            && (rule.status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) ? 1U : 0U;
}

typedef enum
{
    PATTERN_LIVE_PARAM_GLOBAL = 0,
    PATTERN_LIVE_PARAM_TRACK_AWARE,
    PATTERN_LIVE_PARAM_RESERVED,
    PATTERN_LIVE_PARAM_NOT_RELEVANT
} pattern_live_param_class_t;

static pattern_live_param_class_t pattern_live_classify_param(param_id_t id)
{
    switch (id)
    {
        case PARAM_MIX_REVERB_WET:
        case PARAM_MIX_REVERB_SIZE:
        case PARAM_MIX_REVERB_DECAY:
        case PARAM_MIX_REVERB_PRED:
        case PARAM_MIX_REVERB_SPECTRAL_POSITION:
        case PARAM_MIX_REVERB_SPECTRAL_WIDTH:
        case PARAM_MIX_REVERB_DAMP:
        case PARAM_MIX_REVERB_TANK_SIZE:
        case PARAM_CFG_START:
        case PARAM_CFG_TEMPO:
        case PARAM_CFG_SYNC:
        case PARAM_CFG_REC_LEN:
        case PARAM_CFG_METRO:
        case PARAM_MIX_SEND0_FX:
        case PARAM_MIX_SEND1_FX:
        case PARAM_MIX_DELAY_TYPE:
        case PARAM_MIX_DELAY_TIME:
        case PARAM_MIX_DELAY_PINGPONG:
        case PARAM_MIX_DELAY_MODE:
        case PARAM_MIX_DELAY_TIME_R:
        case PARAM_MIX_DELAY_WIDTH:
        case PARAM_MIX_DELAY_FEEDBACK:
        case PARAM_MIX_DELAY_SPECTRAL_POSITION:
        case PARAM_MIX_DELAY_SPECTRAL_WIDTH:
        case PARAM_MIX_DELAY_FBW:
        case PARAM_MIX_DELAY_MOD:
        case PARAM_MIX_DELAY_MOD_RATE:
        case PARAM_MIX_DELAY_REV:
        case PARAM_MIX_DELAY_VOL:
        case PARAM_COMP_MODEL:
        case PARAM_BUS_COMP_THRESHOLD_DB:
        case PARAM_BUS_COMP_RATIO:
        case PARAM_BUS_COMP_ATTACK_INDEX:
        case PARAM_BUS_COMP_RELEASE_INDEX:
        case PARAM_BUS_COMP_MAKEUP_DB:
        case PARAM_BUS_COMP_DRYWET:
        case PARAM_BUS_COMP_HPF_HZ:
        case PARAM_COMP_DETECT:
        case PARAM_COMP_KNEE_DB:
        case PARAM_COMP_DELUGE_SAT:
        case PARAM_POST_GAIN:
        case PARAM_OUTPUT_COMP:
            return PATTERN_LIVE_PARAM_GLOBAL;

        case PARAM_RESERVED_000:
        case PARAM_RESERVED_001:
        case PARAM_RESERVED_002:
        case PARAM_RESERVED_003:
        case PARAM_RESERVED_004:
        case PARAM_RESERVED_005:
        case PARAM_RESERVED_006:
        case PARAM_RESERVED_007:
        case PARAM_RESERVED_008:
        case PARAM_RESERVED_009:
        case PARAM_RESERVED_010:
        case PARAM_RESERVED_011:
        case PARAM_RESERVED_012:
        case PARAM_RESERVED_013:
        case PARAM_RESERVED_015:
        case PARAM_RESERVED_018:
        case PARAM_RESERVED_019:
        case PARAM_RESERVED_020:
        case PARAM_RESERVED_030:
        case PARAM_RESERVED_031:
        case PARAM_RESERVED_032:
        case PARAM_RESERVED_033:
        case PARAM_RESERVED_034:
        case PARAM_RESERVED_035:
        case PARAM_RESERVED_036:
        case PARAM_RESERVED_037:
            return PATTERN_LIVE_PARAM_RESERVED;

        default:
            break;
    }

    if (pattern_live_is_track_scoped_param(id) != 0U)
    {
        return PATTERN_LIVE_PARAM_TRACK_AWARE;
    }

    return PATTERN_LIVE_PARAM_NOT_RELEVANT;
}

static uint8_t pattern_live_locks_required(const pattern_v1_plock_t *locks,
                                           uint8_t lock_count)
{
    if (locks == 0)
    {
        return 0U;
    }
    uint8_t unique_count = 0U;

    for (uint8_t i = 0U; i < lock_count; ++i)
    {
        const pattern_v1_plock_t *const current = &locks[i];
        uint8_t seen = 0U;

        for (uint8_t j = 0U; j < i; ++j)
        {
            const pattern_v1_plock_t *const prior = &locks[j];
            if ((prior->set_id == current->set_id) && (prior->param_slot == current->param_slot))
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

static uint8_t pattern_live_step_required_lock_count(const pattern_v1_step_t *step)
{
    if (step == 0) return 0U;
    const uint8_t count = (step->lock_count > SEQ_STEP_MAX_LOCKS)
        ? SEQ_STEP_MAX_LOCKS : step->lock_count;
    return pattern_live_locks_required(step->locks, count);
}

static uint8_t pattern_live_seq_block_validate_plock_budget(const pattern_v1_seq_block_t *seq,
                                                            uint8_t *out_track,
                                                            uint16_t *out_required)
{
    if (seq == 0)
    {
        return 0U;
    }

    for (uint8_t track = 0U; track < track_topology_get_logical_track_count(); ++track)
    {
        uint16_t required = 0U;
        const uint16_t track_capacity = seq_model_get_track_plock_capacity(track);
        const uint8_t step_limit = seq_model_get_step_lock_limit(track);

        for (uint8_t step = 0U; step < SEQ_MAX_STEPS; ++step)
        {
            const pattern_v1_step_t *const saved_step = &seq->tracks[track].steps[step];
            if (saved_step->lock_count > SEQ_STEP_MAX_LOCKS) return 0U;
            const uint8_t step_required = pattern_live_step_required_lock_count(saved_step);
            if (step_required > step_limit)
            {
                if (out_track != 0) *out_track = track;
                if (out_required != 0) *out_required = step_required;
                return 0U;
            }
            required = (uint16_t)(required + step_required);
            if (required > track_capacity)
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

static uint8_t pattern_live_seq_block_validate_plock_slots(const pattern_v1_seq_block_t *seq)
{
    if (seq == 0)
    {
        return 0U;
    }

    for (uint8_t track = 0U; track < track_topology_get_logical_track_count(); ++track)
    {
        const pattern_v1_track_seq_t *const saved = &seq->tracks[track];

        for (uint8_t step = 0U; step < SEQ_MAX_STEPS; ++step)
        {
            const uint8_t lock_count = saved->steps[step].lock_count;
            if (lock_count > SEQ_STEP_MAX_LOCKS)
            {
                return 0U;
            }

            for (uint8_t lock = 0U; lock < lock_count; ++lock)
            {
                const pattern_v1_plock_t *const plock = &saved->steps[step].locks[lock];
                param_id_t param = PARAM_COUNT;
                if (seq_param_iface_slot_to_param(track,
                                                  plock->set_id,
                                                  plock->param_slot,
                                                  &param) == 0U)
                {
                    return 0U;
                }
            }
        }
    }

    return 1U;
}

static uint8_t pattern_live_arm_ready_queue(uint8_t bank,
                                            uint8_t pattern,
                                            const PatternSaveV1 *snapshot,
                                            uint8_t boundary_track,
                                            uint32_t boundary_generation)
{
    if ((snapshot == 0) || (pattern_live_slot_is_valid(bank, pattern) == 0U))
    {
        return 0U;
    }

    if (boundary_track >= SEQ_TRACK_COUNT)
    {
        boundary_track = 0U;
    }

    if (snapshot != &g_next_pattern)
    {
        memcpy(&g_next_pattern, snapshot, sizeof(g_next_pattern));
    }
    g_queued_valid = 1U;
    g_queued_bank = bank;
    g_queued_pattern = pattern;
    g_queued_boundary_track = boundary_track;
    g_queued_boundary_generation = boundary_generation;

    if ((g_pending_queue_valid != 0U)
        && (g_pending_queue_bank == bank)
        && (g_pending_queue_pattern == pattern))
    {
        g_pending_queue_valid = 0U;
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
    out_pattern->globals.linked_kit_valid = g_current_pattern.globals.linked_kit_valid;
    out_pattern->globals.linked_kit_slot = g_current_pattern.globals.linked_kit_slot;

    for (uint8_t track = 0U; track < track_topology_get_logical_track_count(); ++track)
    {
        out_pattern->track_cfg.family[track] = (uint8_t)ui_get_track_family(track);
        out_pattern->track_cfg.type[track] = (uint8_t)ui_get_track_type(track);
        out_pattern->track_cfg.external_input[track] = ui_get_track_external_input(track);
        out_pattern->track_cfg.midi_channel[track] = ui_get_track_midi_channel(track);
        out_pattern->track_cfg.midi_source[track] = (uint8_t)ui_get_track_midi_source(track);
        if ((track < NOTE_FX_TRACK_COUNT)
                && (note_fx_state_capture_track(track, &out_pattern->note_fx[track]) == 0U))
        {
            return 0U;
        }
        for (uint8_t source_track = 0U; source_track < SEQ_TRACK_COUNT; ++source_track)
        {
            out_pattern->track_cfg.looper_route_enabled[track][source_track] =
                ui_core_runtime_bridge_get_looper_route_enabled(track, source_track);
        }

        pattern_v1_track_seq_t *const saved = &out_pattern->seq.tracks[track];
        saved->length_steps = seq_model_get_track_length(track);
        saved->ui_page = seq_model_get_track_page(track);
        for (uint8_t step = 0U; step < SEQ_MAX_STEPS; ++step)
        {
            pattern_v1_step_t *const out_step = &saved->steps[step];
            out_step->trig = seq_model_get_trig(track, step);
            out_step->roll = seq_model_get_step_roll(track, step);
            const uint8_t count = seq_model_step_plock_count(track, step);
            out_step->lock_count = (count > SEQ_STEP_MAX_LOCKS) ? SEQ_STEP_MAX_LOCKS : count;
            for (uint8_t i = 0U; i < out_step->lock_count; ++i)
            {
                seq_plock_entry_t entry;
                if (seq_model_step_plock_get_at(track, step, i, &entry) == 0U) return 0U;
                out_step->locks[i].set_id = entry.set_id;
                out_step->locks[i].param_slot = entry.param_slot;
                pattern_live_plock_set_value16(&out_step->locks[i], entry.value16);
                out_step->locks[i].flags = entry.flags;
            }
        }
    }

    for (uint16_t id_raw = 0U; id_raw < (uint16_t)PARAM_PERSIST_COUNT; ++id_raw)
    {
        const param_id_t id = (param_id_t)id_raw;

        const pattern_live_param_class_t classification = pattern_live_classify_param(id);

        if (classification == PATTERN_LIVE_PARAM_GLOBAL)
        {
            out_pattern->globals.global_values[id] = param_get(id);
            out_pattern->globals.global_valid[id] = 1U;
        }

        if (classification != PATTERN_LIVE_PARAM_TRACK_AWARE)
        {
            continue;
        }

        for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
        {
            if (pattern_live_param_is_storable_for_track(track, id) == 0U) continue;
            const track_runtime_param_status_t status = track_runtime_get_effective_param_status(track, id);
            if (status == TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL)
            {
                continue;
            }

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
    out_pattern->globals.rec_start_mode = seq_runtime_get_rec_start_mode();
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
    if (pattern_live_seq_block_validate_plock_slots(seq) == 0U)
    {
        return 0U;
    }

    seq_model_init_defaults();

    for (uint8_t track = 0U; track < track_topology_get_logical_track_count(); ++track)
    {
        const pattern_v1_track_seq_t *const saved = &seq->tracks[track];
        seq_model_set_track_length(track, saved->length_steps);
        seq_model_set_track_page(track, saved->ui_page);

        for (uint8_t step = 0U; step < SEQ_MAX_STEPS; ++step)
        {
            const pattern_v1_step_t *const saved_step = &saved->steps[step];
            seq_model_set_trig(track, step, saved_step->trig);
            seq_model_set_step_roll(track, step, saved_step->roll);
            seq_model_step_plock_clear(track, step);

            const uint8_t step_limit = seq_model_get_step_lock_limit(track);
            const uint8_t raw_count = saved_step->lock_count;
            const uint8_t lock_count = (raw_count > step_limit) ? step_limit : raw_count;

            for (uint8_t i = 0U; i < lock_count; ++i)
            {
                const pattern_v1_plock_t *const pl = &saved_step->locks[i];
                const seq_plock_op_status_t status = seq_model_step_plock_upsert(track,
                                                                                 step,
                                                                                 pl->set_id,
                                                                                 pl->param_slot,
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
    uint8_t voice_count[SEQ_TRACK_COUNT];
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

    for (uint16_t id_raw = 0U; id_raw < (uint16_t)PARAM_PERSIST_COUNT; ++id_raw)
    {
        const param_id_t id = (param_id_t)id_raw;
        const pattern_live_param_class_t classification = pattern_live_classify_param(id);

        if (classification == PATTERN_LIVE_PARAM_TRACK_AWARE)
        {
            for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
            {
                if (pattern_live_param_is_storable_for_track(track, id) == 0U) continue;
                if (ctx->pattern->sound.track_valid[track][id] != 0U)
                {
                    float value = ctx->pattern->sound.track_values[track][id];
                    const uint8_t family = ctx->pattern->track_cfg.family[track];
                    if ((id == PARAM_CFG_POLY_VOICES)
                            && ((family == (uint8_t)UI_TRACK_FAMILY_SYNTH)
                                || (family == (uint8_t)UI_TRACK_FAMILY_DRUM))
                            && (ctx->voice_count[track] != 0U))
                    {
                        value = (float)ctx->voice_count[track];
                    }
                    (void)param_registry_apply_track_value(id, track, value);
                }

                if ((pattern_live_is_param_in_mix_domain(id) != 0U)
                        && (ctx->pattern->mix.track_valid[track][id] != 0U))
                {
                    (void)param_registry_apply_track_value(id, track, ctx->pattern->mix.track_values[track][id]);
                }
            }
        }

        if ((classification == PATTERN_LIVE_PARAM_GLOBAL)
                && (ctx->pattern->globals.global_valid[id] != 0U))
        {
            param_set(id, ctx->pattern->globals.global_values[id]);
        }
    }

    param_registry_batch_end();

    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
    {
        if (note_fx_state_restore_track(track, &ctx->pattern->note_fx[track]) == 0U)
        {
            return 0U;
        }
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
    seq_runtime_set_rec_start_mode(ctx->pattern->globals.rec_start_mode);
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

    memcpy(&g_pattern_apply_normalized, pattern, sizeof(g_pattern_apply_normalized));
    pattern = &g_pattern_apply_normalized;

    if (pattern_live_seq_block_validate_plock_budget(&pattern->seq, 0, 0) == 0U)
    {
        return 0U;
    }

    ui_track_config_t ownership_configs[SEQ_TRACK_COUNT];
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        ownership_configs[track].family = (ui_track_family_t)pattern->track_cfg.family[track];
        ownership_configs[track].type = (ui_track_type_t)pattern->track_cfg.type[track];
    }
    if (track_input_ownership_validate_bulk(
            ownership_configs, pattern->track_cfg.external_input) == 0U)
    {
        return 0U;
    }

    g_pattern_voice_limited = 0U;
    uint8_t resolved_voice_count[SEQ_TRACK_COUNT];
    if (pattern_live_resolve_voice_budget(pattern,
                                          resolved_voice_count,
                                          &g_pattern_voice_limited) == 0U)
    {
        return 0U;
    }
    g_apply_in_progress = 1U;
    note_fx_pipeline_reset_all_runtime_overrides();

    pattern_live_transition_ctx_t transition_ctx = {
        .pattern = pattern,
        .resume_transport = resume_transport,
        .was_running = 0U
    };
    memcpy(transition_ctx.voice_count, resolved_voice_count, sizeof(transition_ctx.voice_count));
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

    memcpy(&g_current_pattern, pattern, sizeof(g_current_pattern));
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        g_current_pattern.sound.track_valid[track][PARAM_CFG_POLY_VOICES] = 0U;
        g_current_pattern.sound.track_valid[track][PARAM_CFG_POLY_SPREAD] = 0U;
        g_current_pattern.mix.track_valid[track][PARAM_CFG_POLY_VOICES] = 0U;
        g_current_pattern.mix.track_valid[track][PARAM_CFG_POLY_SPREAD] = 0U;
        const uint8_t is_synth = (uint8_t)((pattern->track_cfg.family[track] == (uint8_t)UI_TRACK_FAMILY_SYNTH)
            || (pattern->track_cfg.family[track] == (uint8_t)UI_TRACK_FAMILY_DRUM));
        const uint8_t is_multi = (uint8_t)((pattern->track_cfg.family[track] == (uint8_t)UI_TRACK_FAMILY_SAMPLER)
            && (pattern->track_cfg.type[track] == (uint8_t)UI_TRACK_TYPE_MULTI));
        if ((is_synth != 0U) || (is_multi != 0U))
        {
            float value = 0.0f;
            if ((param_registry_get_track_value(PARAM_CFG_POLY_VOICES, track, &value) != 0U))
            {
                g_current_pattern.sound.track_values[track][PARAM_CFG_POLY_VOICES] = value;
                g_current_pattern.sound.track_valid[track][PARAM_CFG_POLY_VOICES] = 1U;
            }
            if ((param_registry_get_track_value(PARAM_CFG_POLY_SPREAD, track, &value) != 0U))
            {
                g_current_pattern.sound.track_values[track][PARAM_CFG_POLY_SPREAD] = value;
                g_current_pattern.sound.track_valid[track][PARAM_CFG_POLY_SPREAD] = 1U;
            }
        }
    }
    pattern_live_apply_linked_kit_for_snapshot(pattern);
    g_apply_in_progress = 0U;
    undo_v2_invalidate_history();
    return 1U;
}

static void pattern_live_apply_linked_kit_for_snapshot(const PatternSaveV1 *pattern)
{
    if ((pattern != 0)
            && (pattern->globals.linked_kit_valid != 0U)
            && (pattern->globals.linked_kit_slot < KIT_V1_SLOT_COUNT))
    {
        const kit_v1_result_t result = kit_v1_apply_slot(pattern->globals.linked_kit_slot);
        if ((result != KIT_V1_RESULT_OK) && (result != KIT_V1_RESULT_VOICE_LIMITED))
        {
            kit_v1_set_current_slot(KIT_V1_INVALID_SLOT);
            kit_v1_clear_dirty();
        }
        return;
    }

    kit_v1_set_current_slot(KIT_V1_INVALID_SLOT);
    kit_v1_clear_dirty();
}

uint8_t pattern_live_apply_boot_snapshot(uint8_t resume_transport)
{
    if (pattern_live_apply_snapshot(&g_boot_pattern, resume_transport) == 0U)
    {
        return 0U;
    }

    memcpy(&g_next_pattern, &g_current_pattern, sizeof(g_next_pattern));
    g_active_bank = 0U;
    g_active_pattern = 0U;
    g_queued_valid = 0U;
    g_queued_bank = 0U;
    g_queued_pattern = 0U;
    g_queued_boundary_track = 0U;
    g_queued_boundary_generation = 0U;
    g_pending_queue_valid = 0U;
    g_pending_queue_bank = 0U;
    g_pending_queue_pattern = 0U;
    g_pending_boundary_track = 0U;
    g_pending_boundary_generation = 0U;
    pattern_load_cancel();
    return 1U;
}

uint8_t pattern_load_request(uint8_t bank, uint8_t pattern)
{
    if(sd_preview_is_active() != 0U)
    {
        sd_preview_stop();
    }

    if (pattern_live_slot_is_valid(bank, pattern) == 0U)
    {
        g_pattern_load_state = PATTERN_LOAD_ERROR;
        g_pattern_load_last_error = PATTERN_LOAD_ERR_INVALID_SLOT;
        return 0U;
    }

    if ((multi_record_writer_any_active_backend(MULTI_RECORD_WRITER_BACKEND_LOOPER_RAW) != 0U)
            || (looper_storage_raw_export_is_active() != 0U))
    {
        g_pattern_load_state = PATTERN_LOAD_ERROR;
        g_pattern_load_last_error = PATTERN_LOAD_ERR_RECORD_ACTIVE;
        return 0U;
    }

    if ((g_pattern_load_state == PATTERN_LOAD_READY)
        && (g_pattern_load_bank == bank)
        && (g_pattern_load_pattern == pattern))
    {
        return 1U;
    }

    g_pattern_load_bank = bank;
    g_pattern_load_pattern = pattern;
    g_pattern_load_last_error = 0U;
    memset(&g_pattern_load_ready, 0, sizeof(g_pattern_load_ready));

    const uint8_t has_snapshot = pattern_sd_bank_slot_has_data(bank, pattern);
    g_pattern_slot_meta[bank][pattern].has_snapshot = has_snapshot;
    if (has_snapshot == 0U)
    {
        memcpy(&g_pattern_load_ready, &g_boot_pattern, sizeof(g_pattern_load_ready));
        g_pattern_load_state = PATTERN_LOAD_READY;
        return 1U;
    }

    g_pattern_load_state = PATTERN_LOAD_REQUESTED;
    return 1U;
}

void pattern_load_service(uint32_t byte_budget)
{
    if ((g_pattern_load_state != PATTERN_LOAD_REQUESTED)
        && (g_pattern_load_state != PATTERN_LOAD_LOADING))
    {
        return;
    }

    if (byte_budget == 0U)
    {
        return;
    }

    if ((multi_record_writer_any_active_backend(MULTI_RECORD_WRITER_BACKEND_LOOPER_RAW) != 0U)
            || (looper_storage_raw_export_is_active() != 0U))
    {
        g_pattern_load_state = PATTERN_LOAD_ERROR;
        g_pattern_load_last_error = PATTERN_LOAD_ERR_RECORD_ACTIVE;
        return;
    }

    g_pattern_load_state = PATTERN_LOAD_LOADING;

    if(sd_preview_is_active() != 0U)
    {
        sd_preview_stop();
    }

    if (pattern_sd_bank_load_slot(g_pattern_load_bank, g_pattern_load_pattern, &g_pattern_load_ready) == 0U)
    {
        if (pattern_sd_bank_slot_has_data(g_pattern_load_bank, g_pattern_load_pattern) != 0U)
        {
            g_pattern_load_state = PATTERN_LOAD_ERROR;
            g_pattern_load_last_error = PATTERN_LOAD_ERR_SD_LOAD;
            return;
        }

        memcpy(&g_pattern_load_ready, &g_boot_pattern, sizeof(g_pattern_load_ready));
    }

    g_pattern_load_state = PATTERN_LOAD_READY;
    g_pattern_load_last_error = 0U;
}

uint8_t pattern_load_is_pending(void)
{
    return ((g_pattern_load_state == PATTERN_LOAD_REQUESTED)
            || (g_pattern_load_state == PATTERN_LOAD_LOADING)) ? 1U : 0U;
}

uint8_t pattern_load_is_ready(uint8_t *out_bank, uint8_t *out_pattern)
{
    if (g_pattern_load_state != PATTERN_LOAD_READY)
    {
        return 0U;
    }

    if (out_bank != 0)
    {
        *out_bank = g_pattern_load_bank;
    }
    if (out_pattern != 0)
    {
        *out_pattern = g_pattern_load_pattern;
    }
    return 1U;
}

uint8_t pattern_load_take_ready(uint8_t *out_bank, uint8_t *out_pattern, PatternSaveV1 *out_snapshot)
{
    if ((out_snapshot == 0) || (g_pattern_load_state != PATTERN_LOAD_READY))
    {
        return 0U;
    }

    if (out_bank != 0)
    {
        *out_bank = g_pattern_load_bank;
    }
    if (out_pattern != 0)
    {
        *out_pattern = g_pattern_load_pattern;
    }
    memcpy(out_snapshot, &g_pattern_load_ready, sizeof(*out_snapshot));
    g_pattern_load_state = PATTERN_LOAD_IDLE;
    g_pattern_load_last_error = 0U;
    return 1U;
}

void pattern_load_cancel(void)
{
    g_pattern_load_state = PATTERN_LOAD_IDLE;
    g_pattern_load_bank = 0U;
    g_pattern_load_pattern = 0U;
    g_pattern_load_last_error = 0U;
    memset(&g_pattern_load_ready, 0, sizeof(g_pattern_load_ready));
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

    if ((multi_record_writer_any_active() != 0U)
            || (looper_storage_raw_export_is_active() != 0U))
    {
        /* TODO pending budgeted pattern save: defer the SD store instead of blocking record drain. */
        return 0U;
    }

    if (pattern_sd_bank_store_slot(bank, pattern, &g_current_pattern) == 0U)
    {
        return 0U;
    }

    g_pattern_slot_meta[bank][pattern].has_snapshot = 1U;

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

    if (pattern_load_request(bank, pattern) == 0U)
    {
        return 0U;
    }

    uint8_t boundary_track = ui_get_active_track();
    if (boundary_track >= SEQ_TRACK_COUNT)
    {
        boundary_track = 0U;
    }
    uint32_t boundary_generation = 0U;
    (void)seq_runtime_get_track_loop_generation(boundary_track, &boundary_generation);

    if (seq_runtime_is_running() == 0U)
    {
        uint8_t ready_bank = 0U;
        uint8_t ready_pattern = 0U;
        if ((pattern_load_is_ready(&ready_bank, &ready_pattern) == 0U)
            || (ready_bank != bank)
            || (ready_pattern != pattern)
            || (pattern_load_take_ready(&ready_bank, &ready_pattern, &g_next_pattern) == 0U))
        {
            g_pending_queue_valid = 1U;
            g_pending_queue_bank = bank;
            g_pending_queue_pattern = pattern;
            g_pending_boundary_track = boundary_track;
            g_pending_boundary_generation = boundary_generation;
            undo_v2_clear_all();
            return 1U;
        }

        if (pattern_live_apply_snapshot(&g_next_pattern, 0U) == 0U)
        {
            return 0U;
        }
        g_active_bank = bank;
        g_active_pattern = pattern;
        g_queued_valid = 0U;
        g_pending_queue_valid = 0U;
        g_queued_boundary_track = 0U;
        g_queued_boundary_generation = 0U;
        undo_v2_clear_all();
        return 1U;
    }

    g_pending_queue_valid = 1U;
    g_pending_queue_bank = bank;
    g_pending_queue_pattern = pattern;
    g_pending_boundary_track = boundary_track;
    g_pending_boundary_generation = boundary_generation;

    uint8_t ready_bank = 0U;
    uint8_t ready_pattern = 0U;
    if ((pattern_load_is_ready(&ready_bank, &ready_pattern) != 0U)
        && (ready_bank == bank)
        && (ready_pattern == pattern)
        && (pattern_load_take_ready(&ready_bank, &ready_pattern, &g_next_pattern) != 0U))
    {
        (void)pattern_live_arm_ready_queue(bank,
                                           pattern,
                                           &g_next_pattern,
                                           boundary_track,
                                           boundary_generation);
    }
    undo_v2_clear_all();
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

static uint8_t pattern_live_try_take_pending_ready(void)
{
    if (g_pending_queue_valid == 0U)
    {
        return 0U;
    }

    uint8_t ready_bank = 0U;
    uint8_t ready_pattern = 0U;
    if (pattern_load_is_ready(&ready_bank, &ready_pattern) == 0U)
    {
        return 0U;
    }

    if ((ready_bank != g_pending_queue_bank) || (ready_pattern != g_pending_queue_pattern))
    {
        return 0U;
    }

    if (pattern_load_take_ready(&ready_bank, &ready_pattern, &g_next_pattern) == 0U)
    {
        return 0U;
    }

    if (seq_runtime_is_running() == 0U)
    {
        if (pattern_live_apply_snapshot(&g_next_pattern, 0U) == 0U)
        {
            return 0U;
        }

        g_active_bank = ready_bank;
        g_active_pattern = ready_pattern;
        g_queued_valid = 0U;
        g_pending_queue_valid = 0U;
        g_queued_boundary_track = 0U;
        g_queued_boundary_generation = 0U;
        undo_v2_clear_all();
        return 1U;
    }

    uint32_t current_generation = 0U;
    (void)seq_runtime_get_track_loop_generation(g_pending_boundary_track, &current_generation);
    return pattern_live_arm_ready_queue(ready_bank,
                                        ready_pattern,
                                        &g_next_pattern,
                                        g_pending_boundary_track,
                                        current_generation);
}

void pattern_live_service(void)
{
    if (g_apply_in_progress != 0U)
    {
        return;
    }

    (void)pattern_live_try_take_pending_ready();

    if ((g_queued_valid == 0U) || (seq_runtime_is_running() == 0U))
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
        g_active_bank = g_queued_bank;
        g_active_pattern = g_queued_pattern;
        g_queued_valid = 0U;
        g_queued_boundary_track = 0U;
        g_queued_boundary_generation = 0U;
        if ((g_pending_queue_valid != 0U)
            && (g_pending_queue_bank == g_active_bank)
            && (g_pending_queue_pattern == g_active_pattern))
        {
            g_pending_queue_valid = 0U;
        }
        undo_v2_clear_all();
    }
}

void pattern_live_init(void)
{
    memset(&g_current_pattern, 0, sizeof(g_current_pattern));
    memset(&g_next_pattern, 0, sizeof(g_next_pattern));
    memset(&g_boot_pattern, 0, sizeof(g_boot_pattern));
    memset(&g_pattern_load_ready, 0, sizeof(g_pattern_load_ready));
    memset(&g_pattern_slot_meta, 0, sizeof(g_pattern_slot_meta));
    g_active_bank = 0U;
    g_active_pattern = 0U;
    g_queued_valid = 0U;
    g_queued_bank = 0U;
    g_queued_pattern = 0U;
    g_queued_boundary_track = 0U;
    g_queued_boundary_generation = 0U;
    g_pending_queue_valid = 0U;
    g_pending_queue_bank = 0U;
    g_pending_queue_pattern = 0U;
    g_pending_boundary_track = 0U;
    g_pending_boundary_generation = 0U;
    g_apply_in_progress = 0U;
    g_pattern_load_state = PATTERN_LOAD_IDLE;
    g_pattern_load_bank = 0U;
    g_pattern_load_pattern = 0U;
    g_pattern_load_last_error = 0U;

    if (pattern_live_capture_current(&g_boot_pattern) != 0U)
    {
        memcpy(&g_current_pattern, &g_boot_pattern, sizeof(g_current_pattern));
        memcpy(&g_next_pattern, &g_boot_pattern, sizeof(g_next_pattern));
        pattern_sd_bank_init();
    }
    else
    {
        pattern_sd_bank_init();
    }

    for (uint8_t bank = 0U; bank < PATTERN_BANK_COUNT; ++bank)
    {
        for (uint8_t pattern = 0U; pattern < PATTERN_PER_BANK; ++pattern)
        {
            g_pattern_slot_meta[bank][pattern].has_snapshot = pattern_sd_bank_slot_has_data(bank, pattern);
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
        uint8_t boundary_track = ui_get_active_track();
        if (boundary_track >= SEQ_TRACK_COUNT)
        {
            boundary_track = 0U;
        }
        g_queued_valid = 0U;
        g_queued_boundary_track = 0U;
        g_queued_boundary_generation = 0U;
        if (pattern_load_request(queued_bank, queued_pattern) != 0U)
        {
            g_pending_queue_valid = 1U;
            g_pending_queue_bank = queued_bank;
            g_pending_queue_pattern = queued_pattern;
            g_pending_boundary_track = boundary_track;
            (void)seq_runtime_get_track_loop_generation(boundary_track, &g_pending_boundary_generation);
        }
    }
    else
    {
        g_queued_valid = 0U;
        g_queued_boundary_track = 0U;
        g_queued_boundary_generation = 0U;
        g_pending_queue_valid = 0U;
    }
}

uint8_t pattern_live_is_apply_in_progress(void)
{
    return g_apply_in_progress;
}

uint8_t pattern_live_get_active_linked_kit(uint16_t *out_slot)
{
    if ((g_current_pattern.globals.linked_kit_valid == 0U)
            || (g_current_pattern.globals.linked_kit_slot >= KIT_V1_SLOT_COUNT))
    {
        return 0U;
    }

    if (out_slot != 0)
    {
        *out_slot = g_current_pattern.globals.linked_kit_slot;
    }
    return 1U;
}

uint8_t pattern_live_link_active_kit(uint16_t slot)
{
    if (slot >= KIT_V1_SLOT_COUNT)
    {
        return 0U;
    }

    g_current_pattern.globals.linked_kit_valid = 1U;
    g_current_pattern.globals.linked_kit_slot = slot;
    if ((g_queued_valid != 0U)
            && (g_queued_bank == g_active_bank)
            && (g_queued_pattern == g_active_pattern))
    {
        g_next_pattern.globals.linked_kit_valid = 1U;
        g_next_pattern.globals.linked_kit_slot = slot;
    }
    return 1U;
}

void pattern_live_clear_active_kit_link_if_slot(uint16_t slot)
{
    if ((slot >= KIT_V1_SLOT_COUNT)
            || (g_current_pattern.globals.linked_kit_valid == 0U)
            || (g_current_pattern.globals.linked_kit_slot != slot))
    {
        return;
    }

    g_current_pattern.globals.linked_kit_valid = 0U;
    g_current_pattern.globals.linked_kit_slot = KIT_V1_INVALID_SLOT;
}
