#include "Seq/seq_engine.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_param_iface.h"
#include "Param/param_registry.h"
#include "Param/param_value_policy.h"
#include "NoteFx/note_fx_state.h"
#include "Platform/brick_media_clock.h"
#include "Platform/memory_layout.h"
#include "Track/entity_topology.h"
#include "Track/polyphony_control.h"
#include "Track/track_mute.h"
#include "Track/track_runtime.h"
#include "Keyboard/keyboard_params.h"
#include "stm32h7xx.h"
#include <string.h>

/* Single CONTROL writer. Published slots are never modified while SEQ reads. */
static seq_pattern_t g_pattern_slot_a;
static SEQ_STATE_D2 seq_pattern_t g_pattern_slot_b;
static seq_lock_pattern_t g_locks_a_d1[4][SEQ_ENGINE_LOCK_POOL_CAPACITY];
static CONTROL_M4_SRAM2 seq_lock_pattern_t g_locks_a_sram2[7][SEQ_ENGINE_LOCK_POOL_CAPACITY];
static D3_IPC seq_lock_pattern_t g_locks_a_d3[5][SEQ_ENGINE_LOCK_POOL_CAPACITY];
static SEQ_STATE_D2 seq_lock_pattern_t g_locks_b[SEQ_LANE_CAPACITY][SEQ_ENGINE_LOCK_POOL_CAPACITY];
static seq_pattern_t *const g_pattern[SEQ_ENGINE_SNAPSHOT_SLOTS] = {
    &g_pattern_slot_a, &g_pattern_slot_b
};
static volatile uint32_t g_edit_generation;
static volatile uint32_t g_published_generation;
static volatile uint8_t g_published_slot;
static uint8_t g_build_slot;
static uint8_t g_build_track;
static uint8_t g_build_step;
static uint32_t g_build_generation;
static uint8_t g_last_running;
static uint8_t g_seen_runtime_running;
static uint32_t g_transport_epoch;
static seq_runtime_shadow_seed_t g_build_seed;
static note_fx_track_state_t g_build_fx_state[SEQ_LANE_CAPACITY];
static uint8_t seq_engine_compile_step_fx(seq_pattern_t *pattern,
                                          uint8_t track, uint8_t step)
{
    note_fx_track_state_t effective = g_build_fx_state[track];
    seq_step_pattern_t *const target = &pattern->steps[track][step];
    uint16_t override_mask = 0U;
    const uint16_t first = pattern->lock_first[track][step];
    const uint8_t count = pattern->steps[track][step].lock_count;
    for (uint8_t i = 0U; i < count; ++i)
    {
        const seq_lock_pattern_t *const lock =
            &pattern->lock_pool[track][first + i];
        if ((lock->param_flags & SEQ_ENGINE_PARAM_FLAG_NOTE_FX) == 0U)
            continue;
        const uint8_t slot = (uint8_t)(lock->base_value16 >> 8U);
        const uint8_t param = (uint8_t)lock->base_value16;
        if ((slot >= NOTE_FX_SLOT_COUNT) || (param >= NOTE_FX_PARAM_COUNT))
            return UINT8_MAX;
        effective.value[slot][param] = (uint8_t)lock->value16;
        override_mask |= (uint16_t)(1U <<
            ((uint16_t)slot * NOTE_FX_PARAM_COUNT + param));
    }
    if ((note_fx_state_normalize_track(&effective) == 0U)
            || (note_fx_state_validate_unique_families(&effective) == 0U))
        return UINT8_MAX;
    note_fx_compiled_plan_t compiled;
    if (note_fx_plan_compile(&effective, override_mask, &compiled) == 0U)
        return UINT8_MAX;
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        if (note_fx_plan_model(compiled.slot[slot]) == NOTE_FX_MODEL_GROOVE)
            pattern->track_exec[track].max_negative_horizon_q16 =
                (uint16_t)((UINT32_C(1) << 16U)
                    / SEQ_GROOVE_NEGATIVE_HORIZON_DENOMINATOR + 1U);
    }

    seq_lock_pattern_t compact[SEQ_STEP_MAX_LOCKS];
    uint8_t compact_count = 0U;
    for (uint8_t i = 0U; i < count; ++i)
    {
        const seq_lock_pattern_t lock = pattern->lock_pool[track][first + i];
        if ((lock.param_flags & SEQ_ENGINE_PARAM_FLAG_NOTE_FX) == 0U)
            compact[compact_count++] = lock;
    }
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        const uint8_t slot_override = (uint8_t)(
            (override_mask >> (slot * NOTE_FX_PARAM_COUNT)) & 0x0FU);
        if (slot_override == 0U) continue;
        const uint32_t word = compiled.slot[slot];
        compact[compact_count++] = (seq_lock_pattern_t){
            .param_flags = (uint16_t)(SEQ_ENGINE_PARAM_FLAG_NOTE_FX | slot
                | ((uint16_t)slot_override
                    << SEQ_ENGINE_FX_PLAN_OVERRIDE_SHIFT)),
            .value16 = (uint16_t)word,
            .base_value16 = (uint16_t)(word >> 16U)
        };
    }
    memcpy(&pattern->lock_pool[track][first], compact,
           (size_t)compact_count * sizeof(compact[0]));
    pattern->lock_pool_count[track] = (uint16_t)(first + compact_count);
    target->lock_count = compact_count;
    return 0U;
}

