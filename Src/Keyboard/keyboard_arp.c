/******************************************************************************
 * @file    keyboard_arp.c
 * @brief   Moteur d’arpégiateur du clavier.
 *
 * Ce module gère toute la logique temps-réel de l’arpégiateur :
 * - mémorisation des notes physiques et latched
 * - génération des pas selon le pattern actif
 * - gestion du hold, gate, swing, accent et strum
 * - scheduling des note_on / note_off
 * - émission des notes via keyboard_engine
 *
 * Il ne gère ni l’interface utilisateur, ni le scan clavier, ni le routage
 * global de l’application. Il est piloté par keyboard_runtime.
 ******************************************************************************/

#include "Keyboard/keyboard_arp.h"

#include "Keyboard/keyboard_engine.h"
#include "Seq/seq_types.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx_hal.h"

#define KBD_ARP_MAX_NOTES 16U
#define KBD_ARP_MAX_CHORD_NOTES 16U
#define KBD_ARP_INTERNAL_BPM 120U
#define KBD_ARP_PENDING_MAX_NOTES 48U
#define KBD_ARP_AUDIO_SAMPLE_RATE_HZ 48000U

typedef enum
{
    KBD_ARP_PATTERN_UP = 0,
    KBD_ARP_PATTERN_DOWN,
    KBD_ARP_PATTERN_UPDN,
    KBD_ARP_PATTERN_RND,
    KBD_ARP_PATTERN_CHORD,
    KBD_ARP_PATTERN_COUNT
} kbd_arp_pattern_t;

typedef enum
{
    KBD_ARP_ACCENT_OFF = 0,
    KBD_ARP_ACCENT_1ST,
    KBD_ARP_ACCENT_ALT,
    KBD_ARP_ACCENT_RND,
    KBD_ARP_ACCENT_COUNT
} kbd_arp_accent_t;

typedef enum
{
    KBD_ARP_STRUM_OFF = 0,
    KBD_ARP_STRUM_UP,
    KBD_ARP_STRUM_DOWN,
    KBD_ARP_STRUM_ALT,
    KBD_ARP_STRUM_RND,
    KBD_ARP_STRUM_COUNT
} kbd_arp_strum_t;

typedef enum
{
    KBD_ARP_DIR_NORMAL = 0,
    KBD_ARP_DIR_PINGPONG,
    KBD_ARP_DIR_RNDWALK,
    KBD_ARP_DIR_COUNT
} kbd_arp_dir_t;

typedef enum
{
    KBD_ARP_SYNC_INT = 0,
    KBD_ARP_SYNC_CLOCK,
    KBD_ARP_SYNC_FREE,
    KBD_ARP_SYNC_COUNT
} kbd_arp_sync_t;

typedef struct
{
    bool hold;
    uint8_t rate;
    uint8_t oct;
    kbd_arp_pattern_t pattern;
    uint8_t gate;
    uint8_t swing;
    kbd_arp_accent_t accent;
    uint8_t vel_acc;
    kbd_arp_strum_t strum;
    int8_t offset;
    int8_t trans;
    uint8_t spread;
    kbd_arp_dir_t dir;
    kbd_arp_sync_t sync;
} keyboard_arp_config_t;

typedef enum
{
    KBD_ARP_NOTE_SOURCE_KBD = 0,
    KBD_ARP_NOTE_SOURCE_SEQ_STEP = 1
} keyboard_arp_note_source_t;

typedef struct
{
    bool arp_hold;
    uint8_t arp_rate;
    uint8_t arp_oct;
    kbd_arp_pattern_t arp_pattern;
    uint8_t arp_gate;
    uint8_t arp_swing;
    kbd_arp_accent_t arp_accent;
    uint8_t arp_vel_acc;
    kbd_arp_strum_t arp_strum;
    int8_t arp_offset;
    int8_t arp_trans;
    uint8_t arp_spread;
    kbd_arp_dir_t arp_dir;
    kbd_arp_sync_t arp_sync;

    uint8_t arp_phys_notes[KBD_ARP_MAX_NOTES];
    uint8_t arp_phys_vel[KBD_ARP_MAX_NOTES];
    uint8_t arp_phys_count;

    uint8_t arp_latched_notes[KBD_ARP_MAX_NOTES];
    uint8_t arp_latched_vel[KBD_ARP_MAX_NOTES];
    uint8_t arp_latched_count;
    bool arp_latched_active;

    uint8_t arp_pattern_notes[KBD_ARP_MAX_NOTES];
    uint8_t arp_pattern_vel[KBD_ARP_MAX_NOTES];
    uint8_t arp_pattern_count;
    uint8_t arp_pattern_source;

    uint8_t arp_active_notes[KBD_ARP_MAX_NOTES];
    uint32_t arp_active_until[KBD_ARP_MAX_NOTES];
    uint8_t arp_active_count;
    uint16_t arp_active_seq_mask;

    uint8_t arp_pending_on_notes[KBD_ARP_PENDING_MAX_NOTES];
    uint8_t arp_pending_on_vel[KBD_ARP_PENDING_MAX_NOTES];
    uint32_t arp_pending_on_time[KBD_ARP_PENDING_MAX_NOTES];
    uint8_t arp_pending_on_seq_source[KBD_ARP_PENDING_MAX_NOTES];
    uint8_t arp_pending_on_count;

    uint8_t arp_last_played[KBD_ARP_MAX_CHORD_NOTES];
    uint8_t arp_last_played_count;
    uint8_t arp_step_index;
    int8_t arp_pingpong_dir;
    bool arp_strum_flip;
    uint32_t arp_last_step_ms;
    uint32_t arp_next_event_ms;
    uint32_t arp_random_seed;
} keyboard_arp_runtime_state_t;

CTRL_STATE static keyboard_arp_runtime_state_t g_keyboard_arp_state[TRACK_TOPOLOGY_PLAY_TRACK_COUNT];
static keyboard_arp_runtime_state_t *g_keyboard_arp_current = &g_keyboard_arp_state[0];
#define g_keyboard_arp (*g_keyboard_arp_current)
static keyboard_arp_config_t g_keyboard_arp_config[TRACK_TOPOLOGY_PLAY_TRACK_COUNT];
static uint8_t g_keyboard_arp_config_revision[TRACK_TOPOLOGY_PLAY_TRACK_COUNT];
static uint8_t g_keyboard_arp_config_initialized = 0U;
static uint8_t g_keyboard_arp_active_track = 0U;
static uint8_t g_keyboard_arp_current_track = 0U;

static keyboard_arp_config_t keyboard_arp_default_config(void)
{
    keyboard_arp_config_t cfg;
    cfg.hold = false;
    cfg.rate = 2U;
    cfg.oct = 0U;
    cfg.pattern = KBD_ARP_PATTERN_UP;
    cfg.gate = 100U;
    cfg.swing = 0U;
    cfg.accent = KBD_ARP_ACCENT_OFF;
    cfg.vel_acc = 24U;
    cfg.strum = KBD_ARP_STRUM_OFF;
    cfg.offset = 0;
    cfg.trans = 0;
    cfg.spread = 0U;
    cfg.dir = KBD_ARP_DIR_NORMAL;
    cfg.sync = KBD_ARP_SYNC_INT;
    return cfg;
}

static void keyboard_arp_ensure_config_initialized(void)
{
    if (g_keyboard_arp_config_initialized != 0U)
    {
        return;
    }

    const keyboard_arp_config_t cfg = keyboard_arp_default_config();
    for (uint8_t track = 0U; track < TRACK_TOPOLOGY_PLAY_TRACK_COUNT; ++track)
    {
        g_keyboard_arp_config[track] = cfg;
    }
    g_keyboard_arp_config_initialized = 1U;
}

static uint8_t keyboard_arp_track_is_valid(uint8_t track)
{
    return track_topology_is_play(track);
}

static void keyboard_arp_select_track(uint8_t track)
{
    if (keyboard_arp_track_is_valid(track) == 0U)
    {
        track = 0U;
    }

    g_keyboard_arp_current_track = track;
    g_keyboard_arp_current = &g_keyboard_arp_state[track];
}

static uint8_t keyboard_arp_emit_track(void)
{
    return g_keyboard_arp_current_track;
}

static uint8_t keyboard_arp_state_has_activity(void)
{
    return ((g_keyboard_arp.arp_phys_count > 0U)
            || (g_keyboard_arp.arp_latched_count > 0U)
            || (g_keyboard_arp.arp_pattern_count > 0U)
            || (g_keyboard_arp.arp_active_count > 0U)
            || (g_keyboard_arp.arp_pending_on_count > 0U)) ? 1U : 0U;
}

static uint8_t keyboard_arp_state_is_hold_owned(void)
{
    return ((g_keyboard_arp.arp_hold)
            && ((g_keyboard_arp.arp_latched_count > 0U)
                || (g_keyboard_arp.arp_latched_active)
                || (g_keyboard_arp.arp_pattern_count > 0U)
                || (g_keyboard_arp.arp_active_count > 0U)
                || (g_keyboard_arp.arp_pending_on_count > 0U))) ? 1U : 0U;
}

