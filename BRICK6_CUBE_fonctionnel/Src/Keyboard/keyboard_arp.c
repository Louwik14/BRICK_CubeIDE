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
#include "stm32h7xx_hal.h"

#define KBD_ARP_MAX_NOTES 16U
#define KBD_ARP_MAX_CHORD_NOTES 16U
#define KBD_ARP_INTERNAL_BPM 120U
#define KBD_ARP_PENDING_MAX_NOTES 48U

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

    uint8_t arp_active_notes[KBD_ARP_MAX_NOTES];
    uint32_t arp_active_until[KBD_ARP_MAX_NOTES];
    uint8_t arp_active_count;

    uint8_t arp_pending_on_notes[KBD_ARP_PENDING_MAX_NOTES];
    uint8_t arp_pending_on_vel[KBD_ARP_PENDING_MAX_NOTES];
    uint32_t arp_pending_on_time[KBD_ARP_PENDING_MAX_NOTES];
    uint8_t arp_pending_on_count;

    uint8_t arp_last_played[KBD_ARP_MAX_CHORD_NOTES];
    uint8_t arp_last_played_count;
    uint8_t arp_step_index;
    int8_t arp_pingpong_dir;
    bool arp_strum_flip;
    uint32_t arp_last_step_ms;
    uint32_t arp_clock_pulse_count;
    uint32_t arp_next_event_ms;
    uint32_t arp_random_seed;
} keyboard_arp_state_t;

static keyboard_arp_state_t g_keyboard_arp = {
    .arp_hold = false,
    .arp_rate = 2U,
    .arp_oct = 0U,
    .arp_pattern = KBD_ARP_PATTERN_UP,
    .arp_gate = 100U,
    .arp_swing = 0U,
    .arp_accent = KBD_ARP_ACCENT_OFF,
    .arp_vel_acc = 24U,
    .arp_strum = KBD_ARP_STRUM_OFF,
    .arp_offset = 0,
    .arp_trans = 0,
    .arp_spread = 0U,
    .arp_dir = KBD_ARP_DIR_NORMAL,
    .arp_sync = KBD_ARP_SYNC_INT,
    .arp_random_seed = 0x12345U,
};

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
}

static void keyboard_arp_schedule_note_off(uint8_t note, uint32_t off_time)
{
    if (g_keyboard_arp.arp_active_count >= KBD_ARP_MAX_NOTES)
    {
        return;
    }

    const uint8_t i = g_keyboard_arp.arp_active_count;
    g_keyboard_arp.arp_active_notes[i] = note;
    g_keyboard_arp.arp_active_until[i] = off_time;
    g_keyboard_arp.arp_active_count++;
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
    g_keyboard_arp.arp_pending_on_count++;
}

static void keyboard_arp_dispatch_pending_note_on(uint32_t now, uint32_t gate_ms)
{
    uint8_t w = 0U;

    for (uint8_t i = 0U; i < g_keyboard_arp.arp_pending_on_count; ++i)
    {
        if (g_keyboard_arp.arp_pending_on_time[i] <= now)
        {
            const uint32_t on_time = g_keyboard_arp.arp_pending_on_time[i];
            keyboard_engine_note_on(g_keyboard_arp.arp_pending_on_notes[i],
                                    g_keyboard_arp.arp_pending_on_vel[i]);
            keyboard_arp_schedule_note_off(g_keyboard_arp.arp_pending_on_notes[i],
                                           on_time + gate_ms);
        }
        else
        {
            g_keyboard_arp.arp_pending_on_notes[w] = g_keyboard_arp.arp_pending_on_notes[i];
            g_keyboard_arp.arp_pending_on_vel[w] = g_keyboard_arp.arp_pending_on_vel[i];
            g_keyboard_arp.arp_pending_on_time[w] = g_keyboard_arp.arp_pending_on_time[i];
            ++w;
        }
    }

    g_keyboard_arp.arp_pending_on_count = w;
}

static void keyboard_arp_dispatch_note_off(uint32_t now)
{
    uint8_t w = 0U;

    for (uint8_t i = 0U; i < g_keyboard_arp.arp_active_count; ++i)
    {
        if (g_keyboard_arp.arp_active_until[i] <= now)
        {
            keyboard_engine_note_off(g_keyboard_arp.arp_active_notes[i]);
        }
        else
        {
            g_keyboard_arp.arp_active_notes[w] = g_keyboard_arp.arp_active_notes[i];
            g_keyboard_arp.arp_active_until[w] = g_keyboard_arp.arp_active_until[i];
            ++w;
        }
    }

    g_keyboard_arp.arp_active_count = w;
}

