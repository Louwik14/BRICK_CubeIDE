#include "Storage/pattern_live_ram.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Core/engine_tasklet.h"
#include "Core/track_runtime.h"
#include "UI/ui_core.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_output_guard.h"
#include "Seq/seq_param_iface.h"
#include "Param/param_registry.h"

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
static uint8_t g_apply_in_progress;
static uint8_t g_last_playhead_valid;
static uint8_t g_last_playhead_step;
static uint8_t pattern_live_slot_is_valid(uint8_t bank, uint8_t pattern);

static uint8_t pattern_live_load_slot_into_next(uint8_t bank, uint8_t pattern)
{
    if (pattern_live_slot_is_valid(bank, pattern) == 0U)
    {
        return 0U;
    }

    /*
     * Backend SD non branché dans cette passe :
     * - si le slot demandé est le pattern actif, on duplique le snapshot live courant
     * - sinon on utilise le snapshot boot comme contenu de repli
     *
     * L'architecture reste alignée backend cible:
     * slot persistent (SD) -> next_pattern RAM -> apply quantifié.
     */
    if ((bank == g_active_bank) && (pattern == g_active_pattern))
    {
        memcpy(&g_next_pattern, &g_current_pattern, sizeof(g_next_pattern));
    }
    else
    {
        memcpy(&g_next_pattern, &g_boot_pattern, sizeof(g_next_pattern));
    }

    return 1U;
}

static uint8_t pattern_live_slot_is_valid(uint8_t bank, uint8_t pattern)
{
    return (bank < PATTERN_BANK_COUNT) && (pattern < PATTERN_PER_BANK);
}

static uint8_t pattern_live_is_param_in_sound_domain(param_id_t id)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    return (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_COLORS)
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY);
}

static uint8_t pattern_live_is_param_in_mix_domain(param_id_t id)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    return (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX);
}

static uint8_t pattern_live_is_global_param_useful(param_id_t id)
{
    if ((id == PARAM_MASTER_GAIN) || (id == PARAM_CFG_TRACK) || (id == PARAM_CFG_TRACK_TYPE)
        || (id == PARAM_CFG_MIDI_CH) || (id == PARAM_CFG_MIDI_SRC))
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
        case PARAM_MIX_TRACK0_GAIN:
        case PARAM_MIX_TRACK1_GAIN:
        case PARAM_MIX_TRACK2_GAIN:
        case PARAM_MIX_TRACK3_GAIN:
        case PARAM_MIX_TRACK0_PAN:
        case PARAM_MIX_TRACK1_PAN:
        case PARAM_MIX_TRACK2_PAN:
        case PARAM_MIX_TRACK3_PAN:
        case PARAM_MIX_TRACK0_MUTE:
        case PARAM_MIX_TRACK1_MUTE:
        case PARAM_MIX_TRACK2_MUTE:
        case PARAM_MIX_TRACK3_MUTE:
        case PARAM_MIX_TRACK0_ROUTE:
        case PARAM_MIX_TRACK1_ROUTE:
        case PARAM_MIX_TRACK2_ROUTE:
        case PARAM_MIX_TRACK3_ROUTE:
        case PARAM_MIX_TRACK0_INSERT0:
        case PARAM_MIX_TRACK0_INSERT1:
        case PARAM_MIX_TRACK1_INSERT0:
        case PARAM_MIX_TRACK1_INSERT1:
        case PARAM_MIX_TRACK2_INSERT0:
        case PARAM_MIX_TRACK2_INSERT1:
        case PARAM_MIX_TRACK3_INSERT0:
        case PARAM_MIX_TRACK3_INSERT1:
        case PARAM_MIX_TRACK0_SEND0:
        case PARAM_MIX_TRACK0_SEND1:
        case PARAM_MIX_TRACK1_SEND0:
        case PARAM_MIX_TRACK1_SEND1:
        case PARAM_MIX_TRACK2_SEND0:
        case PARAM_MIX_TRACK2_SEND1:
        case PARAM_MIX_TRACK3_SEND0:
        case PARAM_MIX_TRACK3_SEND1:
        case PARAM_MIX_SEND0_FX:
        case PARAM_MIX_SEND1_FX:
        case PARAM_MIX_LEVEL:
        case PARAM_MIX_PAN:
        case PARAM_MIX_SEND1:
        case PARAM_MIX_SEND2:
            return 1U;

        default:
            return 0U;
    }
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
                out_step->locks[i].value16 = entry.value16;
                out_step->locks[i].flags = entry.flags;
            }
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
        uint8_t quant = 1U;
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
                (void)seq_model_step_plock_upsert(track,
                                                  step,
                                                  pl->set_id,
                                                  pl->param8,
                                                  pl->value16,
                                                  pl->flags);
            }
        }
    }

    return 1U;
}