static uint8_t keyboard_arp_track_has_hold_activity(uint8_t track)
{
    if (keyboard_arp_track_is_valid(track) == 0U)
    {
        return 0U;
    }

    const uint8_t previous_track = g_keyboard_arp_current_track;
    keyboard_arp_select_track(track);
    const uint8_t active = keyboard_arp_state_is_hold_owned();
    keyboard_arp_select_track(previous_track);
    return active;
}

static void keyboard_arp_load_config(const keyboard_arp_config_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    g_keyboard_arp.arp_hold = cfg->hold;
    g_keyboard_arp.arp_rate = cfg->rate;
    g_keyboard_arp.arp_oct = cfg->oct;
    g_keyboard_arp.arp_pattern = cfg->pattern;
    g_keyboard_arp.arp_gate = cfg->gate;
    g_keyboard_arp.arp_swing = cfg->swing;
    g_keyboard_arp.arp_accent = cfg->accent;
    g_keyboard_arp.arp_vel_acc = cfg->vel_acc;
    g_keyboard_arp.arp_strum = cfg->strum;
    g_keyboard_arp.arp_offset = cfg->offset;
    g_keyboard_arp.arp_trans = cfg->trans;
    g_keyboard_arp.arp_spread = cfg->spread;
    g_keyboard_arp.arp_dir = cfg->dir;
    g_keyboard_arp.arp_sync = cfg->sync;
}

static void keyboard_arp_update_active_config(uint8_t track)
{
    if (keyboard_arp_track_is_valid(track) != 0U)
    {
        const uint8_t previous = g_keyboard_arp_current_track;
        keyboard_arp_select_track(track);
        keyboard_arp_load_config(&g_keyboard_arp_config[track]);
        keyboard_arp_select_track(previous);
    }
}

static void keyboard_arp_bump_config_revision(uint8_t track)
{
    if (keyboard_arp_track_is_valid(track) == 0U)
    {
        return;
    }

    g_keyboard_arp_config_revision[track]++;
    if (g_keyboard_arp_config_revision[track] == 0U)
    {
        g_keyboard_arp_config_revision[track] = 1U;
    }
}

static uint32_t keyboard_arp_rng(void)
{
    g_keyboard_arp.arp_random_seed =
        (g_keyboard_arp.arp_random_seed * 1664525U) + 1013904223U;
    return g_keyboard_arp.arp_random_seed;
}

static uint8_t keyboard_arp_clamp_u7(int32_t value)
{
    if (value < 0)
    {
        return 0U;
    }
    if (value > 127)
    {
        return 127U;
    }
    return (uint8_t)value;
}

static uint32_t keyboard_arp_step_interval_ms(void)
{
    static const uint16_t ppqn24_per_step[] = {24U, 12U, 6U, 3U, 16U, 8U, 4U, 2U};
    const uint8_t rate = (g_keyboard_arp.arp_rate > 7U) ? 7U : g_keyboard_arp.arp_rate;
    const uint32_t quarter_note_ms = (60000U / KBD_ARP_INTERNAL_BPM);
    uint32_t period = (quarter_note_ms * ppqn24_per_step[rate]) / 24U;
    if (period == 0U)
    {
        period = 1U;
    }
    return period;
}

static uint64_t keyboard_arp_step_interval_samples_q16(uint32_t samples_per_step_q16)
{
    static const uint16_t ppqn24_per_step[] = {24U, 12U, 6U, 3U, 16U, 8U, 4U, 2U};
    const uint8_t rate = (g_keyboard_arp.arp_rate > 7U) ? 7U : g_keyboard_arp.arp_rate;
    const uint32_t sps_q16 = (samples_per_step_q16 == 0U) ? 1U : samples_per_step_q16;
    uint64_t period = ((uint64_t)sps_q16 * (uint64_t)ppqn24_per_step[rate] + 3ULL) / 6ULL;
    if (period == 0U)
    {
        period = 1U;
    }
    return period;
}

static uint32_t keyboard_arp_gate_samples_from_period_q16(uint64_t period_samples_q16)
{
    uint64_t gate_q16 = (period_samples_q16 * (uint64_t)g_keyboard_arp.arp_gate) / 100ULL;
    uint32_t gate = (uint32_t)((gate_q16 + 0x8000ULL) >> 16);
    if (gate == 0U)
    {
        gate = 1U;
    }
    return gate;
}

static uint32_t keyboard_arp_gate_ms(uint32_t period_ms)
{
    uint32_t gate = (period_ms * g_keyboard_arp.arp_gate) / 100U;
    if (gate == 0U)
    {
        gate = 1U;
    }
    return gate;
}

static uint32_t keyboard_arp_strum_ms(void)
{
    int32_t value = g_keyboard_arp.arp_offset;
    if (value < 0)
    {
        value = 0;
    }
    if (value > 60)
    {
        value = 60;
    }
    return (uint32_t)value;
}

static uint32_t keyboard_arp_strum_samples(void)
{
    const uint32_t ms = keyboard_arp_strum_ms();
    return (uint32_t)(((uint64_t)ms * (uint64_t)KBD_ARP_AUDIO_SAMPLE_RATE_HZ + 999ULL) / 1000ULL);
}

static void keyboard_arp_copy_notes(uint8_t *dst_notes,
                                    uint8_t *dst_vel,
                                    uint8_t *dst_count,
                                    const uint8_t *src_notes,
                                    const uint8_t *src_vel,
                                    uint8_t src_count)
{
    for (uint8_t i = 0U; i < src_count; ++i)
    {
        dst_notes[i] = src_notes[i];
        dst_vel[i] = src_vel[i];
    }
    *dst_count = src_count;
}

static void keyboard_arp_insert_sorted_unique(uint8_t *notes,
                                              uint8_t *vel,
                                              uint8_t *count,
                                              uint8_t capacity,
                                              uint8_t note,
                                              uint8_t velocity)
{
    if (*count >= capacity)
    {
        return;
    }

    uint8_t idx = 0U;
    while ((idx < *count) && (notes[idx] < note))
    {
        ++idx;
    }

    if ((idx < *count) && (notes[idx] == note))
    {
        vel[idx] = velocity;
        return;
    }

    for (uint8_t j = *count; j > idx; --j)
    {
        notes[j] = notes[j - 1U];
        vel[j] = vel[j - 1U];
    }

    notes[idx] = note;
    vel[idx] = velocity;
    (*count)++;
}

static void keyboard_arp_remove_note(uint8_t *notes,
                                     uint8_t *vel,
                                     uint8_t *count,
                                     uint8_t note)
{
    for (uint8_t i = 0U; i < *count; ++i)
    {
        if (notes[i] != note)
        {
            continue;
        }

        for (uint8_t j = i; (j + 1U) < *count; ++j)
        {
            notes[j] = notes[j + 1U];
            vel[j] = vel[j + 1U];
        }

        (*count)--;
        return;
    }
}

static void keyboard_arp_activate_from_phys(void)
{
    keyboard_arp_copy_notes(g_keyboard_arp.arp_pattern_notes,
                            g_keyboard_arp.arp_pattern_vel,
                            &g_keyboard_arp.arp_pattern_count,
                            g_keyboard_arp.arp_phys_notes,
                            g_keyboard_arp.arp_phys_vel,
                            g_keyboard_arp.arp_phys_count);
    g_keyboard_arp.arp_pattern_source = (uint8_t)KBD_ARP_NOTE_SOURCE_KBD;
}

static void keyboard_arp_activate_from_latched(void)
{
    keyboard_arp_copy_notes(g_keyboard_arp.arp_pattern_notes,
                            g_keyboard_arp.arp_pattern_vel,
                            &g_keyboard_arp.arp_pattern_count,
                            g_keyboard_arp.arp_latched_notes,
                            g_keyboard_arp.arp_latched_vel,
                            g_keyboard_arp.arp_latched_count);
    g_keyboard_arp.arp_latched_active = (g_keyboard_arp.arp_latched_count > 0U);
    g_keyboard_arp.arp_pattern_source = (uint8_t)KBD_ARP_NOTE_SOURCE_KBD;
}

static uint8_t keyboard_arp_pattern_source_is_seq_step(void)
{
    return (g_keyboard_arp.arp_pattern_source == (uint8_t)KBD_ARP_NOTE_SOURCE_SEQ_STEP) ? 1U : 0U;
}

static void keyboard_arp_schedule_note_off(uint8_t note, uint32_t off_time, uint8_t seq_step_source)
{
    if (g_keyboard_arp.arp_active_count >= KBD_ARP_MAX_NOTES)
    {
        return;
    }

    const uint8_t i = g_keyboard_arp.arp_active_count;
    g_keyboard_arp.arp_active_notes[i] = note;
    g_keyboard_arp.arp_active_until[i] = off_time;
    g_keyboard_arp.arp_active_count++;
    g_keyboard_arp.arp_active_seq_mask &= (uint16_t)~(uint16_t)(1U << i);
    if (seq_step_source != 0U)
    {
        g_keyboard_arp.arp_active_seq_mask |= (uint16_t)(1U << i);
    }
}

