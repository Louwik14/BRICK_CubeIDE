#include "Audio/audio_control_command.h"

#include <stddef.h>
#include <string.h>

#include "Audio/audio_control_snapshot.h"
#include "Audio/drum_synth.h"
#include "Audio/fx_master_macro.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_looper_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/track_sound_state.h"
#include "Core/track_tone_sound_state.h"
#include "Core/track_runtime.h"
#include "audio_xfade.h"
#include "stm32h7xx_hal.h"

#define AUDIO_CONTROL_ORDERED_CAPACITY 64U
#define AUDIO_CONTROL_COALESCE_CAPACITY 48U

typedef struct
{
    uint8_t kind;
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t d;
    uint16_t u16;
    param_id_t param_id;
    float value;
    uint32_t generation;
} audio_control_command_t;

static audio_control_command_t g_ordered[AUDIO_CONTROL_ORDERED_CAPACITY];
static volatile uint8_t g_ordered_head;
static volatile uint8_t g_ordered_tail;
static volatile uint32_t g_ordered_depth;

static audio_control_command_t g_coalesced[AUDIO_CONTROL_COALESCE_CAPACITY];
static volatile uint8_t g_coalesced_valid[AUDIO_CONTROL_COALESCE_CAPACITY];
static volatile uint32_t g_coalesced_depth;

static volatile uint32_t g_diag_max_depth;
static volatile uint32_t g_diag_coalesced;
static volatile uint32_t g_diag_rejected;
static volatile uint32_t g_diag_critical_failures;
static volatile uint32_t g_diag_stale_generation_ignored;
static volatile uint32_t g_diag_max_consumed_per_block;

static uint8_t audio_control_command_in_irq(void)
{
    return (__get_IPSR() != 0U) ? 1U : 0U;
}