static void keyboard_arp_reset_phrase(bool stop_notes)
{
    if (stop_notes)
    {
        keyboard_engine_all_notes_off();
        g_keyboard_arp.arp_active_count = 0U;
        g_keyboard_arp.arp_pending_on_count = 0U;
        g_keyboard_arp.arp_last_played_count = 0U;
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
        return (uint8_t)(HAL_GetTick() % count);
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
        const int8_t step = ((HAL_GetTick() & 0x1U) == 0U) ? 1 : -1;
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
        keyboard_engine_note_on(notes[0], vel);
        keyboard_arp_schedule_note_off(notes[0], now + gate_ms);
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

void keyboard_arp_init(void)
{
    g_keyboard_arp.arp_next_event_ms = HAL_GetTick();
}

void keyboard_arp_tick(void)
{
    const uint32_t now = HAL_GetTick();
    const uint32_t base_period = keyboard_arp_step_interval_ms();
    const uint32_t gate_ms = keyboard_arp_gate_ms(base_period);

    keyboard_arp_dispatch_pending_note_on(now, gate_ms);
    keyboard_arp_dispatch_note_off(now);

    keyboard_arp_try_start(now);

    if (g_keyboard_arp.arp_pattern_count == 0U)
    {
        return;
    }

    if (!keyboard_arp_should_tick(now, base_period))
    {
        return;
    }

    if (g_keyboard_arp.arp_next_event_ms > now)
    {
        return;
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
        keyboard_engine_all_notes_off();
        g_keyboard_arp.arp_active_count = 0U;
        g_keyboard_arp.arp_pending_on_count = 0U;
        g_keyboard_arp.arp_last_played_count = 0U;
    }
}

void keyboard_arp_all_notes_off(void)
{
    keyboard_engine_all_notes_off();

    g_keyboard_arp.arp_phys_count = 0U;
    g_keyboard_arp.arp_latched_count = 0U;
    g_keyboard_arp.arp_pattern_count = 0U;
    g_keyboard_arp.arp_active_count = 0U;
    g_keyboard_arp.arp_pending_on_count = 0U;
    g_keyboard_arp.arp_latched_active = false;
    g_keyboard_arp.arp_last_played_count = 0U;
}

void keyboard_arp_set_hold(bool enabled)
{
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
            g_keyboard_arp.arp_active_count = 0U;
            g_keyboard_arp.arp_pending_on_count = 0U;
            keyboard_engine_all_notes_off();
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

void keyboard_arp_set_rate(uint8_t value) { g_keyboard_arp.arp_rate = (value > 7U) ? 7U : value; }
void keyboard_arp_set_oct(uint8_t value) { g_keyboard_arp.arp_oct = (value > 4U) ? 4U : value; }
void keyboard_arp_set_pattern(uint8_t value) { g_keyboard_arp.arp_pattern = (kbd_arp_pattern_t)((value >= (uint8_t)KBD_ARP_PATTERN_COUNT) ? 0U : value); }
void keyboard_arp_set_gate(uint8_t value) { g_keyboard_arp.arp_gate = (value > 100U) ? 100U : value; }
void keyboard_arp_set_swing(uint8_t value) { g_keyboard_arp.arp_swing = (value > 100U) ? 100U : value; }
void keyboard_arp_set_accent(uint8_t value) { g_keyboard_arp.arp_accent = (kbd_arp_accent_t)((value >= (uint8_t)KBD_ARP_ACCENT_COUNT) ? 0U : value); }
void keyboard_arp_set_vel_acc(uint8_t value) { g_keyboard_arp.arp_vel_acc = (value > 96U) ? 96U : value; }
void keyboard_arp_set_strum(uint8_t value) { g_keyboard_arp.arp_strum = (kbd_arp_strum_t)((value >= (uint8_t)KBD_ARP_STRUM_COUNT) ? 0U : value); }
void keyboard_arp_set_offset(int8_t value) { g_keyboard_arp.arp_offset = (value < 0) ? 0 : (value > 60 ? 60 : value); }
void keyboard_arp_set_transpose(int8_t value) { g_keyboard_arp.arp_trans = (value < -24) ? -24 : (value > 24 ? 24 : value); }
void keyboard_arp_set_spread(uint8_t value) { g_keyboard_arp.arp_spread = (value > 12U) ? 12U : value; }
void keyboard_arp_set_dir(uint8_t value) { g_keyboard_arp.arp_dir = (kbd_arp_dir_t)((value >= (uint8_t)KBD_ARP_DIR_COUNT) ? 0U : value); }

void keyboard_arp_set_sync(uint8_t value)
{
    g_keyboard_arp.arp_sync = (kbd_arp_sync_t)((value >= (uint8_t)KBD_ARP_SYNC_COUNT) ? 0U : value);
    g_keyboard_arp.arp_clock_pulse_count = 0U;
    keyboard_arp_reset_phrase(false);
}

void keyboard_arp_on_mode_enter(void)
{
    keyboard_arp_reset_phrase(true);
}

void keyboard_arp_on_mode_leave(void)
{
    keyboard_engine_all_notes_off();
    g_keyboard_arp.arp_active_count = 0U;
    g_keyboard_arp.arp_pending_on_count = 0U;
    g_keyboard_arp.arp_last_played_count = 0U;
}

void keyboard_arp_on_mode_leave_silent(void)
{
    g_keyboard_arp.arp_active_count = 0U;
    g_keyboard_arp.arp_pending_on_count = 0U;
    g_keyboard_arp.arp_last_played_count = 0U;
}