static void keyboard_arp_queue_note_on(uint8_t note, uint8_t velocity, uint32_t when)
{
    if (g_keyboard_arp.arp_pending_on_count >= KBD_ARP_PENDING_MAX_NOTES)
    {
        return;
    }

    const uint8_t i = g_keyboard_arp.arp_pending_on_count;
    g_keyboard_arp.arp_pending_on_notes[i] = note;
    g_keyboard_arp.arp_pending_on_vel[i] = velocity;
    g_keyboard_arp.arp_pending_on_time[i] = when;
    g_keyboard_arp.arp_pending_on_seq_source[i] =
        keyboard_arp_pattern_source_is_seq_step();
    g_keyboard_arp.arp_pending_on_count++;
}

static void keyboard_arp_dispatch_pending_note_on(uint32_t now, uint32_t gate_ms)
{
    uint8_t w = 0U;
    for (uint8_t i = 0U; i < g_keyboard_arp.arp_pending_on_count; ++i)
    {
        const uint8_t seq_step_source = g_keyboard_arp.arp_pending_on_seq_source[i];
        if (g_keyboard_arp.arp_pending_on_time[i] <= now)
        {
            const uint32_t on_time = g_keyboard_arp.arp_pending_on_time[i];
            keyboard_engine_note_on_for_track(keyboard_arp_emit_track(),
                                              g_keyboard_arp.arp_pending_on_notes[i],
                                              g_keyboard_arp.arp_pending_on_vel[i]);
            keyboard_arp_schedule_note_off(g_keyboard_arp.arp_pending_on_notes[i],
                                           on_time + gate_ms,
                                           seq_step_source);
        }
        else
        {
            g_keyboard_arp.arp_pending_on_notes[w] = g_keyboard_arp.arp_pending_on_notes[i];
            g_keyboard_arp.arp_pending_on_vel[w] = g_keyboard_arp.arp_pending_on_vel[i];
            g_keyboard_arp.arp_pending_on_time[w] = g_keyboard_arp.arp_pending_on_time[i];
            g_keyboard_arp.arp_pending_on_seq_source[w] = seq_step_source;
            ++w;
        }
    }

    g_keyboard_arp.arp_pending_on_count = w;
}

static void keyboard_arp_dispatch_note_off(uint32_t now)
{
    uint8_t w = 0U;
    uint16_t seq_mask = 0U;

    for (uint8_t i = 0U; i < g_keyboard_arp.arp_active_count; ++i)
    {
        const uint8_t seq_step_source =
            ((g_keyboard_arp.arp_active_seq_mask & (uint16_t)(1U << i)) != 0U) ? 1U : 0U;
        if (g_keyboard_arp.arp_active_until[i] <= now)
        {
            keyboard_engine_note_off_for_track(keyboard_arp_emit_track(),
                                               g_keyboard_arp.arp_active_notes[i]);
        }
        else
        {
            g_keyboard_arp.arp_active_notes[w] = g_keyboard_arp.arp_active_notes[i];
            g_keyboard_arp.arp_active_until[w] = g_keyboard_arp.arp_active_until[i];
            if (seq_step_source != 0U)
            {
                seq_mask |= (uint16_t)(1U << w);
            }
            ++w;
        }
    }

    g_keyboard_arp.arp_active_count = w;
    g_keyboard_arp.arp_active_seq_mask = seq_mask;
}

static void keyboard_arp_release_owned_notes(void)
{
    for (uint8_t i = 0U; i < g_keyboard_arp.arp_active_count; ++i)
    {
        keyboard_engine_note_off_for_track(keyboard_arp_emit_track(),
                                           g_keyboard_arp.arp_active_notes[i]);
    }
    g_keyboard_arp.arp_active_count = 0U;
    g_keyboard_arp.arp_active_seq_mask = 0U;
    g_keyboard_arp.arp_pending_on_count = 0U;
    g_keyboard_arp.arp_last_played_count = 0U;
}

static void keyboard_arp_release_seq_step_notes(void)
{
    uint8_t active_w = 0U;
    uint16_t active_seq_mask = 0U;
    for (uint8_t i = 0U; i < g_keyboard_arp.arp_active_count; ++i)
    {
        const uint8_t seq_step_source =
            ((g_keyboard_arp.arp_active_seq_mask & (uint16_t)(1U << i)) != 0U) ? 1U : 0U;
        if (seq_step_source != 0U)
        {
            keyboard_engine_note_off_for_track(keyboard_arp_emit_track(),
                                               g_keyboard_arp.arp_active_notes[i]);
            continue;
        }

        g_keyboard_arp.arp_active_notes[active_w] = g_keyboard_arp.arp_active_notes[i];
        g_keyboard_arp.arp_active_until[active_w] = g_keyboard_arp.arp_active_until[i];
        ++active_w;
    }
    g_keyboard_arp.arp_active_count = active_w;
    g_keyboard_arp.arp_active_seq_mask = active_seq_mask;

    uint8_t pending_w = 0U;
    for (uint8_t i = 0U; i < g_keyboard_arp.arp_pending_on_count; ++i)
    {
        if (g_keyboard_arp.arp_pending_on_seq_source[i] != 0U)
        {
            continue;
        }

        g_keyboard_arp.arp_pending_on_notes[pending_w] = g_keyboard_arp.arp_pending_on_notes[i];
        g_keyboard_arp.arp_pending_on_vel[pending_w] = g_keyboard_arp.arp_pending_on_vel[i];
        g_keyboard_arp.arp_pending_on_time[pending_w] = g_keyboard_arp.arp_pending_on_time[i];
        g_keyboard_arp.arp_pending_on_seq_source[pending_w] = 0U;
        ++pending_w;
    }
    g_keyboard_arp.arp_pending_on_count = pending_w;

    if (keyboard_arp_pattern_source_is_seq_step() != 0U)
    {
        g_keyboard_arp.arp_pattern_count = 0U;
        g_keyboard_arp.arp_pattern_source = (uint8_t)KBD_ARP_NOTE_SOURCE_KBD;
        g_keyboard_arp.arp_last_played_count = 0U;
        if (g_keyboard_arp.arp_hold && (g_keyboard_arp.arp_latched_count > 0U))
        {
            keyboard_arp_activate_from_latched();
        }
    }

    if ((g_keyboard_arp.arp_phys_count == 0U)
        && (g_keyboard_arp.arp_latched_count == 0U)
        && (g_keyboard_arp.arp_pattern_count == 0U)
        && (g_keyboard_arp.arp_active_count == 0U)
        && (g_keyboard_arp.arp_pending_on_count == 0U))
    {
        g_keyboard_arp.arp_step_index = 0U;
        g_keyboard_arp.arp_pingpong_dir = 1;
        g_keyboard_arp.arp_strum_flip = false;
        g_keyboard_arp.arp_last_played_count = 0U;
    }
}

static void keyboard_arp_reset_phrase(bool stop_notes)
{
    if (stop_notes)
    {
        keyboard_arp_release_owned_notes();
    }

    g_keyboard_arp.arp_step_index = 0U;
    g_keyboard_arp.arp_pingpong_dir = 1;
    g_keyboard_arp.arp_strum_flip = false;
    g_keyboard_arp.arp_last_step_ms = HAL_GetTick();
    g_keyboard_arp.arp_next_event_ms = HAL_GetTick();
}

static uint8_t keyboard_arp_apply_step_accent(uint8_t velocity, uint8_t step_index)
{
    const int32_t strong = (int32_t)g_keyboard_arp.arp_vel_acc * 2;

    switch (g_keyboard_arp.arp_accent)
    {
        case KBD_ARP_ACCENT_1ST:
            if (step_index == 0U)
            {
                return keyboard_arp_clamp_u7((int32_t)velocity + strong);
            }
            return velocity;

        case KBD_ARP_ACCENT_ALT:
            if ((step_index & 0x1U) == 0U)
            {
                return keyboard_arp_clamp_u7((int32_t)velocity + strong);
            }
            return velocity;

        case KBD_ARP_ACCENT_RND:
        {
            const uint8_t delta = (uint8_t)(keyboard_arp_rng() % ((g_keyboard_arp.arp_vel_acc * 2U) + 1U));
            return keyboard_arp_clamp_u7((int32_t)velocity + delta);
        }

        case KBD_ARP_ACCENT_OFF:
        default:
            return velocity;
    }
}

static uint8_t keyboard_arp_next_base_index(uint8_t count)
{
    if (count == 0U)
    {
        return 0U;
    }

    if (g_keyboard_arp.arp_pattern == KBD_ARP_PATTERN_RND)
    {
        return (uint8_t)(keyboard_arp_rng() % count);
    }

    if ((g_keyboard_arp.arp_pattern == KBD_ARP_PATTERN_DOWN)
            || (g_keyboard_arp.arp_strum == KBD_ARP_STRUM_DOWN)
            || ((g_keyboard_arp.arp_strum == KBD_ARP_STRUM_ALT) && g_keyboard_arp.arp_strum_flip))
    {
        return (uint8_t)((count - 1U) - (g_keyboard_arp.arp_step_index % count));
    }

    if (g_keyboard_arp.arp_pattern == KBD_ARP_PATTERN_UPDN)
    {
        const uint8_t period = (count <= 1U) ? 1U : (uint8_t)((count * 2U) - 2U);
        const uint8_t phase = (period == 0U) ? 0U : (uint8_t)(g_keyboard_arp.arp_step_index % period);

        if (phase < count)
        {
            return phase;
        }

        return (uint8_t)(period - phase);
    }

    return (uint8_t)(g_keyboard_arp.arp_step_index % count);
}