static uint8_t audio_control_command_is_coalescable(uint8_t kind)
{
    switch ((audio_control_command_kind_t)kind)
    {
        case AUDIO_CONTROL_COMMAND_MIXER_MASTER:
        case AUDIO_CONTROL_COMMAND_MIXER_TRACK_GAIN:
        case AUDIO_CONTROL_COMMAND_MIXER_TRACK_PAN:
        case AUDIO_CONTROL_COMMAND_MIXER_TRACK_MUTE:
        case AUDIO_CONTROL_COMMAND_MIXER_TRACK_SEND_LEVEL:
        case AUDIO_CONTROL_COMMAND_MIXER_SEND_FX_SLOT:
        case AUDIO_CONTROL_COMMAND_MIXER_REVERB_PARAM:
        case AUDIO_CONTROL_COMMAND_MIXER_DELAY_PARAM:
        case AUDIO_CONTROL_COMMAND_MIXER_FILTER_PARAM:
        case AUDIO_CONTROL_COMMAND_MIXER_VCA_PARAM:
        case AUDIO_CONTROL_COMMAND_ENGINE_WAVE_PARAM:
        case AUDIO_CONTROL_COMMAND_ENGINE_DRUM_PARAM:
        case AUDIO_CONTROL_COMMAND_ENGINE_SAMPLER_PARAM:
        case AUDIO_CONTROL_COMMAND_LOOPER_PARAM:
        case AUDIO_CONTROL_COMMAND_XFADE:
        case AUDIO_CONTROL_COMMAND_MIXER_SNAP_TRACK:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t audio_control_command_same_key(const audio_control_command_t *lhs,
                                              const audio_control_command_t *rhs)
{
    return ((lhs != NULL)
            && (rhs != NULL)
            && (lhs->kind == rhs->kind)
            && (lhs->a == rhs->a)
            && (lhs->b == rhs->b)
            && (lhs->c == rhs->c)
            && (lhs->param_id == rhs->param_id)) ? 1U : 0U;
}

static void audio_control_command_update_depth_diag(void)
{
    const uint32_t depth = g_ordered_depth + g_coalesced_depth;
    if (depth > g_diag_max_depth)
    {
        g_diag_max_depth = depth;
    }
}

static void audio_control_command_direct_execute(const audio_control_command_t *cmd)
{
    if (cmd == NULL)
    {
        return;
    }

    switch ((audio_control_command_kind_t)cmd->kind)
    {
        case AUDIO_CONTROL_COMMAND_MIXER_MASTER:
            mixer_set_master(cmd->value);
            break;
        case AUDIO_CONTROL_COMMAND_MIXER_TRACK_GAIN:
            mixer_set_track_gain(cmd->a, cmd->value);
            break;
        case AUDIO_CONTROL_COMMAND_MIXER_TRACK_PAN:
            mixer_set_track_pan(cmd->a, cmd->value);
            break;
        case AUDIO_CONTROL_COMMAND_MIXER_TRACK_MUTE:
            mixer_set_track_mute(cmd->a, cmd->b);
            break;
        case AUDIO_CONTROL_COMMAND_MIXER_TRACK_SEND_LEVEL:
            mixer_set_track_send_level(cmd->a, cmd->b, cmd->value);
            break;
        case AUDIO_CONTROL_COMMAND_MIXER_SEND_FX_SLOT:
            mixer_set_send_fx_slot(cmd->a, (int8_t)cmd->b);
            break;
        case AUDIO_CONTROL_COMMAND_MIXER_REVERB_PARAM:
            switch ((audio_control_reverb_param_t)cmd->a)
            {
                case AUDIO_CONTROL_REVERB_WET: mixer_set_reverb_wet(cmd->value); break;
                case AUDIO_CONTROL_REVERB_SIZE: mixer_set_reverb_size(cmd->value); break;
                case AUDIO_CONTROL_REVERB_DECAY: mixer_set_reverb_decay(cmd->value); break;
                case AUDIO_CONTROL_REVERB_PRE_DELAY: mixer_set_reverb_pre_delay(cmd->value); break;
                case AUDIO_CONTROL_REVERB_SURROUND: mixer_set_reverb_surround(cmd->value); break;
                case AUDIO_CONTROL_REVERB_TYPE: mixer_set_reverb_type(cmd->b); break;
                case AUDIO_CONTROL_REVERB_HPF: mixer_set_reverb_hpf(cmd->value); break;
                case AUDIO_CONTROL_REVERB_LPF: mixer_set_reverb_lpf(cmd->value); break;
                default: break;
            }
            break;
        case AUDIO_CONTROL_COMMAND_MIXER_DELAY_PARAM:
            switch ((audio_control_delay_param_t)cmd->a)
            {
                case AUDIO_CONTROL_DELAY_TYPE: mixer_set_delay_type(cmd->b); break;
                case AUDIO_CONTROL_DELAY_MODE: mixer_set_delay_mode(cmd->b); break;
                case AUDIO_CONTROL_DELAY_TIME: mixer_set_delay_time(cmd->value); break;
                case AUDIO_CONTROL_DELAY_TIME_R: mixer_set_delay_time_r(cmd->value); break;
                case AUDIO_CONTROL_DELAY_FEEDBACK: mixer_set_delay_feedback(cmd->value); break;
                case AUDIO_CONTROL_DELAY_HPF: mixer_set_delay_hpf(cmd->value); break;
                case AUDIO_CONTROL_DELAY_LPF: mixer_set_delay_lpf(cmd->value); break;
                case AUDIO_CONTROL_DELAY_PINGPONG: mixer_set_delay_pingpong(cmd->b); break;
                case AUDIO_CONTROL_DELAY_WIDTH: mixer_set_delay_width(cmd->value); break;
                case AUDIO_CONTROL_DELAY_FEEDBACK_WIDTH: mixer_set_delay_feedback_width(cmd->value); break;
                case AUDIO_CONTROL_DELAY_MOD_DEPTH: mixer_set_delay_mod_depth(cmd->value); break;
                case AUDIO_CONTROL_DELAY_MOD_RATE: mixer_set_delay_mod_rate(cmd->value); break;
                case AUDIO_CONTROL_DELAY_REVERB_SEND: mixer_set_delay_reverb_send(cmd->value); break;
                case AUDIO_CONTROL_DELAY_VOLUME: mixer_set_delay_volume(cmd->value); break;
                default: break;
            }
            break;
        case AUDIO_CONTROL_COMMAND_MIXER_FILTER_PARAM:
            switch ((audio_control_filter_param_t)cmd->b)
            {
                case AUDIO_CONTROL_FILTER_TYPE: mixer_set_track_filter_type(cmd->a, (mixer_track_filter_type_t)cmd->c); break;
                case AUDIO_CONTROL_FILTER_CUTOFF: mixer_set_track_filter_cutoff(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_FILTER_RESONANCE: mixer_set_track_filter_resonance(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_FILTER_EG_AMOUNT: mixer_set_track_filter_eg_amount(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_FILTER_ATTACK: mixer_set_track_filter_attack(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_FILTER_DECAY: mixer_set_track_filter_decay(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_FILTER_SUSTAIN: mixer_set_track_filter_sustain(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_FILTER_RELEASE: mixer_set_track_filter_release(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_FILTER_KEYTRACK: mixer_set_track_filter_keytrack(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_FILTER_EQ_LOW: mixer_set_track_filter_eq_low(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_FILTER_EQ_MID: mixer_set_track_filter_eq_mid(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_FILTER_EQ_HIGH: mixer_set_track_filter_eq_high(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_FILTER_ALL_NOTES_OFF: mixer_track_filter_all_notes_off(cmd->a); break;
                default: break;
            }
            break;
        case AUDIO_CONTROL_COMMAND_MIXER_VCA_PARAM:
            switch ((audio_control_vca_param_t)cmd->b)
            {
                case AUDIO_CONTROL_VCA_ATTACK: mixer_set_track_vca_attack(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_VCA_DECAY: mixer_set_track_vca_decay(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_VCA_SUSTAIN: mixer_set_track_vca_sustain(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_VCA_RELEASE: mixer_set_track_vca_release(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_VCA_ENABLED: mixer_set_track_vca_enabled(cmd->a, cmd->c); break;
                case AUDIO_CONTROL_VCA_ALL_NOTES_OFF: mixer_track_vca_all_notes_off(cmd->a); break;
                default: break;
            }
            break;
        case AUDIO_CONTROL_COMMAND_ENGINE_WAVE_PARAM:
            switch ((audio_control_wave_param_t)cmd->b)
            {
                case AUDIO_CONTROL_WAVE_EDIT: brick6_braids_runtime_set_edit(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_WAVE_FINE: brick6_braids_runtime_set_fine(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_WAVE_COARSE: brick6_braids_runtime_set_coarse(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_WAVE_FM: brick6_braids_runtime_set_fm(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_WAVE_TIMBRE: brick6_braids_runtime_set_timbre(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_WAVE_MODULATION: brick6_braids_runtime_set_modulation(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_WAVE_COLOR: brick6_braids_runtime_set_color(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_WAVE_PHASE_RESET: brick6_braids_runtime_set_phase_reset(cmd->a, cmd->c); break;
                case AUDIO_CONTROL_WAVE_VCA_RELEASE: brick6_braids_runtime_set_vca_release_seconds(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_WAVE_ALL_NOTES_OFF: brick6_braids_runtime_all_notes_off(cmd->a); break;
                case AUDIO_CONTROL_WAVE_RESET_INSTANCE: brick6_braids_runtime_reset_instance(cmd->a); break;
                default: break;
            }
            break;
        case AUDIO_CONTROL_COMMAND_ENGINE_WAVE_NOTE:
            if (cmd->b == AUDIO_CONTROL_NOTE_ON)
            {
                brick6_braids_runtime_note_on(cmd->a, (float)cmd->c, (float)cmd->d / 127.0f);
            }
            else if (cmd->b == AUDIO_CONTROL_NOTE_OFF)
            {
                brick6_braids_runtime_note_off(cmd->a, cmd->c);
            }
            else
            {
                brick6_braids_runtime_all_notes_off(cmd->a);
            }
            break;
        case AUDIO_CONTROL_COMMAND_ENGINE_DRUM_PARAM:
            (void)drum_synth_set_param_for_instance(cmd->a, cmd->param_id, cmd->value);
            break;
        case AUDIO_CONTROL_COMMAND_ENGINE_DRUM_NOTE:
            if (cmd->b == AUDIO_CONTROL_NOTE_ON)
            {
                drum_synth_note_on_for_instance(cmd->a, cmd->c, cmd->d);
            }
            else if (cmd->b == AUDIO_CONTROL_NOTE_OFF)
            {
                drum_synth_note_off_for_instance(cmd->a, cmd->c);
            }
            else
            {
                drum_synth_all_notes_off_for_instance(cmd->a);
            }
            break;
        case AUDIO_CONTROL_COMMAND_ENGINE_SAMPLER_PARAM:
            switch ((audio_control_sampler_param_t)cmd->b)
            {
                case AUDIO_CONTROL_SAMPLER_SAMPLE: brick6_sampler_runtime_set_sample(cmd->a, cmd->u16); break;
                case AUDIO_CONTROL_SAMPLER_GAIN: brick6_sampler_runtime_set_gain(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_SAMPLER_MULTI_INSTRUMENT: brick6_sampler_runtime_set_multi_instrument(cmd->a, cmd->u16); break;
                case AUDIO_CONTROL_SAMPLER_MULTI_GAIN: brick6_sampler_runtime_set_multi_gain(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_SAMPLER_MULTI_LOOP: brick6_sampler_runtime_set_multi_loop(cmd->a, cmd->c); break;
                case AUDIO_CONTROL_SAMPLER_START: brick6_sampler_runtime_set_start(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_SAMPLER_END: brick6_sampler_runtime_set_end(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_SAMPLER_MODE: brick6_sampler_runtime_set_mode(cmd->a, cmd->c); break;
                case AUDIO_CONTROL_SAMPLER_TUNE: brick6_sampler_runtime_set_tune(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_SAMPLER_LOOP_START: brick6_sampler_runtime_set_loop_start(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_SAMPLER_SLICE_COUNT: brick6_sampler_runtime_set_slice_count(cmd->a, cmd->c); break;
                case AUDIO_CONTROL_SAMPLER_CLIP_SOURCE_BPM: brick6_sampler_runtime_set_clip_source_bpm(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_SAMPLER_CLIP_SYNC_LENGTH: brick6_sampler_runtime_set_clip_sync_length(cmd->a, cmd->c); break;
                case AUDIO_CONTROL_SAMPLER_CLIP_PITCH: brick6_sampler_runtime_set_clip_pitch(cmd->a, cmd->value); break;
                case AUDIO_CONTROL_SAMPLER_CLIP_PLAY_MODE: brick6_sampler_runtime_set_clip_play_mode(cmd->a, cmd->c); break;
                case AUDIO_CONTROL_SAMPLER_CLIP_LOOP: brick6_sampler_runtime_set_clip_loop(cmd->a, cmd->c); break;
                case AUDIO_CONTROL_SAMPLER_CLIP_STRETCH_MODE: brick6_sampler_runtime_set_clip_stretch_mode(cmd->a, cmd->c); break;
                case AUDIO_CONTROL_SAMPLER_CLIP_GRAIN: brick6_sampler_runtime_set_clip_grain_size(cmd->a, cmd->u16); break;
                case AUDIO_CONTROL_SAMPLER_STOP: brick6_sampler_runtime_stop(cmd->a); break;
                case AUDIO_CONTROL_SAMPLER_STOP_TRANSPORT_CLIPS: brick6_sampler_runtime_stop_transport_clips(); break;
                case AUDIO_CONTROL_SAMPLER_RESET_TRACK: brick6_sampler_runtime_reset_track(cmd->a); break;
                case AUDIO_CONTROL_SAMPLER_STOP_MULTI_INSTRUMENT: brick6_sampler_runtime_stop_multi_instrument(cmd->u16); break;
                default: break;
            }
            break;
        case AUDIO_CONTROL_COMMAND_ENGINE_SAMPLER_NOTE:
            if (cmd->b == AUDIO_CONTROL_NOTE_ON)
            {
                brick6_sampler_runtime_trigger_note_velocity(cmd->a, cmd->c, cmd->d);
            }
            else if (cmd->b == AUDIO_CONTROL_NOTE_OFF)
            {
                brick6_sampler_runtime_note_off_note(cmd->a, cmd->c);
            }
            else
            {
                brick6_sampler_runtime_stop(cmd->a);
            }
            break;
        case AUDIO_CONTROL_COMMAND_LOOPER_PARAM:
            switch ((audio_control_looper_param_t)cmd->b)
            {
                case AUDIO_CONTROL_LOOPER_PLAY_AUTO: brick6_looper_runtime_set_play_auto(cmd->a, cmd->c); break;
                case AUDIO_CONTROL_LOOPER_STRETCH: brick6_looper_runtime_set_stretch(cmd->a, cmd->c, cmd->value, cmd->u16); break;
                case AUDIO_CONTROL_LOOPER_STOP_PLAYBACK: brick6_looper_runtime_stop_playback(cmd->a); break;
                case AUDIO_CONTROL_LOOPER_TRANSPORT_START: brick6_looper_runtime_on_transport_start(); break;
                case AUDIO_CONTROL_LOOPER_TRANSPORT_STOP: brick6_looper_runtime_on_transport_stop(); break;
                case AUDIO_CONTROL_LOOPER_PREPARE_REPLACE: brick6_looper_runtime_prepare_replace(cmd->a); break;
                default: break;
            }
            break;
        case AUDIO_CONTROL_COMMAND_XFADE:
            audio_xfade_set(cmd->value);
            break;
        case AUDIO_CONTROL_COMMAND_PANIC_TRACK:
            if (cmd->b < MIXER_MAX_TRACKS)
            {
                mixer_track_vca_all_notes_off(cmd->b);
                mixer_track_filter_all_notes_off(cmd->b);
            }
            if (cmd->c == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
            {
                drum_synth_all_notes_off_for_instance(cmd->d);
            }
            else if (cmd->c == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
            {
                brick6_braids_runtime_all_notes_off(cmd->d);
            }
            else if (cmd->c == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
            {
                brick6_sampler_runtime_stop(cmd->a);
            }
            break;
        case AUDIO_CONTROL_COMMAND_PANIC_ALL:
            drum_synth_all_notes_off_all();
            brick6_sampler_runtime_stop_transport_clips();
            break;
        case AUDIO_CONTROL_COMMAND_AUDIO_RUNTIME_RESET_ALL:
            for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
            {
                brick6_sampler_runtime_reset_track(track);
            }
            brick6_sampler_runtime_service();
            brick6_looper_runtime_init();
            brick6_braids_runtime_init();
            drum_synth_all_notes_off_all();
            mixer_reset_runtime_state();
            fx_master_macro_init(48000.0f);
            track_sound_state_init();
            track_tone_sound_state_init();
            break;
        case AUDIO_CONTROL_COMMAND_MIXER_REBIND_ONE:
            mixer_rebind_track_state(cmd->a, cmd->b);
            break;
        case AUDIO_CONTROL_COMMAND_MIXER_REBIND_ALL:
            break;
        case AUDIO_CONTROL_COMMAND_MIXER_SNAP_TRACK:
            mixer_snap_track_runtime_state(cmd->a);
            break;
        default:
            break;
    }
}

static uint8_t audio_control_command_enqueue_ordered(const audio_control_command_t *cmd)
{
    const uint8_t next_head = (uint8_t)((g_ordered_head + 1U) % AUDIO_CONTROL_ORDERED_CAPACITY);
    if (next_head == g_ordered_tail)
    {
        g_diag_rejected++;
        g_diag_critical_failures++;
        return 0U;
    }

    g_ordered[g_ordered_head] = *cmd;
    g_ordered_head = next_head;
    g_ordered_depth++;
    audio_control_command_update_depth_diag();
    return 1U;
}

static uint8_t audio_control_command_enqueue_coalesced(const audio_control_command_t *cmd)
{
    for (uint8_t i = 0U; i < AUDIO_CONTROL_COALESCE_CAPACITY; ++i)
    {
        if ((g_coalesced_valid[i] != 0U) && (audio_control_command_same_key(&g_coalesced[i], cmd) != 0U))
        {
            g_coalesced[i] = *cmd;
            g_diag_coalesced++;
            return 1U;
        }
    }

    for (uint8_t i = 0U; i < AUDIO_CONTROL_COALESCE_CAPACITY; ++i)
    {
        if (g_coalesced_valid[i] == 0U)
        {
            g_coalesced[i] = *cmd;
            g_coalesced_valid[i] = 1U;
            g_coalesced_depth++;
            audio_control_command_update_depth_diag();
            return 1U;
        }
    }

    g_diag_rejected++;
    return 0U;
}

static uint8_t audio_control_command_submit(const audio_control_command_t *cmd)
{
    if (cmd == NULL)
    {
        return 0U;
    }

    if (audio_control_command_in_irq() != 0U)
    {
        audio_control_command_direct_execute(cmd);
        return 1U;
    }

    const uint32_t primask = __get_PRIMASK();
    uint8_t ok;
    __disable_irq();
    ok = (audio_control_command_is_coalescable(cmd->kind) != 0U)
            ? audio_control_command_enqueue_coalesced(cmd)
            : audio_control_command_enqueue_ordered(cmd);
    __set_PRIMASK(primask);
    return ok;
}

static void audio_control_command_make(audio_control_command_t *cmd,
                                       audio_control_command_kind_t kind,
                                       uint8_t a,
                                       uint8_t b,
                                       uint8_t c,
                                       float value)
{
    memset(cmd, 0, sizeof(*cmd));
    cmd->kind = (uint8_t)kind;
    cmd->a = a;
    cmd->b = b;
    cmd->c = c;
    cmd->value = value;
    cmd->generation = track_runtime_get_revision();
}

void audio_control_command_init(void)
{
    memset(g_ordered, 0, sizeof(g_ordered));
    memset(g_coalesced, 0, sizeof(g_coalesced));
    memset((void *)g_coalesced_valid, 0, sizeof(g_coalesced_valid));
    g_ordered_head = 0U;
    g_ordered_tail = 0U;
    g_ordered_depth = 0U;
    g_coalesced_depth = 0U;
    g_diag_max_depth = 0U;
    g_diag_coalesced = 0U;
    g_diag_rejected = 0U;
    g_diag_critical_failures = 0U;
    g_diag_stale_generation_ignored = 0U;
    g_diag_max_consumed_per_block = 0U;
}

void audio_control_command_process_from_audio(void)
{
    uint32_t consumed = 0U;
    const uint32_t active_generation = audio_control_snapshot_get_active_track_runtime_revision();

    while ((g_ordered_tail != g_ordered_head) && (consumed < AUDIO_CONTROL_COMMANDS_PER_BLOCK_MAX))
    {
        const audio_control_command_t cmd = g_ordered[g_ordered_tail];
        g_ordered_tail = (uint8_t)((g_ordered_tail + 1U) % AUDIO_CONTROL_ORDERED_CAPACITY);
        if (g_ordered_depth > 0U)
        {
            g_ordered_depth--;
        }
        if ((cmd.generation != 0U) && (cmd.generation != active_generation))
        {
            g_diag_stale_generation_ignored++;
            consumed++;
            continue;
        }
        audio_control_command_direct_execute(&cmd);
        consumed++;
    }

    for (uint8_t i = 0U; (i < AUDIO_CONTROL_COALESCE_CAPACITY) && (consumed < AUDIO_CONTROL_COMMANDS_PER_BLOCK_MAX); ++i)
    {
        if (g_coalesced_valid[i] == 0U)
        {
            continue;
        }

        const audio_control_command_t cmd = g_coalesced[i];
        g_coalesced_valid[i] = 0U;
        if (g_coalesced_depth > 0U)
        {
            g_coalesced_depth--;
        }
        if ((cmd.generation != 0U) && (cmd.generation != active_generation))
        {
            g_diag_stale_generation_ignored++;
            consumed++;
            continue;
        }
        audio_control_command_direct_execute(&cmd);
        consumed++;
    }

    if (consumed > g_diag_max_consumed_per_block)
    {
        g_diag_max_consumed_per_block = consumed;
    }
}

void audio_control_command_diag_snapshot(audio_control_command_diag_t *out_diag)
{
    if (out_diag == NULL)
    {
        return;
    }

    out_diag->current_depth = g_ordered_depth + g_coalesced_depth;
    out_diag->max_depth = g_diag_max_depth;
    out_diag->coalesced_commands = g_diag_coalesced;
    out_diag->rejected_commands = g_diag_rejected;
    out_diag->critical_failures = g_diag_critical_failures;
    out_diag->stale_generation_ignored = g_diag_stale_generation_ignored;
    out_diag->max_consumed_per_block = g_diag_max_consumed_per_block;
}

uint8_t audio_control_command_submit_mixer_master(float value)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd, AUDIO_CONTROL_COMMAND_MIXER_MASTER, 0U, 0U, 0U, value);
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_mixer_track_gain(uint8_t mix_track, float value)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd, AUDIO_CONTROL_COMMAND_MIXER_TRACK_GAIN, mix_track, 0U, 0U, value);
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_mixer_track_pan(uint8_t mix_track, float value)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd, AUDIO_CONTROL_COMMAND_MIXER_TRACK_PAN, mix_track, 0U, 0U, value);
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_mixer_track_mute(uint8_t mix_track, uint8_t value)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd, AUDIO_CONTROL_COMMAND_MIXER_TRACK_MUTE, mix_track, value, 0U, 0.0f);
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_mixer_track_send_level(uint8_t mix_track, uint8_t send, float value)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd, AUDIO_CONTROL_COMMAND_MIXER_TRACK_SEND_LEVEL, mix_track, send, 0U, value);
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_mixer_send_fx_slot(uint8_t send, int8_t slot)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd, AUDIO_CONTROL_COMMAND_MIXER_SEND_FX_SLOT, send, (uint8_t)slot, 0U, 0.0f);
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_mixer_reverb(uint8_t param, float value)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd, AUDIO_CONTROL_COMMAND_MIXER_REVERB_PARAM, param, (uint8_t)(value + 0.5f), 0U, value);
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_mixer_delay(uint8_t param, float value)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd, AUDIO_CONTROL_COMMAND_MIXER_DELAY_PARAM, param, (uint8_t)(value + 0.5f), 0U, value);
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_mixer_filter(uint8_t mix_track, uint8_t param, float value)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd,
                               AUDIO_CONTROL_COMMAND_MIXER_FILTER_PARAM,
                               mix_track,
                               param,
                               (uint8_t)(value + 0.5f),
                               value);
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_mixer_vca(uint8_t mix_track, uint8_t param, float value)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd,
                               AUDIO_CONTROL_COMMAND_MIXER_VCA_PARAM,
                               mix_track,
                               param,
                               (uint8_t)(value + 0.5f),
                               value);
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_mixer_rebind_one(uint8_t previous_mix_track, uint8_t next_mix_track)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd,
                               AUDIO_CONTROL_COMMAND_MIXER_REBIND_ONE,
                               previous_mix_track,
                               next_mix_track,
                               0U,
                               0.0f);
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_mixer_rebind_all(const uint8_t *previous_mix_tracks,
                                                      const uint8_t *next_mix_tracks,
                                                      uint8_t track_count)
{
    if ((previous_mix_tracks == NULL) || (next_mix_tracks == NULL) || (track_count > SEQ_TRACK_COUNT))
    {
        return 0U;
    }

    uint8_t ok = 1U;
    for (uint8_t track = 0U; track < track_count; ++track)
    {
        if (previous_mix_tracks[track] != next_mix_tracks[track])
        {
            ok &= audio_control_command_submit_mixer_rebind_one(previous_mix_tracks[track], next_mix_tracks[track]);
        }
    }
    return ok;
}

uint8_t audio_control_command_submit_mixer_snap_track(uint8_t mix_track)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd, AUDIO_CONTROL_COMMAND_MIXER_SNAP_TRACK, mix_track, 0U, 0U, 0.0f);
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_wave_param(uint8_t instance, uint8_t param, float value)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd,
                               AUDIO_CONTROL_COMMAND_ENGINE_WAVE_PARAM,
                               instance,
                               param,
                               (uint8_t)(value + 0.5f),
                               value);
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_wave_note(uint8_t instance, uint8_t action, uint8_t note, uint8_t velocity)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd, AUDIO_CONTROL_COMMAND_ENGINE_WAVE_NOTE, instance, action, note, 0.0f);
    cmd.d = velocity;
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_drum_param(uint8_t instance, param_id_t param, float value)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd, AUDIO_CONTROL_COMMAND_ENGINE_DRUM_PARAM, instance, 0U, 0U, value);
    cmd.param_id = param;
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_drum_note(uint8_t instance, uint8_t action, uint8_t note, uint8_t velocity)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd, AUDIO_CONTROL_COMMAND_ENGINE_DRUM_NOTE, instance, action, note, 0.0f);
    cmd.d = velocity;
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_sampler_param(uint8_t track, uint8_t param, float value, uint16_t value_u16)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd,
                               AUDIO_CONTROL_COMMAND_ENGINE_SAMPLER_PARAM,
                               track,
                               param,
                               (uint8_t)(value + 0.5f),
                               value);
    cmd.u16 = value_u16;
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_sampler_note(uint8_t track, uint8_t action, uint8_t note, uint8_t velocity)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd, AUDIO_CONTROL_COMMAND_ENGINE_SAMPLER_NOTE, track, action, note, 0.0f);
    cmd.d = velocity;
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_looper_param(uint8_t track, uint8_t param, float value, uint16_t value_u16)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd,
                               AUDIO_CONTROL_COMMAND_LOOPER_PARAM,
                               track,
                               param,
                               (uint8_t)(value + 0.5f),
                               value);
    cmd.u16 = value_u16;
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_looper_stretch(uint8_t track,
                                                    uint8_t mode,
                                                    float pitch_semitones,
                                                    uint16_t grain_frames)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd,
                               AUDIO_CONTROL_COMMAND_LOOPER_PARAM,
                               track,
                               AUDIO_CONTROL_LOOPER_STRETCH,
                               mode,
                               pitch_semitones);
    cmd.u16 = grain_frames;
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_xfade(float value)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd, AUDIO_CONTROL_COMMAND_XFADE, 0U, 0U, 0U, value);
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_panic_track(uint8_t track, uint8_t mix_track, uint8_t engine, uint8_t instance)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd, AUDIO_CONTROL_COMMAND_PANIC_TRACK, track, mix_track, engine, 0.0f);
    cmd.d = instance;
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_panic_all(void)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd, AUDIO_CONTROL_COMMAND_PANIC_ALL, 0U, 0U, 0U, 0.0f);
    return audio_control_command_submit(&cmd);
}

uint8_t audio_control_command_submit_audio_runtime_reset_all(void)
{
    audio_control_command_t cmd;
    audio_control_command_make(&cmd, AUDIO_CONTROL_COMMAND_AUDIO_RUNTIME_RESET_ALL, 0U, 0U, 0U, 0.0f);
    return audio_control_command_submit(&cmd);
}
