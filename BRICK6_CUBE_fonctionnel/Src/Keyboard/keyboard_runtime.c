#include "Keyboard/keyboard_runtime.h"

#include "Keyboard/kbd_input_mapper.h"
#include "Audio/microdexed_synth.h"
#include "Audio/mixer.h"
#include "Audio/monob_synth.h"
#include "MIDI/midi.h"
#include "stm32h7xx_hal.h"
#include "ui_core.h"

#define KBD_ARP_MAX_NOTES 16U
#define KBD_ARP_MAX_CHORD_NOTES 4U
#define KBD_ARP_INTERNAL_BPM 120U
#define KBD_ARP_MIDI_CHANNEL 0U

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
    uint8_t root_index;
    uint8_t scale_index;
    bool omnichord;
    note_order_t note_order;
    bool chord_override;
    ui_track_type_t sounding_type;
    bool sounding_active;

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

    uint8_t arp_source_notes[KBD_ARP_MAX_NOTES];
    uint8_t arp_source_count;
    uint8_t arp_live_refcount[128];
    uint8_t arp_last_played[KBD_ARP_MAX_CHORD_NOTES];
    uint8_t arp_last_played_count;
    uint8_t arp_step_index;
    int8_t arp_pingpong_dir;
    bool arp_strum_flip;
    uint32_t arp_last_step_ms;
    uint32_t arp_clock_pulse_count;
} keyboard_runtime_state_t;

static keyboard_runtime_state_t g_keyboard_runtime = {
    .root_index = 0U,
    .scale_index = (uint8_t)KBD_SCALE_MAJOR,
    .omnichord = false,
    .note_order = NOTE_ORDER_NATURAL,
    .chord_override = false,
    .sounding_type = UI_TRACK_TYPE_DX7,
    .sounding_active = false,
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
    .arp_source_notes = { 0U },
    .arp_source_count = 0U,
    .arp_live_refcount = { 0U },
    .arp_last_played = { 0U },
    .arp_last_played_count = 0U,
    .arp_step_index = 0U,
    .arp_pingpong_dir = 1,
    .arp_strum_flip = false,
    .arp_last_step_ms = 0U,
    .arp_clock_pulse_count = 0U,
};

static bool keyboard_runtime_active_track_is_synth(void)
{
    return (ui_get_track_family(ui_get_active_track()) == UI_TRACK_FAMILY_SYNTH);
}

static bool keyboard_runtime_active_track_has_midi_note_path(void)
{
    const ui_track_config_t config = ui_get_track_config(ui_get_active_track());
    return (config.family == UI_TRACK_FAMILY_SYNTH) || (config.type == UI_TRACK_TYPE_HYBRID);
}

static ui_track_type_t keyboard_runtime_get_active_synth_type(void)
{
    return ui_get_track_type(ui_get_active_track());
}

static uint8_t keyboard_runtime_get_filter_target_track(void)
{
    uint8_t track_id = 0U;
    if (ui_resolve_filter_target_track(&track_id))
    {
        return track_id;
    }
    return 0xFFU;
}

static void keyboard_runtime_engine_note_on(uint8_t note, uint8_t velocity)
{
    const uint8_t filter_track = keyboard_runtime_get_filter_target_track();
    if (filter_track != 0xFFU)
    {
        mixer_track_filter_note_on(filter_track, note, velocity);
    }

    if (!keyboard_runtime_active_track_has_midi_note_path())
    {
        return;
    }

    midi_note_on(MIDI_DEST_USB, KBD_ARP_MIDI_CHANNEL, note, velocity);

    if (!keyboard_runtime_active_track_is_synth())
    {
        return;
    }

    const ui_track_type_t synth_type = keyboard_runtime_get_active_synth_type();
    g_keyboard_runtime.sounding_type = synth_type;
    g_keyboard_runtime.sounding_active = true;

    if (synth_type == UI_TRACK_TYPE_MONOB)
    {
        monob_synth_note_on(note, velocity);
    }
    else
    {
        microdexed_synth_note_on(note, velocity);
    }
}

