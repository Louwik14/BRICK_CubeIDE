#include "Seq/seq_rt_pass1.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_param_iface.h"
#include "Param/param_registry.h"
#include "Param/param_value_policy.h"
#include "NoteFx/note_fx_state.h"
#include "Platform/brick_media_clock.h"
#include "Platform/memory_layout.h"
#include "Track/entity_topology.h"
#include "Track/track_mute.h"
#include "Track/track_runtime.h"
#include "Keyboard/keyboard_params.h"
#include "stm32h7xx.h"
#include <string.h>

/* Single CONTROL writer. Published slots are never modified while SEQ reads. */
static seq_rt_projection_t g_projection_d1;
static SEQ_STATE_D2 seq_rt_projection_t g_projection_d2;
static seq_rt_projection_t *const g_projection[SEQ_RT_SNAPSHOT_SLOTS] = {
    &g_projection_d1, &g_projection_d2
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

const seq_rt_projection_t *seq_rt_pass1_projection_capture(void)
{
    const uint8_t slot = g_published_slot;
    __DMB();
    return (g_published_generation != 0U) ? g_projection[slot] : 0;
}

void seq_rt_pass1_control_init(void)
{
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
}

void seq_rt_pass1_control_mark_dirty(void)
{
    ++g_edit_generation;
    if (g_edit_generation == 0U) g_edit_generation = 1U;
}

static void seq_rt_pass1_capture_step(seq_rt_projection_t *projection,
                                      seq_track_id_t track, seq_step_id_t step)
{
    if (step == 0U)
    {
        uint8_t div = 1U, swing = 0U, quant = 0U;
        (void)seq_runtime_get_track_div(track, &div);
        (void)seq_runtime_get_track_swing(track, &swing);
        (void)seq_runtime_get_track_quant(track, &quant);
        projection->track_div[track] = div;
        projection->track_swing[track] = swing;
        projection->track_quant[track] = quant;
        projection->track_muted[track] =
            (track_mute_should_suppress_note_on(track) > 0) ? 1U : 0U;
        entity_topology_descriptor_t entity;
        projection->track_can_emit[track] =
            ((entity_topology_get(track, &entity) != 0U)
                && (entity_topology_can_emit_notes(&entity) != 0U)
                && (track_runtime_has_capability(track,
                    TRACK_CAPABILITY_NOTES) != 0U)) ? 1U : 0U;
        note_fx_track_state_t fx_state;
        projection->track_rt_note_direct[track] = 0U;
        projection->track_rt_plock_direct[track] = 1U;
        projection->track_rt_fx_direct[track] = 0U;
        if (note_fx_state_capture_track(track, &fx_state) != 0U)
        {
            projection->track_rt_note_direct[track] = 1U;
            projection->track_rt_fx_direct[track] = 1U;
            projection->note_fx[track] = fx_state;
        }
        track_runtime_descriptor_t runtime_descriptor;
        if ((track_runtime_get_descriptor(track, &runtime_descriptor) == 0U)
                || (runtime_descriptor.family == TRACK_RUNTIME_FAMILY_MIDI)
                || (runtime_descriptor.family == TRACK_RUNTIME_FAMILY_EXTERNAL)
                || (seq_runtime_get_clock_source() != SEQ_CLOCK_SRC_INTERNAL))
            projection->track_rt_note_direct[track] = 0U;
        projection->track_length[track] = seq_model_get_track_playback_length(track);
        (void)seq_model_play_base_capture(track, &projection->play_base[track]);
    }
        seq_rt_step_projection_t *const target = &projection->steps[track][step];
        target->trig_roll = (uint8_t)((seq_model_get_trig(track, step) & 1U)
            | ((seq_model_get_step_roll(track, step) & 0x0FU) << 1U));
        target->lock_count = 0U;
        projection->lock_first[track][step] = projection->lock_pool_count;
        if (seq_model_step_is_active(track, step) != 0U)
        {
            const uint8_t count = seq_model_step_param_plock_count(track, step);
            for (uint8_t i = 0U; i < count; ++i)
            {
                seq_plock_entry_t entry;
                param_id_t param;
                seq_value16_t base;
                if ((seq_model_step_param_plock_get_at(track, step, i,
                            &entry) == 0U)
                        || (seq_param_iface_slot_to_param(track, entry.set_id,
                            entry.param_slot, &param) == 0U)
                        || (param >= PARAM_COUNT)
                        || (seq_param_iface_get_base_value(track, entry.set_id,
                            entry.param_slot, &base) == 0U)
                        || (projection->lock_pool_count
                            >= SEQ_RT_LOCK_POOL_CAPACITY))
                {
                    projection->track_rt_plock_direct[track] = 0U;
                    break;
                }
                const uint8_t is_fx=(entry.set_id
                    ==(uint8_t)SEQ_PLOCK_SET_MIDI_FX)?1U:0U;
                if ((is_fx==0U)&&(param_registry_track_temp_is_applicable(
                        param,track)==0U))
                {
                    projection->track_rt_plock_direct[track]=0U;
                    break;
                }
                const uint16_t projected_value=is_fx
                    ?(uint16_t)(param_value_policy_decode_u16(
                        &param_registry[param],entry.value16)+0.5f)
                    :entry.value16;
                const uint16_t projected_base=is_fx
                    ?(uint16_t)(param_value_policy_decode_u16(
                        &param_registry[param],base)+0.5f):base;
                projection->lock_pool[projection->lock_pool_count++] =
                    (seq_rt_lock_projection_t){
                        .param_flags=(uint16_t)param
                            | (is_fx?SEQ_RT_PARAM_FLAG_NOTE_FX:0U)
                            | ((param_registry_temp_is_clearable(param) != 0U)
                                ? SEQ_RT_PARAM_FLAG_CLEARABLE : 0U),
                        .value16=projected_value,.base_value16=projected_base};
                ++target->lock_count;
            }
        }
        seq_play_snapshot_t *const top = (track < BRICK_ENTITY_TOP_LEVEL_COUNT)
            ? &projection->top_play[track][step] : 0;
        seq_play_item_t *const child = (track >= BRICK_ENTITY_TOP_LEVEL_COUNT)
            ? &projection->child_play[track - BRICK_ENTITY_TOP_LEVEL_COUNT][step] : 0;
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
}

void seq_rt_pass1_control_poll(void)
{
    const uint8_t running = seq_runtime_is_running();
    if (running != g_seen_runtime_running)
    {
        g_seen_runtime_running = running;
        seq_rt_pass1_control_mark_dirty();
    }
    const uint32_t current = g_edit_generation;
    if ((g_build_track == 0U) && (g_build_step == 0U))
    {
        if ((g_published_generation != 0U)
                && (current == g_published_generation)) return;
        g_build_generation = current;
        g_build_slot = (uint8_t)(g_published_slot ^ 1U);
        g_projection[g_build_slot]->lock_pool_count = 0U;
    }
    if (g_build_generation != current)
    {
        g_build_track = 0U;
        g_build_step = 0U;
        return;
    }
    /* Four steps per CONTROL pass keeps capture cooperative. */
    for (uint8_t work = 0U; work < 4U; ++work)
    {
        seq_rt_pass1_capture_step(g_projection[g_build_slot],
                                  g_build_track, g_build_step++);
        if (g_build_step < SEQ_MAX_STEPS) continue;
        g_build_step = 0U;
        if (++g_build_track == SEQ_LANE_CAPACITY) break;
    }
    if (g_build_track < SEQ_LANE_CAPACITY) return;
    g_build_track = 0U;
    g_build_step = 0U;
    if (g_edit_generation != g_build_generation) return;
    seq_rt_projection_t *const projection = g_projection[g_build_slot];
    seq_runtime_shadow_seed_t seed;
    seq_runtime_capture_shadow_seed(&seed);
    if (seed.running != g_last_running)
    {
        ++g_transport_epoch;
        if (g_transport_epoch == 0U) g_transport_epoch = 1U;
        g_last_running = seed.running;
    }
    projection->running = seed.running;
    projection->scale_index = keyboard_params_get_scale_index();
    projection->root_index = keyboard_params_get_root_index();
    projection->transport_epoch = g_transport_epoch;
    projection->seed_step_sample_q16 = seed.step_sample_q16;
    projection->samples_per_step_q16 = seed.samples_per_step_q16;
    memcpy(projection->seed_play_step, seed.play_step,
           sizeof(projection->seed_play_step));
    memcpy(projection->seed_div_phase, seed.track_div_phase,
           sizeof(projection->seed_div_phase));
    memcpy(projection->seed_swing_phase, seed.track_swing_phase,
           sizeof(projection->seed_swing_phase));
    projection->generation = g_build_generation;
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
        projection->track_rt_note_direct[track] &=
            projection->track_rt_plock_direct[track];
    (void)brick_media_clock_now_sample(&projection->effective_sample);
    __DMB();
    g_published_slot = g_build_slot;
    __DMB();
    g_published_generation = g_build_generation;
}