uint8_t pattern_live_apply_snapshot(const PatternSaveV1 *pattern, uint8_t resume_transport)
{
    if ((pattern == 0) || (g_apply_in_progress != 0U))
    {
        return 0U;
    }

    g_apply_in_progress = 1U;

    const uint8_t was_running = seq_runtime_is_running();
    seq_runtime_stop();
    seq_output_guard_panic(1U);

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        (void)ui_set_track_family(track, (ui_track_family_t)pattern->track_cfg.family[track]);
        (void)ui_set_track_type(track, (ui_track_type_t)pattern->track_cfg.type[track]);
        (void)ui_set_track_midi_channel(track, pattern->track_cfg.midi_channel[track]);
        (void)ui_set_track_midi_source(track, (ui_track_midi_source_t)pattern->track_cfg.midi_source[track]);
    }

    track_runtime_refresh_all();

    for (uint16_t id_raw = 0U; id_raw < (uint16_t)PARAM_COUNT; ++id_raw)
    {
        const param_id_t id = (param_id_t)id_raw;

        for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
        {
            if (pattern->sound.track_valid[track][id] != 0U)
            {
                (void)param_registry_apply_track_value(id, track, pattern->sound.track_values[track][id]);
            }

            if (pattern->mix.track_valid[track][id] != 0U)
            {
                (void)param_registry_apply_track_value(id, track, pattern->mix.track_values[track][id]);
            }
        }

        if (pattern->globals.global_valid[id] != 0U)
        {
            param_set(id, pattern->globals.global_values[id]);
        }
    }

    seq_runtime_set_tempo_bpm_milli(pattern->globals.tempo_bpm_milli);
    seq_runtime_set_clock_source((seq_clock_src_t)pattern->globals.clock_src);
    seq_runtime_set_rec_count_in_mode(pattern->globals.rec_count_in_mode);
    seq_runtime_set_rec_len_mode(pattern->globals.rec_len_mode);

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        seq_runtime_set_track_div(track, pattern->globals.track_div[track]);
        seq_runtime_set_track_quant(track, pattern->globals.track_quant[track]);
        seq_runtime_set_track_swing(track, pattern->globals.track_swing[track]);
    }

    (void)pattern_live_apply_seq_block(&pattern->seq);

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        (void)seq_runtime_set_playhead_step(track, 0U);
    }

    param_registry_sync_ui_for_active_track();

    if ((resume_transport != 0U) && (was_running != 0U))
    {
        seq_runtime_start();
    }

    g_last_playhead_valid = 0U;
    g_apply_in_progress = 0U;
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
    if ((pattern_live_slot_is_valid(bank, pattern) == 0U)
        || (g_pattern_slot_meta[bank][pattern].has_snapshot == 0U))
    {
        return 0U;
    }

    if (pattern_live_load_slot_into_next(bank, pattern) == 0U)
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
        return 1U;
    }

    g_queued_bank = bank;
    g_queued_pattern = pattern;
    g_queued_valid = 1U;
    return 1U;
}

void pattern_live_service(void)
{
    if ((g_queued_valid == 0U) || (seq_runtime_is_running() == 0U) || (g_apply_in_progress != 0U))
    {
        return;
    }

    seq_step_id_t playhead = 0U;
    if (seq_runtime_get_playhead_step(ui_get_active_track(), &playhead) == 0U)
    {
        return;
    }

    if (g_last_playhead_valid == 0U)
    {
        g_last_playhead_step = playhead;
        g_last_playhead_valid = 1U;
        return;
    }

    if ((playhead == 0U) && (g_last_playhead_step != 0U))
    {
        if (pattern_live_apply_snapshot(&g_next_pattern, 1U) != 0U)
        {
            memcpy(&g_current_pattern, &g_next_pattern, sizeof(g_current_pattern));
            g_active_bank = g_queued_bank;
            g_active_pattern = g_queued_pattern;
            g_queued_valid = 0U;
        }
    }

    g_last_playhead_step = playhead;
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
    g_apply_in_progress = 0U;
    g_last_playhead_valid = 0U;
    g_last_playhead_step = 0U;

    if (pattern_live_capture_current(&g_boot_pattern) != 0U)
    {
        memcpy(&g_current_pattern, &g_boot_pattern, sizeof(g_current_pattern));
        memcpy(&g_next_pattern, &g_boot_pattern, sizeof(g_next_pattern));

        for (uint8_t bank = 0U; bank < PATTERN_BANK_COUNT; ++bank)
        {
            for (uint8_t pattern = 0U; pattern < PATTERN_PER_BANK; ++pattern)
            {
                g_pattern_slot_meta[bank][pattern].has_snapshot = 1U;
                g_pattern_slot_meta[bank][pattern].dirty_pending_persist = 0U;
            }
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

uint8_t pattern_live_is_apply_in_progress(void)
{
    return g_apply_in_progress;
}