static void keyboard_runtime_engine_note_off(uint8_t note)
{
    const uint8_t filter_track = keyboard_runtime_get_filter_target_track();
    if (filter_track != 0xFFU)
    {
        mixer_track_filter_note_off(filter_track, note);
    }

    if (keyboard_runtime_active_track_has_midi_note_path())
    {
        midi_note_off(MIDI_DEST_USB, KBD_ARP_MIDI_CHANNEL, note, 0U);
    }

    if (!keyboard_runtime_active_track_is_synth() && !g_keyboard_runtime.sounding_active)
    {
        return;
    }

    const ui_track_type_t synth_type = g_keyboard_runtime.sounding_active ? g_keyboard_runtime.sounding_type : keyboard_runtime_get_active_synth_type();

    if (synth_type == UI_TRACK_TYPE_MONOB)
    {
        monob_synth_note_off(note);
    }
    else
    {
        microdexed_synth_note_off(note);
    }
}

static void keyboard_runtime_engine_all_notes_off(void)
{
    microdexed_synth_all_notes_off();
    monob_synth_all_notes_off();
    const uint8_t filter_track = keyboard_runtime_get_filter_target_track();
    if (filter_track != 0xFFU)
    {
        mixer_track_filter_all_notes_off(filter_track);
    }

    if (keyboard_runtime_active_track_has_midi_note_path())
    {
        midi_all_notes_off(MIDI_DEST_USB, KBD_ARP_MIDI_CHANNEL);
    }

    g_keyboard_runtime.sounding_active = false;
}

static uint32_t keyboard_runtime_arp_step_interval_ms(void)
{
    static const uint16_t ppqn24_per_step[] = {24U, 12U, 6U, 3U, 16U, 8U, 4U, 2U};
    const uint8_t rate = (g_keyboard_runtime.arp_rate > 7U) ? 7U : g_keyboard_runtime.arp_rate;
    const uint32_t quarter_note_ms = (60000U / KBD_ARP_INTERNAL_BPM);
    return (quarter_note_ms * ppqn24_per_step[rate]) / 24U;
}

static void keyboard_runtime_arp_release_last_notes(void)
{
    for (uint8_t i = 0U; i < g_keyboard_runtime.arp_last_played_count; ++i)
    {
        keyboard_runtime_engine_note_off(g_keyboard_runtime.arp_last_played[i]);
    }
    g_keyboard_runtime.arp_last_played_count = 0U;
}

static void keyboard_runtime_arp_reset_phrase(bool stop_notes)
{
    if (stop_notes)
    {
        keyboard_runtime_arp_release_last_notes();
    }

    g_keyboard_runtime.arp_step_index = 0U;
    g_keyboard_runtime.arp_pingpong_dir = 1;
    g_keyboard_runtime.arp_strum_flip = false;
    g_keyboard_runtime.arp_last_step_ms = HAL_GetTick();
}

static bool keyboard_runtime_arp_source_contains(uint8_t note)
{
    for (uint8_t i = 0U; i < g_keyboard_runtime.arp_source_count; ++i)
    {
        if (g_keyboard_runtime.arp_source_notes[i] == note)
        {
            return true;
        }
    }
    return false;
}

static void keyboard_runtime_arp_source_add(uint8_t note)
{
    if (keyboard_runtime_arp_source_contains(note) || (g_keyboard_runtime.arp_source_count >= KBD_ARP_MAX_NOTES))
    {
        return;
    }

    uint8_t index = g_keyboard_runtime.arp_source_count;
    while ((index > 0U) && (g_keyboard_runtime.arp_source_notes[index - 1U] > note))
    {
        g_keyboard_runtime.arp_source_notes[index] = g_keyboard_runtime.arp_source_notes[index - 1U];
        --index;
    }

    g_keyboard_runtime.arp_source_notes[index] = note;
    g_keyboard_runtime.arp_source_count++;
}

static void keyboard_runtime_arp_source_remove(uint8_t note)
{
    for (uint8_t i = 0U; i < g_keyboard_runtime.arp_source_count; ++i)
    {
        if (g_keyboard_runtime.arp_source_notes[i] != note)
        {
            continue;
        }

        for (uint8_t j = i; j + 1U < g_keyboard_runtime.arp_source_count; ++j)
        {
            g_keyboard_runtime.arp_source_notes[j] = g_keyboard_runtime.arp_source_notes[j + 1U];
        }

        if (g_keyboard_runtime.arp_source_count > 0U)
        {
            g_keyboard_runtime.arp_source_count--;
        }
        return;
    }
}