static uint8_t keyboard_arp_compute_octave(uint8_t step_index)
{
    const uint8_t oct_count = (g_keyboard_arp.arp_oct > 4U) ? 4U : g_keyboard_arp.arp_oct;
    if (oct_count == 0U)
    {
        return 0U;
    }

    if (g_keyboard_arp.arp_dir == KBD_ARP_DIR_RNDWALK)
    {
        const int8_t step = ((keyboard_arp_rng() & 0x1U) == 0U) ? 1 : -1;
        const int8_t current = (int8_t)(g_keyboard_arp.arp_step_index % (oct_count + 1U));
        int8_t next = current + step;
        if (next < 0)
        {
            next = 0;
        }
        if (next > (int8_t)oct_count)
        {
            next = (int8_t)oct_count;
        }
        return (uint8_t)next;
    }

    if (g_keyboard_arp.arp_dir == KBD_ARP_DIR_PINGPONG)
    {
        const uint8_t period = (uint8_t)((oct_count * 2U) + 1U);
        const uint8_t phase = (period == 0U) ? 0U : (uint8_t)(step_index % period);
        return (phase <= oct_count) ? phase : (uint8_t)(period - phase);
    }

    return (uint8_t)(step_index % (oct_count + 1U));
}



static bool keyboard_arp_should_tick(uint32_t now, uint32_t interval_ms)
{
    if (interval_ms == 0U)
    {
        return false;
    }

    if (g_keyboard_arp.arp_sync == KBD_ARP_SYNC_FREE)
    {
        return ((now - g_keyboard_arp.arp_last_step_ms) >= interval_ms);
    }

    if (g_keyboard_arp.arp_sync == KBD_ARP_SYNC_CLOCK)
    {
        return ((now - g_keyboard_arp.arp_last_step_ms) >= interval_ms);
    }

    return ((now - g_keyboard_arp.arp_last_step_ms) >= interval_ms);
}

static void keyboard_arp_try_start(uint32_t now)
{
    if (g_keyboard_arp.arp_pattern_count == 0U)
    {
        if (g_keyboard_arp.arp_hold && (g_keyboard_arp.arp_latched_count > 0U))
        {
            keyboard_arp_activate_from_latched();
        }
        else if (g_keyboard_arp.arp_phys_count > 0U)
        {
            if (g_keyboard_arp.arp_hold)
            {
                keyboard_arp_copy_notes(g_keyboard_arp.arp_latched_notes,
                                        g_keyboard_arp.arp_latched_vel,
                                        &g_keyboard_arp.arp_latched_count,
                                        g_keyboard_arp.arp_phys_notes,
                                        g_keyboard_arp.arp_phys_vel,
                                        g_keyboard_arp.arp_phys_count);
                keyboard_arp_activate_from_latched();
            }
            else
            {
                keyboard_arp_activate_from_phys();
            }
        }
    }

    if (g_keyboard_arp.arp_next_event_ms == 0U)
    {
        g_keyboard_arp.arp_next_event_ms = now;
    }
}