const seq_pattern_t *seq_engine_pattern_capture(void)
{
    const uint8_t slot = g_published_slot;
    __DMB();
    return (g_published_generation != 0U) ? g_pattern[slot] : 0;
}

void seq_engine_control_init(void)
{
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track) {
        g_pattern_slot_b.lock_pool[track] = g_locks_b[track];
        if (track < 4U) g_pattern_slot_a.lock_pool[track] = g_locks_a_d1[track];
        else if (track < 11U) g_pattern_slot_a.lock_pool[track] = g_locks_a_sram2[track - 4U];
        else g_pattern_slot_a.lock_pool[track] = g_locks_a_d3[track - 11U];
    }
    g_edit_generation = 1U;
    g_published_generation = 0U;
    g_published_slot = 0U;
    g_build_slot = 1U;
    g_build_track = 0U;
    g_build_step = 0U;
    g_build_generation = 1U;
    g_last_running = 0U;
    g_seen_runtime_running = seq_runtime_is_running();
    g_transport_epoch = 1U;
    memset(&g_build_seed, 0, sizeof(g_build_seed));
}

void seq_engine_control_mark_dirty(void)
{
    ++g_edit_generation;
    if (g_edit_generation == 0U) g_edit_generation = 1U;
}

static void seq_engine_capture_step(seq_pattern_t *pattern,
                                      seq_track_id_t track, seq_step_id_t step)
{
    if (step == 0U)
    {
        memset(&g_build_fx_state[track], 0, sizeof(g_build_fx_state[track]));
        uint8_t div = 1U, swing = 0U, quant = 0U;
        (void)seq_runtime_get_track_div(track, &div);
        (void)seq_runtime_get_track_swing(track, &swing);
        (void)seq_runtime_get_track_quant(track, &quant);
        pattern->track_div[track] = div;
        pattern->track_swing[track] = swing;
        pattern->track_quant[track] = quant;
        pattern->track_muted[track] =
            (track_mute_should_suppress_note_on(track) > 0) ? 1U : 0U;
        entity_topology_descriptor_t entity;
        const uint8_t topology_valid = entity_topology_get(track, &entity);
        const uint16_t capabilities = topology_valid
            ? entity_topology_get_capabilities(&entity) : 0U;
        track_runtime_descriptor_t runtime_descriptor;
        const uint8_t runtime_valid = track_runtime_get_descriptor(track,
            &runtime_descriptor);
        uint8_t logical_capacity = 0U;
        if ((capabilities & TRACK_CAPABILITY_NOTES) != 0U)
        {
            if (entity.role == ENTITY_ROLE_GROUP_CHILD)
                logical_capacity = SEQ_LOGICAL_CAPACITY_GROUP_CHILD;
            else if (entity.role != ENTITY_ROLE_GROUP_MASTER)
                logical_capacity = seq_model_play_capacity(track);
            if ((entity.role != ENTITY_ROLE_GROUP_CHILD)
                    && (entity.role != ENTITY_ROLE_GROUP_MASTER)
                    && (runtime_valid != 0U)
                    && (track_runtime_has_configurable_polyphony(
                        runtime_descriptor.family, runtime_descriptor.type) != 0U))
                logical_capacity = track_runtime_effective_voice_count(
                    runtime_descriptor.family, runtime_descriptor.type,
                    polyphony_control_get_voice_count(track));
            if (logical_capacity > SEQ_LOGICAL_CAPACITY_MAX)
                logical_capacity = SEQ_LOGICAL_CAPACITY_MAX;
        }
        pattern->track_exec[track] = (seq_track_exec_t){
            .capabilities = capabilities,
            .logical_capacity = logical_capacity,
            .role = topology_valid ? (uint8_t)entity.role : 0U,
            .type = runtime_valid ? (uint8_t)runtime_descriptor.type : 0U,
            .destination = track,
            .div = div,
            .swing = swing,
            .quant = quant,
            .muted = pattern->track_muted[track],
            .active = topology_valid ? entity.active : 0U
        };
        pattern->track_can_emit[track] =
            ((topology_valid != 0U)
                && (entity_topology_can_emit_notes(&entity) != 0U)
                && (track_runtime_has_capability(track,
                    TRACK_CAPABILITY_NOTES) != 0U)) ? 1U : 0U;
        note_fx_track_state_t fx_state;
        pattern->track_note_enabled[track] = 0U;
        pattern->track_lock_enabled[track] = 1U;
        pattern->track_fx_enabled[track] = 0U;
        if (note_fx_state_capture_track(track, &fx_state) != 0U)
        {
            pattern->track_note_enabled[track] =
                ((capabilities & TRACK_CAPABILITY_NOTES) != 0U) ? 1U : 0U;
            pattern->track_fx_enabled[track] =
                ((capabilities & TRACK_CAPABILITY_MIDI_FX) != 0U) ? 1U : 0U;
            if (pattern->track_fx_enabled[track] == 0U)
                memset(&fx_state, 0, sizeof(fx_state));
            g_build_fx_state[track] = fx_state;
            if (note_fx_plan_compile(&fx_state, 0U,
                    &pattern->fx_base_plan[track]) == 0U)
            {
                memset(&pattern->fx_base_plan[track], 0,
                       sizeof(pattern->fx_base_plan[track]));
                pattern->track_fx_enabled[track] = 0U;
            }
        }
        if ((runtime_valid == 0U)
                || (runtime_descriptor.family == TRACK_RUNTIME_FAMILY_MIDI)
                || (runtime_descriptor.family == TRACK_RUNTIME_FAMILY_EXTERNAL)
                || (seq_runtime_get_clock_source() != SEQ_CLOCK_SRC_INTERNAL))
            pattern->track_note_enabled[track] = 0U;
        pattern->track_length[track] = seq_model_get_track_playback_length(track);
        (void)seq_model_play_base_capture(track, &pattern->play_base[track]);
    }
        seq_step_pattern_t *const target = &pattern->steps[track][step];
        target->trig_roll = (uint8_t)((seq_model_get_trig(track, step) & 1U)
            | ((seq_model_get_step_roll(track, step) & 0x0FU) << 1U));
        target->lock_count = 0U;
        pattern->lock_first[track][step] = pattern->lock_pool_count[track];
        if (seq_model_step_is_active(track, step) != 0U)
        {
            const uint8_t count = seq_model_step_param_plock_count(track, step);
            for (uint8_t i = 0U; i < count; ++i)
            {
                seq_plock_entry_t entry;
                param_id_t param;
                seq_value16_t base;
                if (pattern->lock_pool_count[track]
                        >= SEQ_ENGINE_LOCK_POOL_CAPACITY)
                {
                    break;
                }
                if ((seq_model_step_param_plock_get_at(track, step, i,
                            &entry) == 0U)
                        || (seq_param_iface_slot_to_param(track, entry.set_id,
                            entry.param_slot, &param) == 0U)
                        || (param >= PARAM_COUNT)
                        || (seq_param_iface_get_base_value(track, entry.set_id,
                            entry.param_slot, &base) == 0U))
                {
                    pattern->track_lock_enabled[track] = 0U;
                    break;
                }
                const uint8_t is_fx=(entry.set_id
                    ==(uint8_t)SEQ_PLOCK_SET_MIDI_FX)?1U:0U;
                if ((is_fx==0U)&&(param_registry_track_temp_is_applicable(
                        param,track)==0U))
                {
                    pattern->track_lock_enabled[track]=0U;
                    break;
                }
                const uint16_t projected_value=is_fx
                    ?(uint16_t)(param_value_policy_decode_u16(
                        &param_registry[param],entry.value16)+0.5f)
                    :entry.value16;
                uint16_t projected_base=base;
                if(is_fx!=0U){
                    uint8_t fx_slot=0U,fx_param=0U;
                    if(note_fx_state_param_map(param,&fx_slot,&fx_param)==0U){
                        pattern->track_fx_enabled[track]=0U;break;}
                    projected_base=(uint16_t)(((uint16_t)fx_slot<<8U)|fx_param);
                }
                pattern->lock_pool[track][pattern->lock_pool_count[track]++] =
                    (seq_lock_pattern_t){
                        .param_flags=(uint16_t)param
                            | (is_fx?SEQ_ENGINE_PARAM_FLAG_NOTE_FX:0U)
                            | ((param_registry_temp_is_clearable(param) != 0U)
                                ? SEQ_ENGINE_PARAM_FLAG_CLEARABLE : 0U),
                        .value16=projected_value,.base_value16=projected_base};
                ++target->lock_count;
            }
            /* One traversal in SEQ: canonical step locks are sorted once while
             * CONTROL owns the prepared Pattern. */
            for (uint8_t i = 1U; i < target->lock_count; ++i)
            {
                const seq_lock_pattern_t key =
                    pattern->lock_pool[track][pattern->lock_first[track][step] + i];
                uint8_t j = i;
                while ((j != 0U)
                        && ((pattern->lock_pool[track][
                            pattern->lock_first[track][step] + j - 1U].param_flags
                            & SEQ_ENGINE_PARAM_ID_MASK)
                            > (key.param_flags & SEQ_ENGINE_PARAM_ID_MASK)))
                {
                    pattern->lock_pool[track][pattern->lock_first[track][step] + j] =
                        pattern->lock_pool[track][
                            pattern->lock_first[track][step] + j - 1U];
                    --j;
                }
                pattern->lock_pool[track][pattern->lock_first[track][step] + j] = key;
            }
        }
        seq_play_snapshot_t *const top = (track < BRICK_ENTITY_TOP_LEVEL_COUNT)
            ? &pattern->top_play[track][step] : 0;
        seq_play_item_t *const child = (track >= BRICK_ENTITY_TOP_LEVEL_COUNT)
            ? &pattern->child_play[track - BRICK_ENTITY_TOP_LEVEL_COUNT][step] : 0;
        if (top != 0) memset(top, 0, sizeof(*top));
        if (child != 0) memset(child, 0, sizeof(*child));
        const uint8_t voice_count = (child != 0) ? 1U : SEQ_PLAY_MAX_CAPACITY;
        for (uint8_t voice = 0U; voice < voice_count; ++voice)
        {
            for (uint8_t field = 0U; field < SEQ_STEP_PLAY_FIELD_COUNT; ++field)
            {
                int16_t value;
                if (seq_model_play_get(track, step, voice,
                    (seq_step_play_field_t)field, &value) != 0U)
                {
                    if (top != 0)
                        (void)seq_play_snapshot_set(top, voice,
                            (seq_step_play_field_t)field, value);
                    else
                    {
                        seq_play_snapshot_t temporary = {0};
                        temporary.items[0] = *child;
                        (void)seq_play_snapshot_set(&temporary, 0U,
                            (seq_step_play_field_t)field, value);
                        *child = temporary.items[0];
                    }
                }
            }
        }
        if (seq_engine_compile_step_fx(pattern,
                (uint8_t)track, (uint8_t)step) == UINT8_MAX)
        {
            pattern->track_fx_enabled[track] = 0U;
        }
}