static uint8_t keyboard_runtime_arp_apply_step_accent(uint8_t velocity, uint8_t step_index)
{
    bool accented = false;

    switch (g_keyboard_runtime.arp_accent)
    {
        case KBD_ARP_ACCENT_1ST:
            accented = ((step_index % 4U) == 0U);
            break;

        case KBD_ARP_ACCENT_ALT:
            accented = ((step_index & 0x1U) != 0U);
            break;

        case KBD_ARP_ACCENT_RND:
            accented = ((uint8_t)(HAL_GetTick() & 0x3U) == 0U);
            break;

        case KBD_ARP_ACCENT_OFF:
        default:
            accented = false;
            break;
    }

    if (!accented)
    {
        return velocity;
    }

    uint16_t boosted = (uint16_t)velocity + g_keyboard_runtime.arp_vel_acc;
    if (boosted > 127U)
    {
        boosted = 127U;
    }
    return (uint8_t)boosted;
}

static void keyboard_runtime_arp_emit_notes(const uint8_t *notes, uint8_t count, uint8_t velocity)
{
    keyboard_runtime_arp_release_last_notes();

    const uint8_t capped = (count > KBD_ARP_MAX_CHORD_NOTES) ? KBD_ARP_MAX_CHORD_NOTES : count;
    for (uint8_t i = 0U; i < capped; ++i)
    {
        keyboard_runtime_engine_note_on(notes[i], velocity);
        g_keyboard_runtime.arp_last_played[i] = notes[i];
    }

    g_keyboard_runtime.arp_last_played_count = capped;
}

static uint8_t keyboard_runtime_arp_next_base_index(uint8_t count)
{
    if (count == 0U)
    {
        return 0U;
    }

    if (g_keyboard_runtime.arp_pattern == KBD_ARP_PATTERN_RND)
    {
        return (uint8_t)(HAL_GetTick() % count);
    }

    if ((g_keyboard_runtime.arp_pattern == KBD_ARP_PATTERN_DOWN)
            || (g_keyboard_runtime.arp_strum == KBD_ARP_STRUM_DOWN)
            || ((g_keyboard_runtime.arp_strum == KBD_ARP_STRUM_ALT) && g_keyboard_runtime.arp_strum_flip))
    {
        return (uint8_t)((count - 1U) - (g_keyboard_runtime.arp_step_index % count));
    }

    if (g_keyboard_runtime.arp_pattern == KBD_ARP_PATTERN_UPDN)
    {
        const uint8_t period = (count <= 1U) ? 1U : (uint8_t)((count * 2U) - 2U);
        const uint8_t phase = (period == 0U) ? 0U : (uint8_t)(g_keyboard_runtime.arp_step_index % period);

        if (phase < count)
        {
            return phase;
        }

        return (uint8_t)(period - phase);
    }

    return (uint8_t)(g_keyboard_runtime.arp_step_index % count);
}