static void keyboard_arp_play_step(uint32_t now, uint32_t period_ms)
{
    const uint8_t src_count = g_keyboard_arp.arp_pattern_count;
    if (src_count == 0U)
    {
        return;
    }

    uint8_t notes[KBD_ARP_MAX_CHORD_NOTES];
    uint8_t velocities[KBD_ARP_MAX_CHORD_NOTES];
    uint8_t out_count = 0U;

    const uint8_t base_index = keyboard_arp_next_base_index(src_count);
    const uint8_t octave = keyboard_arp_compute_octave(g_keyboard_arp.arp_step_index);
    const int16_t octave_offset = (int16_t)octave * 12;
    const int16_t trans = (int16_t)g_keyboard_arp.arp_trans;

    if (g_keyboard_arp.arp_pattern == KBD_ARP_PATTERN_CHORD)
    {
        for (uint8_t i = 0U; (i < src_count) && (out_count < KBD_ARP_MAX_CHORD_NOTES); ++i)
        {
            uint8_t source_idx = i;
            if ((g_keyboard_arp.arp_strum == KBD_ARP_STRUM_DOWN)
                    || ((g_keyboard_arp.arp_strum == KBD_ARP_STRUM_ALT) && g_keyboard_arp.arp_strum_flip))
            {
                source_idx = (uint8_t)((src_count - 1U) - i);
            }
            else if (g_keyboard_arp.arp_strum == KBD_ARP_STRUM_RND)
            {
                source_idx = (uint8_t)((HAL_GetTick() + i) % src_count);
            }

            int16_t note = (int16_t)g_keyboard_arp.arp_pattern_notes[source_idx]
                         + octave_offset
                         + trans
                         + ((int16_t)g_keyboard_arp.arp_spread * (int16_t)i);

            if (note < 0)
            {
                note = 0;
            }
            if (note > 127)
            {
                note = 127;
            }

            notes[out_count] = (uint8_t)note;
            velocities[out_count] = g_keyboard_arp.arp_pattern_vel[source_idx];
            out_count++;
        }
    }
    else
    {
        int16_t note = (int16_t)g_keyboard_arp.arp_pattern_notes[base_index] + octave_offset + trans;

        if (note < 0)
        {
            note = 0;
        }
        if (note > 127)
        {
            note = 127;
        }

        notes[out_count] = (uint8_t)note;
        velocities[out_count] = g_keyboard_arp.arp_pattern_vel[base_index];
        out_count++;
    }

    const uint32_t gate_ms = keyboard_arp_gate_ms(period_ms);
    const uint32_t strum_ms = keyboard_arp_strum_ms();

    if ((g_keyboard_arp.arp_pattern != KBD_ARP_PATTERN_CHORD)
        && (g_keyboard_arp.arp_strum == KBD_ARP_STRUM_OFF))
    {
        const uint8_t vel = keyboard_arp_apply_step_accent(velocities[0], g_keyboard_arp.arp_step_index);
        keyboard_engine_note_on_for_track(keyboard_arp_emit_track(), notes[0], vel);
        keyboard_arp_schedule_note_off(notes[0],
                                       now + gate_ms,
                                       keyboard_arp_pattern_source_is_seq_step());
    }
    else
    {
        uint8_t order[KBD_ARP_MAX_CHORD_NOTES];
        for (uint8_t i = 0U; i < out_count; ++i)
        {
            order[i] = i;
        }

        for (uint8_t i = 1U; i < out_count; ++i)
        {
            uint8_t key = order[i];
            uint8_t j = i;
            while ((j > 0U) && (notes[order[j - 1U]] > notes[key]))
            {
                order[j] = order[j - 1U];
                --j;
            }
            order[j] = key;
        }

        if (g_keyboard_arp.arp_strum == KBD_ARP_STRUM_DOWN)
        {
            for (uint8_t i = 0U; i < (out_count / 2U); ++i)
            {
                const uint8_t j = (uint8_t)(out_count - 1U - i);
                const uint8_t t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
        else if (g_keyboard_arp.arp_strum == KBD_ARP_STRUM_ALT)
        {
            if (g_keyboard_arp.arp_strum_flip)
            {
                for (uint8_t i = 0U; i < (out_count / 2U); ++i)
                {
                    const uint8_t j = (uint8_t)(out_count - 1U - i);
                    const uint8_t t = order[i];
                    order[i] = order[j];
                    order[j] = t;
                }
            }
        }
        else if (g_keyboard_arp.arp_strum == KBD_ARP_STRUM_RND)
        {
            for (uint8_t i = out_count; i > 1U; --i)
            {
                const uint8_t j = (uint8_t)(keyboard_arp_rng() % i);
                const uint8_t t = order[i - 1U];
                order[i - 1U] = order[j];
                order[j] = t;
            }
        }

        uint32_t offset = 0U;

        for (uint8_t i = 0U; i < out_count; ++i)
        {
            uint32_t when = now + offset;

            if ((g_keyboard_arp.arp_strum == KBD_ARP_STRUM_RND) && (strum_ms > 1U))
            {
                const uint32_t jitter = (uint32_t)(keyboard_arp_rng() % (strum_ms / 2U + 1U));
                when += jitter;
            }

            const uint8_t idx = order[i];
            const uint8_t vel = keyboard_arp_apply_step_accent(velocities[idx],
                                                               (uint8_t)(g_keyboard_arp.arp_step_index + i));
            keyboard_arp_queue_note_on(notes[idx], vel, when);

            if (g_keyboard_arp.arp_strum != KBD_ARP_STRUM_OFF)
            {
                offset += strum_ms;
            }
        }
    }


    g_keyboard_arp.arp_last_played_count = out_count;
    for (uint8_t i = 0U; i < out_count; ++i)
    {
        g_keyboard_arp.arp_last_played[i] = notes[i];
    }

    g_keyboard_arp.arp_step_index++;
    if (g_keyboard_arp.arp_strum == KBD_ARP_STRUM_ALT)
    {
        g_keyboard_arp.arp_strum_flip = !g_keyboard_arp.arp_strum_flip;
    }
}

static uint8_t keyboard_arp_render_current_step_samples(uint32_t samples_per_step_q16,
                                                        keyboard_arp_scheduled_note_t *out_notes,
                                                        uint8_t max_out_notes)
{
    const uint8_t src_count = g_keyboard_arp.arp_pattern_count;
    if ((src_count == 0U) || (out_notes == NULL) || (max_out_notes == 0U))
    {
        return 0U;
    }

    uint8_t notes[KBD_ARP_MAX_CHORD_NOTES];
    uint8_t velocities[KBD_ARP_MAX_CHORD_NOTES];
    uint8_t out_count = 0U;

    const uint8_t base_index = keyboard_arp_next_base_index(src_count);
    const uint8_t octave = keyboard_arp_compute_octave(g_keyboard_arp.arp_step_index);
    const int16_t octave_offset = (int16_t)octave * 12;
    const int16_t trans = (int16_t)g_keyboard_arp.arp_trans;

    if (g_keyboard_arp.arp_pattern == KBD_ARP_PATTERN_CHORD)
    {
        for (uint8_t i = 0U; (i < src_count) && (out_count < KBD_ARP_MAX_CHORD_NOTES); ++i)
        {
            uint8_t source_idx = i;
            if ((g_keyboard_arp.arp_strum == KBD_ARP_STRUM_DOWN)
                    || ((g_keyboard_arp.arp_strum == KBD_ARP_STRUM_ALT) && g_keyboard_arp.arp_strum_flip))
            {
                source_idx = (uint8_t)((src_count - 1U) - i);
            }
            else if (g_keyboard_arp.arp_strum == KBD_ARP_STRUM_RND)
            {
                source_idx = (uint8_t)(keyboard_arp_rng() % src_count);
            }

            int16_t note = (int16_t)g_keyboard_arp.arp_pattern_notes[source_idx]
                         + octave_offset
                         + trans
                         + ((int16_t)g_keyboard_arp.arp_spread * (int16_t)i);
            if (note < 0)
            {
                note = 0;
            }
            if (note > 127)
            {
                note = 127;
            }

            notes[out_count] = (uint8_t)note;
            velocities[out_count] = g_keyboard_arp.arp_pattern_vel[source_idx];
            out_count++;
        }
    }
    else
    {
        int16_t note = (int16_t)g_keyboard_arp.arp_pattern_notes[base_index] + octave_offset + trans;
        if (note < 0)
        {
            note = 0;
        }
        if (note > 127)
        {
            note = 127;
        }
        notes[out_count] = (uint8_t)note;
        velocities[out_count] = g_keyboard_arp.arp_pattern_vel[base_index];
        out_count++;
    }

    uint8_t order[KBD_ARP_MAX_CHORD_NOTES];
    for (uint8_t i = 0U; i < out_count; ++i)
    {
        order[i] = i;
    }

    if ((g_keyboard_arp.arp_pattern == KBD_ARP_PATTERN_CHORD)
            || (g_keyboard_arp.arp_strum != KBD_ARP_STRUM_OFF))
    {
        for (uint8_t i = 1U; i < out_count; ++i)
        {
            uint8_t key = order[i];
            uint8_t j = i;
            while ((j > 0U) && (notes[order[j - 1U]] > notes[key]))
            {
                order[j] = order[j - 1U];
                --j;
            }
            order[j] = key;
        }

        if ((g_keyboard_arp.arp_strum == KBD_ARP_STRUM_DOWN)
                || ((g_keyboard_arp.arp_strum == KBD_ARP_STRUM_ALT) && g_keyboard_arp.arp_strum_flip))
        {
            for (uint8_t i = 0U; i < (out_count / 2U); ++i)
            {
                const uint8_t j = (uint8_t)(out_count - 1U - i);
                const uint8_t t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
        else if (g_keyboard_arp.arp_strum == KBD_ARP_STRUM_RND)
        {
            for (uint8_t i = out_count; i > 1U; --i)
            {
                const uint8_t j = (uint8_t)(keyboard_arp_rng() % i);
                const uint8_t t = order[i - 1U];
                order[i - 1U] = order[j];
                order[j] = t;
            }
        }
    }

    const uint64_t period_q16 = keyboard_arp_step_interval_samples_q16(samples_per_step_q16);
    const uint32_t gate_samples = keyboard_arp_gate_samples_from_period_q16(period_q16);
    const uint32_t strum_samples = keyboard_arp_strum_samples();
    uint32_t offset = 0U;
    uint8_t rendered = 0U;

    for (uint8_t i = 0U; (i < out_count) && (rendered < max_out_notes); ++i)
    {
        uint32_t on_offset = offset;
        if ((g_keyboard_arp.arp_strum == KBD_ARP_STRUM_RND) && (strum_samples > 1U))
        {
            on_offset += (uint32_t)(keyboard_arp_rng() % ((strum_samples / 2U) + 1U));
        }

        const uint8_t idx = order[i];
        out_notes[rendered].note = notes[idx];
        out_notes[rendered].velocity =
            keyboard_arp_apply_step_accent(velocities[idx], (uint8_t)(g_keyboard_arp.arp_step_index + i));
        out_notes[rendered].on_offset_samples = on_offset;
        out_notes[rendered].off_offset_samples = on_offset + gate_samples;
        rendered++;

        if (g_keyboard_arp.arp_strum != KBD_ARP_STRUM_OFF)
        {
            offset += strum_samples;
        }
    }

    g_keyboard_arp.arp_last_played_count = rendered;
    for (uint8_t i = 0U; i < rendered; ++i)
    {
        g_keyboard_arp.arp_last_played[i] = out_notes[i].note;
    }

    g_keyboard_arp.arp_step_index++;
    if (g_keyboard_arp.arp_strum == KBD_ARP_STRUM_ALT)
    {
        g_keyboard_arp.arp_strum_flip = !g_keyboard_arp.arp_strum_flip;
    }

    return rendered;
}

static uint8_t keyboard_arp_render_window_samples(uint32_t samples_per_step_q16,
                                                  uint32_t duration_samples,
                                                  keyboard_arp_scheduled_note_t *out_notes,
                                                  uint8_t max_out_notes,
                                                  uint64_t *out_next_offset_q16)
{
    if (out_next_offset_q16 != NULL)
    {
        *out_next_offset_q16 = 0ULL;
    }

    if ((duration_samples == 0U) || (out_notes == NULL) || (max_out_notes == 0U))
    {
        return 0U;
    }

    const uint64_t base_period_q16 = keyboard_arp_step_interval_samples_q16(samples_per_step_q16);
    uint64_t step_offset_q16 = 0ULL;
    uint8_t rendered = 0U;

    while (rendered < max_out_notes)
    {
        const uint32_t step_offset = (uint32_t)(step_offset_q16 >> 16);
        if (step_offset >= duration_samples)
        {
            break;
        }

        keyboard_arp_scheduled_note_t step_notes[KBD_ARP_MAX_CHORD_NOTES];
        const uint8_t step_count =
            keyboard_arp_render_current_step_samples(samples_per_step_q16,
                                                     step_notes,
                                                     KBD_ARP_MAX_CHORD_NOTES);
        if (step_count == 0U)
        {
            break;
        }

        for (uint8_t i = 0U; (i < step_count) && (rendered < max_out_notes); ++i)
        {
            const uint64_t on_abs = (uint64_t)step_offset + (uint64_t)step_notes[i].on_offset_samples;
            if (on_abs >= (uint64_t)duration_samples)
            {
                continue;
            }

            uint64_t off_abs = (uint64_t)step_offset + (uint64_t)step_notes[i].off_offset_samples;
            if (off_abs > (uint64_t)duration_samples)
            {
                off_abs = (uint64_t)duration_samples;
            }
            if (off_abs <= on_abs)
            {
                off_abs = on_abs + 1ULL;
            }

            out_notes[rendered].note = step_notes[i].note;
            out_notes[rendered].velocity = step_notes[i].velocity;
            out_notes[rendered].on_offset_samples = (uint32_t)on_abs;
            out_notes[rendered].off_offset_samples = (uint32_t)off_abs;
            rendered++;
        }

        uint64_t period_q16 = base_period_q16;
        if (((g_keyboard_arp.arp_step_index & 0x1U) != 0U) && (g_keyboard_arp.arp_swing > 0U))
        {
            period_q16 += (base_period_q16 * (uint64_t)g_keyboard_arp.arp_swing) / 100ULL;
        }
        step_offset_q16 += period_q16;
    }

    if (out_next_offset_q16 != NULL)
    {
        *out_next_offset_q16 = step_offset_q16;
    }

    const uint8_t last_count =
        (rendered > KBD_ARP_MAX_CHORD_NOTES) ? KBD_ARP_MAX_CHORD_NOTES : rendered;
    const uint8_t first_last = (uint8_t)(rendered - last_count);
    g_keyboard_arp.arp_last_played_count = last_count;
    for (uint8_t i = 0U; i < last_count; ++i)
    {
        g_keyboard_arp.arp_last_played[i] = out_notes[(uint8_t)(first_last + i)].note;
    }

    return rendered;
}

void keyboard_arp_init(void)
{
    keyboard_arp_ensure_config_initialized();
    g_keyboard_arp_active_track = 0U;
    const uint32_t now = HAL_GetTick();
    for (uint8_t track = 0U; track < TRACK_TOPOLOGY_PLAY_TRACK_COUNT; ++track)
    {
        keyboard_arp_select_track(track);
        keyboard_arp_load_config(&g_keyboard_arp_config[track]);
        g_keyboard_arp.arp_random_seed = 0x12345U + track;
        g_keyboard_arp.arp_next_event_ms = now;
        g_keyboard_arp.arp_last_step_ms = now;
    }
    keyboard_arp_select_track(g_keyboard_arp_active_track);
}

void keyboard_arp_sync_track(uint8_t track)
{
    keyboard_arp_ensure_config_initialized();
    if (keyboard_arp_track_is_valid(track) == 0U)
    {
        return;
    }

    g_keyboard_arp_active_track = track;
    keyboard_arp_select_track(track);
    keyboard_arp_load_config(&g_keyboard_arp_config[track]);
}

void keyboard_arp_tick(void)
{
    const uint32_t now = HAL_GetTick();
    for (uint8_t track = 0U; track < TRACK_TOPOLOGY_PLAY_TRACK_COUNT; ++track)
    {
        keyboard_arp_select_track(track);

        if (keyboard_arp_state_has_activity() == 0U)
        {
            continue;
        }

        const uint32_t base_period = keyboard_arp_step_interval_ms();
        const uint32_t gate_ms = keyboard_arp_gate_ms(base_period);

        keyboard_arp_dispatch_pending_note_on(now, gate_ms);
        keyboard_arp_dispatch_note_off(now);

        keyboard_arp_try_start(now);

        if (g_keyboard_arp.arp_pattern_count == 0U)
        {
            continue;
        }

        if (keyboard_arp_pattern_source_is_seq_step() != 0U)
        {
            continue;
        }

        if (!keyboard_arp_should_tick(now, base_period))
        {
            continue;
        }

        if (g_keyboard_arp.arp_next_event_ms > now)
        {
            continue;
        }

        g_keyboard_arp.arp_last_step_ms = now;
        keyboard_arp_play_step(now, base_period);

        uint32_t period = base_period;
        if (((g_keyboard_arp.arp_step_index & 0x1U) != 0U) && (g_keyboard_arp.arp_swing > 0U))
        {
            period += (base_period * g_keyboard_arp.arp_swing) / 100U;
        }

        g_keyboard_arp.arp_next_event_ms = now + period;
    }
    keyboard_arp_select_track(g_keyboard_arp_active_track);
}

void keyboard_arp_note_on(uint8_t note, uint8_t velocity)
{
    const bool had_phys = (g_keyboard_arp.arp_phys_count > 0U);

    keyboard_arp_insert_sorted_unique(g_keyboard_arp.arp_phys_notes,
                                      g_keyboard_arp.arp_phys_vel,
                                      &g_keyboard_arp.arp_phys_count,
                                      KBD_ARP_MAX_NOTES,
                                      note,
                                      velocity);

    if (g_keyboard_arp.arp_hold)
    {
        if (!had_phys)
        {
            g_keyboard_arp.arp_latched_count = 0U;
        }

        if (g_keyboard_arp.arp_latched_count == 0U)
        {
            keyboard_arp_copy_notes(g_keyboard_arp.arp_latched_notes,
                                    g_keyboard_arp.arp_latched_vel,
                                    &g_keyboard_arp.arp_latched_count,
                                    g_keyboard_arp.arp_phys_notes,
                                    g_keyboard_arp.arp_phys_vel,
                                    g_keyboard_arp.arp_phys_count);
        }
        else if (had_phys)
        {
            keyboard_arp_insert_sorted_unique(g_keyboard_arp.arp_latched_notes,
                                              g_keyboard_arp.arp_latched_vel,
                                              &g_keyboard_arp.arp_latched_count,
                                              KBD_ARP_MAX_NOTES,
                                              note,
                                              velocity);
        }

        keyboard_arp_activate_from_latched();
    }
    else
    {
        keyboard_arp_activate_from_phys();
    }

    if (g_keyboard_arp.arp_pattern_count == 1U)
    {
        keyboard_arp_reset_phrase(true);
    }

    keyboard_arp_try_start(HAL_GetTick());
}

void keyboard_arp_note_off(uint8_t note)
{
    keyboard_arp_remove_note(g_keyboard_arp.arp_phys_notes,
                             g_keyboard_arp.arp_phys_vel,
                             &g_keyboard_arp.arp_phys_count,
                             note);

    if (g_keyboard_arp.arp_hold)
    {
        if (g_keyboard_arp.arp_phys_count == 0U)
        {
            g_keyboard_arp.arp_latched_active = (g_keyboard_arp.arp_latched_count > 0U);
        }
        return;
    }

    keyboard_arp_activate_from_phys();

    if (g_keyboard_arp.arp_pattern_count == 0U)
    {
        keyboard_arp_release_owned_notes();
    }
}

uint8_t keyboard_arp_seq_step_render_for_track(uint8_t track,
                                               const uint8_t *notes,
                                               const uint8_t *velocities,
                                               uint8_t count,
                                               uint32_t samples_per_step_q16,
                                               uint32_t duration_samples,
                                               keyboard_arp_scheduled_note_t *out_notes,
                                               uint8_t max_out_notes,
                                               uint64_t *out_next_offset_q16)
{
    keyboard_arp_ensure_config_initialized();

    if ((keyboard_arp_track_is_valid(track) == 0U)
        || (notes == NULL)
        || (velocities == NULL)
        || (count == 0U)
        || (out_notes == NULL)
        || (max_out_notes == 0U))
    {
        return 0U;
    }

    if (!g_keyboard_arp_config[track].hold)
    {
        return 0U;
    }

    const uint8_t previous_track = g_keyboard_arp_current_track;
    keyboard_arp_select_track(track);
    keyboard_arp_load_config(&g_keyboard_arp_config[track]);

    g_keyboard_arp.arp_pattern_count = 0U;
    g_keyboard_arp.arp_pattern_source = (uint8_t)KBD_ARP_NOTE_SOURCE_SEQ_STEP;
    for (uint8_t i = 0U; i < count; ++i)
    {
        const uint8_t note = notes[i];
        const uint8_t velocity = velocities[i];
        if ((note >= 128U) || (velocity == 0U))
        {
            continue;
        }

        keyboard_arp_insert_sorted_unique(g_keyboard_arp.arp_pattern_notes,
                                          g_keyboard_arp.arp_pattern_vel,
                                          &g_keyboard_arp.arp_pattern_count,
                                          KBD_ARP_MAX_NOTES,
                                          note,
                                          velocity);
    }

    uint8_t rendered = 0U;
    if (g_keyboard_arp.arp_pattern_count > 0U)
    {
        rendered = keyboard_arp_render_window_samples(samples_per_step_q16,
                                                      duration_samples,
                                                      out_notes,
                                                      max_out_notes,
                                                      out_next_offset_q16);
    }

    g_keyboard_arp.arp_pattern_count = 0U;
    g_keyboard_arp.arp_pattern_source = (uint8_t)KBD_ARP_NOTE_SOURCE_KBD;
    if (g_keyboard_arp.arp_hold && (g_keyboard_arp.arp_latched_count > 0U))
    {
        keyboard_arp_activate_from_latched();
    }

    keyboard_arp_select_track(previous_track);
    return rendered;
}

void keyboard_arp_clear_seq_step_source(void)
{
    for (uint8_t track = 0U; track < TRACK_TOPOLOGY_PLAY_TRACK_COUNT; ++track)
    {
        keyboard_arp_select_track(track);
        keyboard_arp_release_seq_step_notes();
    }
    keyboard_arp_select_track(g_keyboard_arp_active_track);
}

void keyboard_arp_note_on_for_track(uint8_t track, uint8_t note, uint8_t velocity)
{
    keyboard_arp_ensure_config_initialized();
    if (keyboard_arp_track_is_valid(track) == 0U)
    {
        return;
    }

    const uint8_t previous_track = g_keyboard_arp_current_track;
    keyboard_arp_select_track(track);
    keyboard_arp_load_config(&g_keyboard_arp_config[track]);
    keyboard_arp_note_on(note, velocity);
    keyboard_arp_select_track(previous_track);
}

void keyboard_arp_note_off_for_track(uint8_t track, uint8_t note)
{
    if (keyboard_arp_track_is_valid(track) == 0U)
    {
        return;
    }

    const uint8_t previous_track = g_keyboard_arp_current_track;
    keyboard_arp_select_track(track);
    keyboard_arp_note_off(note);
    keyboard_arp_select_track(previous_track);
}

void keyboard_arp_all_notes_off_track(uint8_t track)
{
    if (keyboard_arp_track_is_valid(track) == 0U)
    {
        return;
    }

    const uint8_t previous_track = g_keyboard_arp_current_track;
    keyboard_arp_select_track(track);
    keyboard_arp_release_owned_notes();

    g_keyboard_arp.arp_phys_count = 0U;
    g_keyboard_arp.arp_latched_count = 0U;
    g_keyboard_arp.arp_pattern_count = 0U;
    g_keyboard_arp.arp_pattern_source = (uint8_t)KBD_ARP_NOTE_SOURCE_KBD;
    g_keyboard_arp.arp_latched_active = false;
    keyboard_arp_select_track(previous_track);
}

void keyboard_arp_all_notes_off(void)
{
    for (uint8_t track = 0U; track < TRACK_TOPOLOGY_PLAY_TRACK_COUNT; ++track)
    {
        keyboard_arp_all_notes_off_track(track);
    }
    keyboard_arp_select_track(g_keyboard_arp_active_track);
}

uint8_t keyboard_arp_has_hold_activity(void)
{
    for (uint8_t track = 0U; track < TRACK_TOPOLOGY_PLAY_TRACK_COUNT; ++track)
    {
        if (keyboard_arp_track_has_hold_activity(track) != 0U)
        {
            keyboard_arp_select_track(g_keyboard_arp_active_track);
            return 1U;
        }
    }
    keyboard_arp_select_track(g_keyboard_arp_active_track);
    return 0U;
}

void keyboard_arp_set_hold(bool enabled)
{
    keyboard_arp_ensure_config_initialized();
    g_keyboard_arp_config[g_keyboard_arp_active_track].hold = enabled;
    keyboard_arp_bump_config_revision(g_keyboard_arp_active_track);
    keyboard_arp_select_track(g_keyboard_arp_active_track);
    const bool previous = g_keyboard_arp.arp_hold;
    g_keyboard_arp.arp_hold = enabled;

    if (!enabled)
    {
        g_keyboard_arp.arp_latched_count = 0U;
        g_keyboard_arp.arp_latched_active = false;
        keyboard_arp_activate_from_phys();

        if (g_keyboard_arp.arp_phys_count == 0U)
        {
            g_keyboard_arp.arp_pattern_count = 0U;
            g_keyboard_arp.arp_pattern_source = (uint8_t)KBD_ARP_NOTE_SOURCE_KBD;
            keyboard_arp_release_owned_notes();
        }
    }
    else if (!previous)
    {
        keyboard_arp_copy_notes(g_keyboard_arp.arp_latched_notes,
                                g_keyboard_arp.arp_latched_vel,
                                &g_keyboard_arp.arp_latched_count,
                                g_keyboard_arp.arp_phys_notes,
                                g_keyboard_arp.arp_phys_vel,
                                g_keyboard_arp.arp_phys_count);
        g_keyboard_arp.arp_latched_active = (g_keyboard_arp.arp_latched_count > 0U);
    }
}

void keyboard_arp_set_hold_for_track(uint8_t track, bool enabled)
{
    keyboard_arp_ensure_config_initialized();
    if (keyboard_arp_track_is_valid(track) == 0U)
    {
        return;
    }

    const uint8_t previous_track = g_keyboard_arp_current_track;
    keyboard_arp_select_track(track);
    const bool previous = g_keyboard_arp.arp_hold;
    g_keyboard_arp_config[track].hold = enabled;
    keyboard_arp_bump_config_revision(track);
    g_keyboard_arp.arp_hold = enabled;

    if (!enabled)
    {
        g_keyboard_arp.arp_latched_count = 0U;
        g_keyboard_arp.arp_latched_active = false;
        keyboard_arp_activate_from_phys();
        if (g_keyboard_arp.arp_phys_count == 0U)
        {
            g_keyboard_arp.arp_pattern_count = 0U;
            g_keyboard_arp.arp_pattern_source = (uint8_t)KBD_ARP_NOTE_SOURCE_KBD;
            keyboard_arp_release_owned_notes();
        }
    }
    else if (!previous)
    {
        keyboard_arp_copy_notes(g_keyboard_arp.arp_latched_notes,
                                g_keyboard_arp.arp_latched_vel,
                                &g_keyboard_arp.arp_latched_count,
                                g_keyboard_arp.arp_phys_notes,
                                g_keyboard_arp.arp_phys_vel,
                                g_keyboard_arp.arp_phys_count);
        g_keyboard_arp.arp_latched_active = (g_keyboard_arp.arp_latched_count > 0U);
    }
    keyboard_arp_select_track(previous_track);
}

void keyboard_arp_set_rate_for_track(uint8_t track, uint8_t value)
{
    keyboard_arp_ensure_config_initialized();
    if (keyboard_arp_track_is_valid(track) == 0U)
        return;
    g_keyboard_arp_config[track].rate = (value > 7U) ? 7U : value;
    keyboard_arp_bump_config_revision(track);
    keyboard_arp_update_active_config(track);
}

void keyboard_arp_set_oct_for_track(uint8_t track, uint8_t value)
{
    keyboard_arp_ensure_config_initialized();
    if (keyboard_arp_track_is_valid(track) == 0U)
        return;
    g_keyboard_arp_config[track].oct = (value > 4U) ? 4U : value;
    keyboard_arp_bump_config_revision(track);
    keyboard_arp_update_active_config(track);
}

void keyboard_arp_set_pattern_for_track(uint8_t track, uint8_t value)
{
    keyboard_arp_ensure_config_initialized();
    if (keyboard_arp_track_is_valid(track) == 0U)
        return;
    g_keyboard_arp_config[track].pattern = (kbd_arp_pattern_t)((value >= (uint8_t)KBD_ARP_PATTERN_COUNT) ? 0U : value);
    keyboard_arp_bump_config_revision(track);
    keyboard_arp_update_active_config(track);
}

void keyboard_arp_set_gate_for_track(uint8_t track, uint8_t value)
{
    keyboard_arp_ensure_config_initialized();
    if (keyboard_arp_track_is_valid(track) == 0U)
        return;
    g_keyboard_arp_config[track].gate = (value > 100U) ? 100U : value;
    keyboard_arp_bump_config_revision(track);
    keyboard_arp_update_active_config(track);
}

void keyboard_arp_set_swing_for_track(uint8_t track, uint8_t value)
{
    keyboard_arp_ensure_config_initialized();
    if (keyboard_arp_track_is_valid(track) == 0U)
        return;
    g_keyboard_arp_config[track].swing = (value > 100U) ? 100U : value;
    keyboard_arp_bump_config_revision(track);
    keyboard_arp_update_active_config(track);
}

void keyboard_arp_set_accent_for_track(uint8_t track, uint8_t value)
{
    keyboard_arp_ensure_config_initialized();
    if (keyboard_arp_track_is_valid(track) == 0U)
        return;
    g_keyboard_arp_config[track].accent = (kbd_arp_accent_t)((value >= (uint8_t)KBD_ARP_ACCENT_COUNT) ? 0U : value);
    keyboard_arp_bump_config_revision(track);
    keyboard_arp_update_active_config(track);
}

void keyboard_arp_set_vel_acc_for_track(uint8_t track, uint8_t value)
{
    keyboard_arp_ensure_config_initialized();
    if (keyboard_arp_track_is_valid(track) == 0U)
        return;
    g_keyboard_arp_config[track].vel_acc = (value > 96U) ? 96U : value;
    keyboard_arp_bump_config_revision(track);
    keyboard_arp_update_active_config(track);
}

void keyboard_arp_set_strum_for_track(uint8_t track, uint8_t value)
{
    keyboard_arp_ensure_config_initialized();
    if (keyboard_arp_track_is_valid(track) == 0U)
        return;
    g_keyboard_arp_config[track].strum = (kbd_arp_strum_t)((value >= (uint8_t)KBD_ARP_STRUM_COUNT) ? 0U : value);
    keyboard_arp_bump_config_revision(track);
    keyboard_arp_update_active_config(track);
}

void keyboard_arp_set_offset_for_track(uint8_t track, int8_t value)
{
    keyboard_arp_ensure_config_initialized();
    if (keyboard_arp_track_is_valid(track) == 0U)
        return;
    g_keyboard_arp_config[track].offset = (value < 0) ? 0 : (value > 60 ? 60 : value);
    keyboard_arp_bump_config_revision(track);
    keyboard_arp_update_active_config(track);
}

void keyboard_arp_set_transpose_for_track(uint8_t track, int8_t value)
{
    keyboard_arp_ensure_config_initialized();
    if (keyboard_arp_track_is_valid(track) == 0U)
        return;
    g_keyboard_arp_config[track].trans = (value < -24) ? -24 : (value > 24 ? 24 : value);
    keyboard_arp_bump_config_revision(track);
    keyboard_arp_update_active_config(track);
}

void keyboard_arp_set_spread_for_track(uint8_t track, uint8_t value)
{
    keyboard_arp_ensure_config_initialized();
    if (keyboard_arp_track_is_valid(track) == 0U)
        return;
    g_keyboard_arp_config[track].spread = (value > 12U) ? 12U : value;
    keyboard_arp_bump_config_revision(track);
    keyboard_arp_update_active_config(track);
}

void keyboard_arp_set_dir_for_track(uint8_t track, uint8_t value)
{
    keyboard_arp_ensure_config_initialized();
    if (keyboard_arp_track_is_valid(track) == 0U)
        return;
    g_keyboard_arp_config[track].dir = (kbd_arp_dir_t)((value >= (uint8_t)KBD_ARP_DIR_COUNT) ? 0U : value);
    keyboard_arp_bump_config_revision(track);
    keyboard_arp_update_active_config(track);
}

void keyboard_arp_set_rate(uint8_t value) { keyboard_arp_set_rate_for_track(g_keyboard_arp_active_track, value); }
void keyboard_arp_set_oct(uint8_t value) { keyboard_arp_set_oct_for_track(g_keyboard_arp_active_track, value); }
void keyboard_arp_set_pattern(uint8_t value) { keyboard_arp_set_pattern_for_track(g_keyboard_arp_active_track, value); }
void keyboard_arp_set_gate(uint8_t value) { keyboard_arp_set_gate_for_track(g_keyboard_arp_active_track, value); }
void keyboard_arp_set_swing(uint8_t value) { keyboard_arp_set_swing_for_track(g_keyboard_arp_active_track, value); }
void keyboard_arp_set_accent(uint8_t value) { keyboard_arp_set_accent_for_track(g_keyboard_arp_active_track, value); }
void keyboard_arp_set_vel_acc(uint8_t value) { keyboard_arp_set_vel_acc_for_track(g_keyboard_arp_active_track, value); }
void keyboard_arp_set_strum(uint8_t value) { keyboard_arp_set_strum_for_track(g_keyboard_arp_active_track, value); }
void keyboard_arp_set_offset(int8_t value) { keyboard_arp_set_offset_for_track(g_keyboard_arp_active_track, value); }
void keyboard_arp_set_transpose(int8_t value) { keyboard_arp_set_transpose_for_track(g_keyboard_arp_active_track, value); }
void keyboard_arp_set_spread(uint8_t value) { keyboard_arp_set_spread_for_track(g_keyboard_arp_active_track, value); }
void keyboard_arp_set_dir(uint8_t value) { keyboard_arp_set_dir_for_track(g_keyboard_arp_active_track, value); }

void keyboard_arp_set_sync(uint8_t value)
{
    keyboard_arp_set_sync_for_track(g_keyboard_arp_active_track, value);
}

void keyboard_arp_set_sync_for_track(uint8_t track, uint8_t value)
{
    keyboard_arp_ensure_config_initialized();
    if (keyboard_arp_track_is_valid(track) == 0U)
        return;
    g_keyboard_arp_config[track].sync = (kbd_arp_sync_t)((value >= (uint8_t)KBD_ARP_SYNC_COUNT) ? 0U : value);
    keyboard_arp_bump_config_revision(track);
    if (track == g_keyboard_arp_active_track)
    {
        keyboard_arp_select_track(track);
        g_keyboard_arp.arp_sync = g_keyboard_arp_config[track].sync;
        keyboard_arp_reset_phrase(false);
        keyboard_arp_select_track(g_keyboard_arp_active_track);
    }
}

bool keyboard_arp_get_hold_for_track(uint8_t track) { keyboard_arp_ensure_config_initialized(); return (keyboard_arp_track_is_valid(track) != 0U) ? g_keyboard_arp_config[track].hold : false; }
uint8_t keyboard_arp_get_revision_for_track(uint8_t track) { keyboard_arp_ensure_config_initialized(); return (keyboard_arp_track_is_valid(track) != 0U) ? g_keyboard_arp_config_revision[track] : 0U; }
uint8_t keyboard_arp_get_rate_for_track(uint8_t track) { keyboard_arp_ensure_config_initialized(); return (keyboard_arp_track_is_valid(track) != 0U) ? g_keyboard_arp_config[track].rate : 2U; }
uint8_t keyboard_arp_get_oct_for_track(uint8_t track) { keyboard_arp_ensure_config_initialized(); return (keyboard_arp_track_is_valid(track) != 0U) ? g_keyboard_arp_config[track].oct : 0U; }
uint8_t keyboard_arp_get_pattern_for_track(uint8_t track) { keyboard_arp_ensure_config_initialized(); return (keyboard_arp_track_is_valid(track) != 0U) ? (uint8_t)g_keyboard_arp_config[track].pattern : 0U; }
uint8_t keyboard_arp_get_gate_for_track(uint8_t track) { keyboard_arp_ensure_config_initialized(); return (keyboard_arp_track_is_valid(track) != 0U) ? g_keyboard_arp_config[track].gate : 100U; }
uint8_t keyboard_arp_get_swing_for_track(uint8_t track) { keyboard_arp_ensure_config_initialized(); return (keyboard_arp_track_is_valid(track) != 0U) ? g_keyboard_arp_config[track].swing : 0U; }
uint8_t keyboard_arp_get_accent_for_track(uint8_t track) { keyboard_arp_ensure_config_initialized(); return (keyboard_arp_track_is_valid(track) != 0U) ? (uint8_t)g_keyboard_arp_config[track].accent : 0U; }
uint8_t keyboard_arp_get_vel_acc_for_track(uint8_t track) { keyboard_arp_ensure_config_initialized(); return (keyboard_arp_track_is_valid(track) != 0U) ? g_keyboard_arp_config[track].vel_acc : 24U; }
uint8_t keyboard_arp_get_strum_for_track(uint8_t track) { keyboard_arp_ensure_config_initialized(); return (keyboard_arp_track_is_valid(track) != 0U) ? (uint8_t)g_keyboard_arp_config[track].strum : 0U; }
int8_t keyboard_arp_get_offset_for_track(uint8_t track) { keyboard_arp_ensure_config_initialized(); return (keyboard_arp_track_is_valid(track) != 0U) ? g_keyboard_arp_config[track].offset : 0; }
int8_t keyboard_arp_get_transpose_for_track(uint8_t track) { keyboard_arp_ensure_config_initialized(); return (keyboard_arp_track_is_valid(track) != 0U) ? g_keyboard_arp_config[track].trans : 0; }
uint8_t keyboard_arp_get_spread_for_track(uint8_t track) { keyboard_arp_ensure_config_initialized(); return (keyboard_arp_track_is_valid(track) != 0U) ? g_keyboard_arp_config[track].spread : 0U; }
uint8_t keyboard_arp_get_dir_for_track(uint8_t track) { keyboard_arp_ensure_config_initialized(); return (keyboard_arp_track_is_valid(track) != 0U) ? (uint8_t)g_keyboard_arp_config[track].dir : 0U; }
uint8_t keyboard_arp_get_sync_for_track(uint8_t track) { keyboard_arp_ensure_config_initialized(); return (keyboard_arp_track_is_valid(track) != 0U) ? (uint8_t)g_keyboard_arp_config[track].sync : 0U; }

void keyboard_arp_on_mode_enter(void)
{
    keyboard_arp_select_track(g_keyboard_arp_active_track);
    keyboard_arp_reset_phrase(false);
}

void keyboard_arp_on_mode_enter_silent(void)
{
    keyboard_arp_select_track(g_keyboard_arp_active_track);
    keyboard_arp_reset_phrase(false);
}

void keyboard_arp_on_mode_leave(void)
{
    const uint8_t track = g_keyboard_arp_active_track;
    keyboard_arp_select_track(track);
    if (keyboard_arp_state_is_hold_owned() == 0U)
    {
        keyboard_arp_all_notes_off_track(track);
    }
    keyboard_arp_select_track(g_keyboard_arp_active_track);
}

void keyboard_arp_on_mode_leave_silent(void)
{
    const uint8_t track = g_keyboard_arp_active_track;
    keyboard_arp_select_track(track);
    if (keyboard_arp_state_is_hold_owned() == 0U)
    {
        g_keyboard_arp.arp_active_count = 0U;
        g_keyboard_arp.arp_active_seq_mask = 0U;
        g_keyboard_arp.arp_pending_on_count = 0U;
        g_keyboard_arp.arp_last_played_count = 0U;
    }
    keyboard_arp_select_track(g_keyboard_arp_active_track);
}

void keyboard_arp_clear_track(uint8_t track)
{
    if (keyboard_arp_track_is_valid(track) == 0U)
    {
        return;
    }

    const uint8_t previous_track = g_keyboard_arp_current_track;
    keyboard_arp_select_track(track);
    g_keyboard_arp.arp_phys_count = 0U;
    g_keyboard_arp.arp_latched_count = 0U;
    g_keyboard_arp.arp_latched_active = false;
    g_keyboard_arp.arp_pattern_count = 0U;
    g_keyboard_arp.arp_pattern_source = (uint8_t)KBD_ARP_NOTE_SOURCE_KBD;
    g_keyboard_arp.arp_active_count = 0U;
    g_keyboard_arp.arp_active_seq_mask = 0U;
    g_keyboard_arp.arp_pending_on_count = 0U;
    g_keyboard_arp.arp_last_played_count = 0U;
    g_keyboard_arp.arp_step_index = 0U;
    g_keyboard_arp.arp_pingpong_dir = 1;
    g_keyboard_arp.arp_strum_flip = false;
    g_keyboard_arp.arp_next_event_ms = HAL_GetTick();
    g_keyboard_arp.arp_last_step_ms = g_keyboard_arp.arp_next_event_ms;
    keyboard_arp_select_track(previous_track);
}

void keyboard_arp_clear_state_silent(void)
{
    for (uint8_t track = 0U; track < TRACK_TOPOLOGY_PLAY_TRACK_COUNT; ++track)
    {
        keyboard_arp_clear_track(track);
    }
    keyboard_arp_select_track(g_keyboard_arp_active_track);
}