void seq_engine_control_poll(void)
{
    const uint8_t running = seq_runtime_is_running();
    uint8_t transport_changed = 0U;
    if (running != g_seen_runtime_running)
    {
        g_seen_runtime_running = running;
        transport_changed = 1U;
        seq_engine_control_mark_dirty();
    }
    const uint32_t current = g_edit_generation;
    if (((g_build_track != 0U) || (g_build_step != 0U))
            && (g_build_generation != current))
    {
        g_build_track = 0U;
        g_build_step = 0U;
        if (transport_changed == 0U) return;
    }
    if ((g_build_track == 0U) && (g_build_step == 0U))
    {
        if ((g_published_generation != 0U)
                && (current == g_published_generation)) return;
        g_build_generation = current;
        g_build_slot = (uint8_t)(g_published_slot ^ 1U);
        seq_runtime_capture_shadow_seed(&g_build_seed);
        if (g_build_seed.running != g_last_running)
        {
            ++g_transport_epoch;
            if (g_transport_epoch == 0U) g_transport_epoch = 1U;
            g_last_running = g_build_seed.running;
        }
        memset(g_pattern[g_build_slot]->lock_pool_count, 0,
               sizeof(g_pattern[g_build_slot]->lock_pool_count));
    }
    /* A transport edge must publish its immutable frontier before the first
     * step elapses. Ordinary edits remain cooperative. */
    const uint16_t work_limit = (transport_changed != 0U)
        ? (uint16_t)(SEQ_LANE_CAPACITY * SEQ_MAX_STEPS) : 4U;
    for (uint16_t work = 0U; work < work_limit; ++work)
    {
        seq_engine_capture_step(g_pattern[g_build_slot],
                                  g_build_track, g_build_step++);
        if (g_build_step < SEQ_MAX_STEPS) continue;
        g_build_step = 0U;
        if (++g_build_track == SEQ_LANE_CAPACITY) break;
    }
    if (g_build_track < SEQ_LANE_CAPACITY) return;
    g_build_track = 0U;
    g_build_step = 0U;
    if (g_edit_generation != g_build_generation) return;
    seq_pattern_t *const pattern = g_pattern[g_build_slot];
    pattern->running = g_build_seed.running;
    pattern->scale_index = keyboard_params_get_scale_index();
    pattern->root_index = keyboard_params_get_root_index();
    pattern->transport_epoch = g_transport_epoch;
    pattern->seed_step_sample_q16 = g_build_seed.step_sample_q16;
    pattern->samples_per_step_q16 = g_build_seed.samples_per_step_q16;
    memcpy(pattern->seed_play_step, g_build_seed.play_step,
           sizeof(pattern->seed_play_step));
    memcpy(pattern->seed_div_phase, g_build_seed.track_div_phase,
           sizeof(pattern->seed_div_phase));
    memcpy(pattern->seed_swing_phase, g_build_seed.track_swing_phase,
           sizeof(pattern->seed_swing_phase));
    pattern->generation = g_build_generation;
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
        pattern->track_note_enabled[track] &=
            pattern->track_lock_enabled[track];
    (void)brick_media_clock_now_sample(&pattern->effective_sample);
    __DMB();
    g_published_slot = g_build_slot;
    __DMB();
    g_published_generation = g_build_generation;
}

uint8_t seq_engine_control_flush(void)
{
    const uint32_t target_generation = g_edit_generation;
    const uint16_t pass_limit = (uint16_t)(
        (SEQ_LANE_CAPACITY * SEQ_MAX_STEPS + 3U) / 4U + 1U);
    for (uint16_t pass = 0U; pass < pass_limit; ++pass)
    {
        seq_engine_control_poll();
        if (g_published_generation == target_generation) return 1U;
        if (g_edit_generation != target_generation) return 0U;
    }
    return 0U;
}