static uint8_t keyboard_runtime_arp_compute_octave(uint8_t step_index)
{
    const uint8_t oct_count = (g_keyboard_runtime.arp_oct > 4U) ? 4U : g_keyboard_runtime.arp_oct;
    if (oct_count == 0U)
    {
        return 0U;
    }

    if (g_keyboard_runtime.arp_dir == KBD_ARP_DIR_RNDWALK)
    {
        const int8_t step = ((HAL_GetTick() & 0x1U) == 0U) ? 1 : -1;
        const int8_t current = (int8_t)(g_keyboard_runtime.arp_step_index % (oct_count + 1U));
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

    if (g_keyboard_runtime.arp_dir == KBD_ARP_DIR_PINGPONG)
    {
        const uint8_t period = (uint8_t)((oct_count * 2U) + 1U);
        const uint8_t phase = (period == 0U) ? 0U : (uint8_t)(step_index % period);
        return (phase <= oct_count) ? phase : (uint8_t)(period - phase);
    }

    return (uint8_t)(step_index % (oct_count + 1U));
}

static void keyboard_runtime_arp_play_step(void)
{
    const uint8_t src_count = g_keyboard_runtime.arp_source_count;
    if (src_count == 0U)
    {
        keyboard_runtime_arp_release_last_notes();
        return;
    }

    uint8_t notes[KBD_ARP_MAX_CHORD_NOTES];
    uint8_t out_count = 0U;

    const uint8_t base_index = keyboard_runtime_arp_next_base_index(src_count);
    const uint8_t octave = keyboard_runtime_arp_compute_octave(g_keyboard_runtime.arp_step_index);
    const int16_t octave_offset = (int16_t)octave * 12;
    const int16_t trans = (int16_t)g_keyboard_runtime.arp_trans + (int16_t)g_keyboard_runtime.arp_offset;

    if (g_keyboard_runtime.arp_pattern == KBD_ARP_PATTERN_CHORD)
    {
        for (uint8_t i = 0U; (i < src_count) && (out_count < KBD_ARP_MAX_CHORD_NOTES); ++i)
        {
            uint8_t source_idx = i;
            if ((g_keyboard_runtime.arp_strum == KBD_ARP_STRUM_DOWN)
                    || ((g_keyboard_runtime.arp_strum == KBD_ARP_STRUM_ALT) && g_keyboard_runtime.arp_strum_flip))
            {
                source_idx = (uint8_t)((src_count - 1U) - i);
            }
            else if (g_keyboard_runtime.arp_strum == KBD_ARP_STRUM_RND)
            {
                source_idx = (uint8_t)((HAL_GetTick() + i) % src_count);
            }

            int16_t note = (int16_t)g_keyboard_runtime.arp_source_notes[source_idx] + octave_offset + trans + ((int16_t)g_keyboard_runtime.arp_spread * (int16_t)i);
            if (note < 0)
            {
                note = 0;
            }
            if (note > 127)
            {
                note = 127;
            }

            notes[out_count++] = (uint8_t)note;
        }
    }
    else
    {
        int16_t note = (int16_t)g_keyboard_runtime.arp_source_notes[base_index] + octave_offset + trans;
        if (note < 0)
        {
            note = 0;
        }
        if (note > 127)
        {
            note = 127;
        }

        notes[out_count++] = (uint8_t)note;
    }

    uint8_t velocity = keyboard_runtime_arp_apply_step_accent(100U, g_keyboard_runtime.arp_step_index);
    keyboard_runtime_arp_emit_notes(notes, out_count, velocity);

    g_keyboard_runtime.arp_step_index++;
    if (g_keyboard_runtime.arp_strum == KBD_ARP_STRUM_ALT)
    {
        g_keyboard_runtime.arp_strum_flip = !g_keyboard_runtime.arp_strum_flip;
    }
}

static bool keyboard_runtime_arp_should_tick(uint32_t now, uint32_t interval_ms)
{
    if (interval_ms == 0U)
    {
        return false;
    }

    if (g_keyboard_runtime.arp_sync == KBD_ARP_SYNC_FREE)
    {
        return ((now - g_keyboard_runtime.arp_last_step_ms) >= interval_ms);
    }

    if (g_keyboard_runtime.arp_sync == KBD_ARP_SYNC_CLOCK)
    {
        /* Future external clock hook point. For now: deterministic fallback to internal timing. */
        return ((now - g_keyboard_runtime.arp_last_step_ms) >= interval_ms);
    }

    return ((now - g_keyboard_runtime.arp_last_step_ms) >= interval_ms);
}

static void keyboard_runtime_arp_note_on(uint8_t note)
{
    if (g_keyboard_runtime.arp_live_refcount[note] < 0xFFU)
    {
        g_keyboard_runtime.arp_live_refcount[note]++;
    }

    keyboard_runtime_arp_source_add(note);

    if (g_keyboard_runtime.arp_source_count == 1U)
    {
        keyboard_runtime_arp_reset_phrase(true);
    }
}

static void keyboard_runtime_arp_note_off(uint8_t note)
{
    if (g_keyboard_runtime.arp_live_refcount[note] > 0U)
    {
        g_keyboard_runtime.arp_live_refcount[note]--;
    }

    if (g_keyboard_runtime.arp_hold)
    {
        return;
    }

    if (g_keyboard_runtime.arp_live_refcount[note] == 0U)
    {
        keyboard_runtime_arp_source_remove(note);
    }

    if (g_keyboard_runtime.arp_source_count == 0U)
    {
        keyboard_runtime_arp_release_last_notes();
    }
}

static void keyboard_runtime_note_on_sink(uint8_t note, uint8_t velocity)
{
    (void)velocity;

    if (ui_get_hall_mode() == UI_HALL_MODE_ARP)
    {
        keyboard_runtime_arp_note_on(note);
        return;
    }

    keyboard_runtime_engine_note_on(note, velocity);
}

static void keyboard_runtime_note_off_sink(uint8_t note)
{
    if (ui_get_hall_mode() == UI_HALL_MODE_ARP)
    {
        keyboard_runtime_arp_note_off(note);
        return;
    }

    keyboard_runtime_engine_note_off(note);
}

static void keyboard_runtime_all_notes_off_sink(void)
{
    keyboard_runtime_engine_all_notes_off();

    g_keyboard_runtime.arp_source_count = 0U;
    for (uint16_t i = 0U; i < 128U; ++i)
    {
        g_keyboard_runtime.arp_live_refcount[i] = 0U;
    }

    g_keyboard_runtime.arp_last_played_count = 0U;
}

static void keyboard_runtime_apply_params(void)
{
    ui_keyboard_app_set_params((uint8_t)(60U + (g_keyboard_runtime.root_index % 12U)),
                               (kbd_scale_t)g_keyboard_runtime.scale_index,
                               g_keyboard_runtime.omnichord);
    ui_keyboard_app_set_note_order(g_keyboard_runtime.note_order);
    ui_keyboard_app_set_chord_override(g_keyboard_runtime.chord_override);
    kbd_input_mapper_set_omnichord_state(g_keyboard_runtime.omnichord);
}

void keyboard_runtime_init(void)
{
    const ui_keyboard_note_sink_t sink = {
        .note_on = keyboard_runtime_note_on_sink,
        .note_off = keyboard_runtime_note_off_sink,
        .all_notes_off = keyboard_runtime_all_notes_off_sink,
        .velocity = 100U,
    };

    ui_keyboard_app_init(&sink);
    kbd_input_mapper_init(g_keyboard_runtime.omnichord);
    keyboard_runtime_apply_params();
}

void keyboard_runtime_tick(void)
{
    ui_keyboard_app_tick(0U);

    if (ui_get_hall_mode() != UI_HALL_MODE_ARP)
    {
        return;
    }

    const uint32_t interval_ms = keyboard_runtime_arp_step_interval_ms();
    const uint32_t now = HAL_GetTick();
    if (!keyboard_runtime_arp_should_tick(now, interval_ms))
    {
        return;
    }

    g_keyboard_runtime.arp_last_step_ms = now;
    keyboard_runtime_arp_play_step();
}

void keyboard_runtime_set_root(uint8_t root_index)
{
    g_keyboard_runtime.root_index = (uint8_t)(root_index % 12U);
    keyboard_runtime_apply_params();
}

void keyboard_runtime_set_scale(uint8_t scale_index)
{
    if (scale_index > (uint8_t)KBD_SCALE_CHROMATIC)
    {
        scale_index = (uint8_t)KBD_SCALE_CHROMATIC;
    }

    g_keyboard_runtime.scale_index = scale_index;
    keyboard_runtime_apply_params();
}

void keyboard_runtime_set_omnichord(bool enabled)
{
    g_keyboard_runtime.omnichord = enabled;
    keyboard_runtime_apply_params();
}

void keyboard_runtime_set_note_order(note_order_t order)
{
    g_keyboard_runtime.note_order = order;
    keyboard_runtime_apply_params();
}

void keyboard_runtime_set_chord_override(bool enabled)
{
    g_keyboard_runtime.chord_override = enabled;
    keyboard_runtime_apply_params();
}

void keyboard_runtime_set_arp_hold(bool enabled)
{
    if ((g_keyboard_runtime.arp_hold != enabled) && !enabled)
    {
        for (uint16_t note = 0U; note < 128U; ++note)
        {
            if (g_keyboard_runtime.arp_live_refcount[note] == 0U)
            {
                keyboard_runtime_arp_source_remove((uint8_t)note);
            }
        }

        if (g_keyboard_runtime.arp_source_count == 0U)
        {
            keyboard_runtime_arp_release_last_notes();
        }
    }

    g_keyboard_runtime.arp_hold = enabled;
}

void keyboard_runtime_set_arp_rate(uint8_t value) { g_keyboard_runtime.arp_rate = (value > 7U) ? 7U : value; }
void keyboard_runtime_set_arp_oct(uint8_t value) { g_keyboard_runtime.arp_oct = (value > 4U) ? 4U : value; }
void keyboard_runtime_set_arp_pattern(uint8_t value) { g_keyboard_runtime.arp_pattern = (kbd_arp_pattern_t)((value >= (uint8_t)KBD_ARP_PATTERN_COUNT) ? 0U : value); }
void keyboard_runtime_set_arp_gate(uint8_t value) { g_keyboard_runtime.arp_gate = (value > 127U) ? 127U : value; }
void keyboard_runtime_set_arp_swing(uint8_t value) { g_keyboard_runtime.arp_swing = (value > 100U) ? 100U : value; }
void keyboard_runtime_set_arp_accent(uint8_t value) { g_keyboard_runtime.arp_accent = (kbd_arp_accent_t)((value >= (uint8_t)KBD_ARP_ACCENT_COUNT) ? 0U : value); }
void keyboard_runtime_set_arp_vel_acc(uint8_t value) { g_keyboard_runtime.arp_vel_acc = (value > 64U) ? 64U : value; }
void keyboard_runtime_set_arp_strum(uint8_t value) { g_keyboard_runtime.arp_strum = (kbd_arp_strum_t)((value >= (uint8_t)KBD_ARP_STRUM_COUNT) ? 0U : value); }
void keyboard_runtime_set_arp_offset(int8_t value) { g_keyboard_runtime.arp_offset = (value < -24) ? -24 : (value > 24 ? 24 : value); }
void keyboard_runtime_set_arp_transpose(int8_t value) { g_keyboard_runtime.arp_trans = (value < -24) ? -24 : (value > 24 ? 24 : value); }
void keyboard_runtime_set_arp_spread(uint8_t value) { g_keyboard_runtime.arp_spread = (value > 12U) ? 12U : value; }
void keyboard_runtime_set_arp_dir(uint8_t value) { g_keyboard_runtime.arp_dir = (kbd_arp_dir_t)((value >= (uint8_t)KBD_ARP_DIR_COUNT) ? 0U : value); }
void keyboard_runtime_set_arp_sync(uint8_t value)
{
    g_keyboard_runtime.arp_sync = (kbd_arp_sync_t)((value >= (uint8_t)KBD_ARP_SYNC_COUNT) ? 0U : value);
    g_keyboard_runtime.arp_clock_pulse_count = 0U;
    keyboard_runtime_arp_reset_phrase(false);
}

void keyboard_runtime_step_octave(int8_t delta)
{
    ui_keyboard_app_set_octave_shift((int8_t)(ui_keyboard_app_get_octave_shift() + delta));
}

void keyboard_runtime_process_hall(uint8_t hall_index, bool pressed, uint8_t velocity)
{
    ui_keyboard_app_set_velocity(velocity);
    kbd_input_mapper_process((uint8_t)(hall_index + 1U), pressed);
}

void keyboard_runtime_all_notes_off(void)
{
    ui_keyboard_app_all_notes_off();
    g_keyboard_runtime.sounding_active = false;
    keyboard_runtime_arp_release_last_notes();
}

void keyboard_runtime_on_active_track_changed(void)
{
    keyboard_runtime_all_notes_off();
}

void keyboard_runtime_on_hall_mode_changed(ui_hall_mode_t previous_mode, ui_hall_mode_t new_mode)
{
    if ((previous_mode == UI_HALL_MODE_ARP) && (new_mode != UI_HALL_MODE_ARP))
    {
        keyboard_runtime_arp_release_last_notes();
    }

    if ((new_mode == UI_HALL_MODE_ARP) && (previous_mode != UI_HALL_MODE_ARP))
    {
        keyboard_runtime_arp_reset_phrase(true);
    }
}

uint8_t keyboard_runtime_get_root_index(void)
{
    return g_keyboard_runtime.root_index;
}

uint8_t keyboard_runtime_get_scale_index(void)
{
    return g_keyboard_runtime.scale_index;
}

bool keyboard_runtime_get_omnichord(void)
{
    return g_keyboard_runtime.omnichord;
}

note_order_t keyboard_runtime_get_note_order(void)
{
    return g_keyboard_runtime.note_order;
}

bool keyboard_runtime_get_chord_override(void)
{
    return g_keyboard_runtime.chord_override;
}

int8_t keyboard_runtime_get_octave_shift(void)
{
    return ui_keyboard_app_get_octave_shift();
}
