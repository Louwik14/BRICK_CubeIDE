/*
 * Module: seq_model
 * Role: Modèle de données central du séquenceur (project/tracks/steps/plock pool).
 * Responsibilities: CRUD sur trigs/pages/plocks, validations d'index,
 * allocation/libération du pool de locks et accès cohérent à l'état persistant.
 * Integration: backend partagé par édition, runtime, persistence et modules Seq.
 */
#include "Seq/seq_model.h"

#include <string.h>

#include "stm32h7xx_hal.h"
#include "Storage/memory_layout.h"
#include "Seq/seq_param_iface.h"

#define SEQ_LOCK_NONE 0xFFFFU

typedef struct
{
    uint16_t next;
    seq_value16_t value16;
    seq_plock_key_t key;
    uint8_t flags;
} seq_plock_node_t;

_Static_assert(sizeof(seq_plock_node_t) == 6U, "internal p-lock node size changed");

typedef struct
{
    seq_track_data_t tracks[SEQ_LANE_CAPACITY];
    seq_plock_node_t pool[SEQ_LANE_CAPACITY][SEQ_PLOCK_POOL_CAP_PER_TRACK];
    uint16_t free_head[SEQ_LANE_CAPACITY];
    uint16_t free_count[SEQ_LANE_CAPACITY];
} seq_runtime_project_data_t;

_Static_assert(sizeof(seq_runtime_project_data_t) == 127104U,
               "sequencer project storage size changed");

SEQ_STATE_D2 static seq_runtime_project_data_t g_seq_project;

static uint8_t seq_step_play_field_mask(seq_step_play_field_t field)
{
    return (field < SEQ_STEP_PLAY_FIELD_COUNT) ? (uint8_t)(1U << (uint8_t)field) : 0U;
}

static uint8_t seq_step_play_value_is_valid(seq_step_play_field_t field, int16_t value)
{
    switch (field)
    {
        case SEQ_STEP_PLAY_FIELD_NOTE:
        case SEQ_STEP_PLAY_FIELD_VELOCITY:
            return ((value >= 0) && (value <= 127)) ? 1U : 0U;

        case SEQ_STEP_PLAY_FIELD_LENGTH:
            return ((value >= 1) && (value <= 64)) ? 1U : 0U;

        case SEQ_STEP_PLAY_FIELD_MICROTIMING:
            return ((value >= -24) && (value <= 24)) ? 1U : 0U;

        default:
            return 0U;
    }
}

void seq_step_play_init(seq_step_play_t *play)
{
    seq_step_play_clear(play);
}

uint8_t seq_step_play_get(const seq_step_play_t *play,
                          uint8_t voice,
                          seq_step_play_field_t field,
                          int16_t *out_value)
{
    const uint8_t mask = seq_step_play_field_mask(field);
    if ((play == NULL) || (out_value == NULL) || (voice >= SEQ_STEP_PLAY_VOICE_COUNT)
            || (mask == 0U) || ((play->voices[voice].present_mask & mask) == 0U))
    {
        return 0U;
    }

    const seq_step_play_voice_t *const state = &play->voices[voice];
    switch (field)
    {
        case SEQ_STEP_PLAY_FIELD_NOTE:
            *out_value = (int16_t)state->note;
            break;
        case SEQ_STEP_PLAY_FIELD_VELOCITY:
            *out_value = (int16_t)state->velocity;
            break;
        case SEQ_STEP_PLAY_FIELD_LENGTH:
            *out_value = (int16_t)state->length;
            break;
        case SEQ_STEP_PLAY_FIELD_MICROTIMING:
            *out_value = (int16_t)state->microtiming;
            break;
        default:
            return 0U;
    }
    return 1U;
}

uint8_t seq_step_play_set(seq_step_play_t *play,
                          uint8_t voice,
                          seq_step_play_field_t field,
                          int16_t value)
{
    const uint8_t mask = seq_step_play_field_mask(field);
    if ((play == NULL) || (voice >= SEQ_STEP_PLAY_VOICE_COUNT) || (mask == 0U)
            || (seq_step_play_value_is_valid(field, value) == 0U))
    {
        return 0U;
    }

    seq_step_play_voice_t *const state = &play->voices[voice];
    switch (field)
    {
        case SEQ_STEP_PLAY_FIELD_NOTE:
            state->note = (uint8_t)value;
            break;
        case SEQ_STEP_PLAY_FIELD_VELOCITY:
            state->velocity = (uint8_t)value;
            break;
        case SEQ_STEP_PLAY_FIELD_LENGTH:
            state->length = (uint8_t)value;
            break;
        case SEQ_STEP_PLAY_FIELD_MICROTIMING:
            state->microtiming = (int8_t)value;
            break;
        default:
            return 0U;
    }
    state->present_mask = (uint8_t)(state->present_mask | mask);
    return 1U;
}

uint8_t seq_step_play_delete(seq_step_play_t *play,
                             uint8_t voice,
                             seq_step_play_field_t field)
{
    const uint8_t mask = seq_step_play_field_mask(field);
    if ((play == NULL) || (voice >= SEQ_STEP_PLAY_VOICE_COUNT) || (mask == 0U))
    {
        return 0U;
    }

    seq_step_play_voice_t *const state = &play->voices[voice];
    const uint8_t was_present = ((state->present_mask & mask) != 0U) ? 1U : 0U;
    state->present_mask = (uint8_t)(state->present_mask & (uint8_t)~mask);
    return was_present;
}

void seq_step_play_clear_voice(seq_step_play_t *play, uint8_t voice)
{
    if ((play == NULL) || (voice >= SEQ_STEP_PLAY_VOICE_COUNT))
    {
        return;
    }
    memset(&play->voices[voice], 0, sizeof(play->voices[voice]));
}

void seq_step_play_clear(seq_step_play_t *play)
{
    if (play != NULL)
    {
        memset(play, 0, sizeof(*play));
    }
}

uint8_t seq_step_play_voice_has_any(const seq_step_play_t *play, uint8_t voice)
{
    if ((play == NULL) || (voice >= SEQ_STEP_PLAY_VOICE_COUNT))
    {
        return 0U;
    }
    return ((play->voices[voice].present_mask & (uint8_t)SEQ_STEP_PLAY_PRESENT_ALL) != 0U) ? 1U : 0U;
}

uint8_t seq_step_play_has_any(const seq_step_play_t *play)
{
    if (play == NULL)
    {
        return 0U;
    }
    for (uint8_t voice = 0U; voice < SEQ_STEP_PLAY_VOICE_COUNT; ++voice)
    {
        if (seq_step_play_voice_has_any(play, voice) != 0U)
        {
            return 1U;
        }
    }
    return 0U;
}

static uint8_t seq_model_play_slot_decode(seq_param_slot_t param_slot,
                                          uint8_t *out_voice,
                                          seq_step_play_field_t *out_field)
{
    if ((out_voice == NULL) || (out_field == NULL) || (param_slot >= SEQ_PARAM_PLAY_SLOT_COUNT))
    {
        return 0U;
    }

    *out_voice = (uint8_t)(param_slot / (seq_param_slot_t)SEQ_STEP_PLAY_FIELD_COUNT);
    *out_field = (seq_step_play_field_t)(param_slot % (seq_param_slot_t)SEQ_STEP_PLAY_FIELD_COUNT);
    return (*out_voice < SEQ_STEP_PLAY_VOICE_COUNT) ? 1U : 0U;
}

static uint8_t seq_model_play_value_decode(seq_track_id_t track,
                                           seq_param_slot_t param_slot,
                                           seq_value16_t value16,
                                           int16_t *out_value)
{
    param_id_t param = PARAM_COUNT;
    if ((out_value == NULL)
            || (seq_param_iface_slot_to_param(track,
                                              (uint8_t)SEQ_PLOCK_SET_PLAY,
                                              param_slot,
                                              &param) == 0U))
    {
        return 0U;
    }

    const float decoded = seq_param_iface_decode_param_value(param, value16);
    *out_value = (int16_t)(decoded + ((decoded >= 0.0f) ? 0.5f : -0.5f));
    return 1U;
}

static uint8_t seq_model_play_entry_read(seq_track_id_t track,
                                         const seq_step_t *step,
                                         seq_param_slot_t param_slot,
                                         seq_plock_entry_t *out_entry)
{
    uint8_t voice = 0U;
    seq_step_play_field_t field = SEQ_STEP_PLAY_FIELD_NOTE;
    int16_t value = 0;
    param_id_t param = PARAM_COUNT;
    if ((step == NULL) || (out_entry == NULL)
            || (seq_model_play_slot_decode(param_slot, &voice, &field) == 0U)
            || (seq_step_play_get(&step->play, voice, field, &value) == 0U)
            || (seq_param_iface_slot_to_param(track,
                                              (uint8_t)SEQ_PLOCK_SET_PLAY,
                                              param_slot,
                                              &param) == 0U))
    {
        return 0U;
    }

    out_entry->next = SEQ_LOCK_NONE;
    out_entry->param_slot = param_slot;
    out_entry->set_id = (uint8_t)SEQ_PLOCK_SET_PLAY;
    out_entry->value16 = seq_param_iface_encode_param_value(param, (float)value);
    out_entry->flags = 0U;
    out_entry->reserved = 0U;
    return 1U;
}

static uint8_t seq_model_step_play_field_count(const seq_step_t *step)
{
    uint8_t count = 0U;
    if (step == NULL)
    {
        return 0U;
    }

    for (uint8_t voice = 0U; voice < SEQ_STEP_PLAY_VOICE_COUNT; ++voice)
    {
        uint8_t mask = (uint8_t)(step->play.voices[voice].present_mask
                                 & (uint8_t)SEQ_STEP_PLAY_PRESENT_ALL);
        while (mask != 0U)
        {
            count = (uint8_t)(count + (mask & 1U));
            mask >>= 1U;
        }
    }
    return count;
}

static uint32_t seq_model_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void seq_model_exit_critical(uint32_t primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}

static uint8_t seq_model_track_is_valid(seq_track_id_t track)
{
    seq_lane_descriptor_t descriptor;
    return (seq_lane_get_descriptor((seq_lane_id_t)track, &descriptor) != 0U)
            && (descriptor.active != 0U);
}

static uint8_t seq_model_track_is_play(seq_track_id_t track)
{
    return seq_model_track_is_valid(track);
}

static uint8_t seq_model_track_can_emit_notes(seq_track_id_t track)
{
    seq_lane_descriptor_t descriptor;
    return (seq_lane_get_descriptor((seq_lane_id_t)track, &descriptor) != 0U)
            && (descriptor.can_emit_notes != 0U);
}

static uint16_t seq_model_pool_capacity(seq_track_id_t track)
{
    return (seq_model_track_is_valid(track) != 0U) ? (uint16_t)SEQ_PLOCK_POOL_CAP_PER_TRACK : 0U;
}

static seq_plock_node_t *seq_model_pool_entry_mut(seq_track_id_t track, uint16_t index)
{
    if ((seq_model_track_is_valid(track) == 0U) || (index >= seq_model_pool_capacity(track)))
    {
        return NULL;
    }
    return &g_seq_project.pool[track][index];
}

static const seq_plock_node_t *seq_model_pool_entry_const(seq_track_id_t track, uint16_t index)
{
    return seq_model_pool_entry_mut(track, index);
}

static uint8_t seq_model_clamp_playback_length(uint8_t length_steps)
{
    if (length_steps == 0U)
    {
        return 1U;
    }
    if (length_steps > SEQ_MAX_STEPS)
    {
        return SEQ_MAX_STEPS;
    }
    return length_steps;
}

static uint8_t seq_model_page_count_for_length(uint8_t length_steps)
{
#if defined(BRICK6_VARIANT_LOWCOST)
    const uint8_t length = seq_model_clamp_playback_length(length_steps);
    uint8_t page_count = (uint8_t)((length + (SEQ_STEPS_PER_PAGE - 1U)) / SEQ_STEPS_PER_PAGE);
    if (page_count > SEQ_PAGE_COUNT)
    {
        page_count = SEQ_PAGE_COUNT;
    }
    return page_count;
#else
    (void)length_steps;
    return SEQ_PAGE_COUNT;
#endif
}

static uint8_t seq_model_clamp_ui_page_for_length(uint8_t page, uint8_t length_steps)
{
    const uint8_t page_count = seq_model_page_count_for_length(length_steps);
    const uint8_t max_page = (page_count > 0U) ? (uint8_t)(page_count - 1U) : 0U;
    return (page > max_page) ? max_page : page;
}

static uint8_t seq_model_step_is_valid(seq_step_id_t step)
{
    return (step < seq_model_get_editable_step_capacity()) ? 1U : 0U;
}

static uint8_t seq_model_normalize_roll(uint8_t roll)
{
    return (roll < (uint8_t)SEQ_STEP_ROLL_COUNT) ? roll : (uint8_t)SEQ_STEP_ROLL_OFF;
}

static seq_step_t *seq_model_get_step_mut(seq_track_id_t track, seq_step_id_t step)
{
    if ((seq_model_track_is_valid(track) == 0U) || (seq_model_step_is_valid(step) == 0U))
    {
        return 0;
    }

    return &g_seq_project.tracks[track].steps[step];
}

static const seq_step_t *seq_model_get_step_const(seq_track_id_t track, seq_step_id_t step)
{
    if ((seq_model_track_is_valid(track) == 0U) || (seq_model_step_is_valid(step) == 0U))
    {
        return 0;
    }

    return &g_seq_project.tracks[track].steps[step];
}

static uint16_t seq_model_alloc_lock_node(seq_track_id_t track)
{
    if ((seq_model_track_is_valid(track) == 0U) ||
        (g_seq_project.free_count[track] == 0U) ||
        (g_seq_project.free_head[track] == SEQ_LOCK_NONE))
    {
        return SEQ_LOCK_NONE;
    }

    const uint16_t idx = g_seq_project.free_head[track];
    seq_plock_node_t *const entry = seq_model_pool_entry_mut(track, idx);
    if (entry == NULL)
    {
        return SEQ_LOCK_NONE;
    }
    g_seq_project.free_head[track] = entry->next;
    entry->next = SEQ_LOCK_NONE;
    g_seq_project.free_count[track]--;
    return idx;
}

static void seq_model_free_lock_node(seq_track_id_t track, uint16_t idx)
{
    if ((seq_model_track_is_valid(track) == 0U) ||
        (idx >= seq_model_pool_capacity(track)))
    {
        return;
    }

    seq_model_pool_entry_mut(track, idx)->next = g_seq_project.free_head[track];
    g_seq_project.free_head[track] = idx;
    g_seq_project.free_count[track]++;
}

static uint16_t seq_model_find_lock_idx(seq_track_id_t track,
                                        const seq_step_t *step,
                                        seq_plock_key_t key,
                                        uint16_t *out_prev)
{
    if (out_prev != 0)
    {
        *out_prev = SEQ_LOCK_NONE;
    }

    uint16_t prev = SEQ_LOCK_NONE;
    uint16_t idx = step->lock_head;
    uint16_t guard = 0U;
    while (idx != SEQ_LOCK_NONE)
    {
        if (guard++ >= seq_model_pool_capacity(track))
        {
            break;
        }

        if (idx >= seq_model_pool_capacity(track))
        {
            break;
        }

        const seq_plock_node_t *entry = seq_model_pool_entry_const(track, idx);
        if (entry->key == key)
        {
            if (out_prev != 0)
            {
                *out_prev = prev;
            }
            return idx;
        }

        prev = idx;
        idx = entry->next;
    }

    return SEQ_LOCK_NONE;
}

static uint8_t seq_model_compute_step_mask(seq_track_id_t track, const seq_step_t *step)
{
    uint8_t mask = 0U;

    uint16_t idx = step->lock_head;
    uint16_t guard = 0U;
    while (idx != SEQ_LOCK_NONE)
    {
        if (guard++ >= seq_model_pool_capacity(track))
        {
            break;
        }

        if (idx >= seq_model_pool_capacity(track))
        {
            break;
        }

        const seq_plock_node_t *entry = seq_model_pool_entry_const(track, idx);
        uint8_t set_id = 0U;
        seq_param_slot_t param_slot = 0U;
        if (seq_param_iface_key_to_address(entry->key, &set_id, &param_slot) == 0U)
        {
            break;
        }
        (void)param_slot;
        mask |= seq_param_iface_set_to_mask(set_id);
        idx = entry->next;
    }

    return mask;
}

static void seq_model_step_scan_lock_sets(seq_track_id_t track,
                                          const seq_step_t *step,
                                          uint8_t *out_has_play_plock,
                                          uint8_t *out_has_non_play_plock)
{
    uint8_t has_play_plock = ((step != NULL) && (seq_step_play_has_any(&step->play) != 0U)) ? 1U : 0U;
    uint8_t has_non_play_plock = 0U;

    if ((seq_model_track_is_valid(track) == 0U) || (step == 0))
    {
        if (out_has_play_plock != 0)
        {
            *out_has_play_plock = 0U;
        }
        if (out_has_non_play_plock != 0)
        {
            *out_has_non_play_plock = 0U;
        }
        return;
    }

    uint16_t idx = step->lock_head;
    uint16_t guard = 0U;
    while (idx != SEQ_LOCK_NONE)
    {
        if (guard++ >= seq_model_pool_capacity(track))
        {
            break;
        }

        if (idx >= seq_model_pool_capacity(track))
        {
            break;
        }

        const seq_plock_node_t *entry = seq_model_pool_entry_const(track, idx);
        uint8_t set_id = 0U;
        seq_param_slot_t param_slot = 0U;
        if (seq_param_iface_key_to_address(entry->key, &set_id, &param_slot) == 0U)
        {
            break;
        }
        (void)param_slot;
        if (set_id == (uint8_t)SEQ_PLOCK_SET_PLAY)
        {
            has_play_plock = 1U;
        }
        else
        {
            has_non_play_plock = 1U;
        }

        idx = entry->next;
    }

    if (out_has_play_plock != 0)
    {
        *out_has_play_plock = has_play_plock;
    }
    if (out_has_non_play_plock != 0)
    {
        *out_has_non_play_plock = has_non_play_plock;
    }
}

void seq_model_init_defaults(void)
{
    memset(&g_seq_project, 0, sizeof(g_seq_project));

    for (uint8_t tr = 0U; tr < (uint8_t)SEQ_LANE_CAPACITY; ++tr)
    {
        g_seq_project.tracks[tr].length_steps = SEQ_DEFAULT_LENGTH_STEPS;
        g_seq_project.tracks[tr].ui_page = 0U;

        for (uint8_t st = 0U; st < SEQ_MAX_STEPS; ++st)
        {
            g_seq_project.tracks[tr].steps[st].lock_head = SEQ_LOCK_NONE;
        }

        g_seq_project.free_head[tr] = 0U;
        const uint16_t pool_capacity = seq_model_pool_capacity(tr);
        g_seq_project.free_count[tr] = pool_capacity;

        for (uint16_t i = 0U; i < pool_capacity; ++i)
        {
            seq_model_pool_entry_mut(tr, i)->next =
                (i + 1U < pool_capacity) ? (uint16_t)(i + 1U) : SEQ_LOCK_NONE;
        }
    }
}

uint8_t seq_model_get_trig(seq_track_id_t track, seq_step_id_t step)
{
    if ((seq_model_track_is_valid(track) == 0U) || (seq_model_step_is_valid(step) == 0U))
    {
        return 0U;
    }
    if (seq_model_track_is_play(track) == 0U)
    {
        return 0U;
    }

    return g_seq_project.tracks[track].steps[step].trig;
}

void seq_model_toggle_trig(seq_track_id_t track, seq_step_id_t step)
{
    if ((seq_model_track_is_valid(track) == 0U) || (seq_model_step_is_valid(step) == 0U))
    {
        return;
    }
    if (seq_model_track_is_play(track) == 0U)
    {
        return;
    }

    seq_step_t *const s = &g_seq_project.tracks[track].steps[step];
    const uint32_t primask = seq_model_enter_critical();
    s->trig = (s->trig == 0U) ? 1U : 0U;
    if (s->trig == 0U)
    {
        s->roll = (uint8_t)SEQ_STEP_ROLL_OFF;
    }
    seq_model_exit_critical(primask);
}

void seq_model_set_trig(seq_track_id_t track, seq_step_id_t step, uint8_t trig)
{
    if ((seq_model_track_is_valid(track) == 0U) || (seq_model_step_is_valid(step) == 0U))
    {
        return;
    }
    if (seq_model_track_is_play(track) == 0U)
    {
        return;
    }

    const uint32_t primask = seq_model_enter_critical();
    seq_step_t *const s = &g_seq_project.tracks[track].steps[step];
    s->trig = (trig != 0U) ? 1U : 0U;
    if (s->trig == 0U)
    {
        s->roll = (uint8_t)SEQ_STEP_ROLL_OFF;
    }
    seq_model_exit_critical(primask);
}

uint8_t seq_model_get_step_roll(seq_track_id_t track, seq_step_id_t step)
{
    if (seq_model_track_is_play(track) == 0U)
    {
        return (uint8_t)SEQ_STEP_ROLL_OFF;
    }
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if ((s == 0) || (s->trig == 0U))
    {
        return (uint8_t)SEQ_STEP_ROLL_OFF;
    }

    return seq_model_normalize_roll(s->roll);
}

void seq_model_set_step_roll(seq_track_id_t track, seq_step_id_t step, uint8_t roll)
{
    if (seq_model_track_is_play(track) == 0U)
    {
        return;
    }
    seq_step_t *const s = seq_model_get_step_mut(track, step);
    if ((s == 0) || (s->trig == 0U))
    {
        return;
    }

    const uint32_t primask = seq_model_enter_critical();
    s->roll = seq_model_normalize_roll(roll);
    seq_model_exit_critical(primask);
}

uint16_t seq_model_step_roll_divisor(uint8_t roll)
{
    static const uint16_t k_divisors[SEQ_STEP_ROLL_COUNT] = {
        0U, 20U, 24U, 32U, 40U, 48U, 64U, 80U
    };

    roll = seq_model_normalize_roll(roll);
    return k_divisors[roll];
}

const char *seq_model_step_roll_label(uint8_t roll)
{
    static const char *const k_labels[SEQ_STEP_ROLL_COUNT] = {
        "OFF", "1/20", "1/24", "1/32", "1/40", "1/48", "1/64", "1/80"
    };

    roll = seq_model_normalize_roll(roll);
    return k_labels[roll];
}

uint8_t seq_model_step_roll_is_emphasized(uint8_t roll)
{
    roll = seq_model_normalize_roll(roll);
    return (uint8_t)(((roll == (uint8_t)SEQ_STEP_ROLL_1_32)
                      || (roll == (uint8_t)SEQ_STEP_ROLL_1_24)
                      || (roll == (uint8_t)SEQ_STEP_ROLL_1_64)) ? 1U : 0U);
}

uint8_t seq_model_get_track_page(seq_track_id_t track)
{
    if (seq_model_track_is_valid(track) == 0U)
    {
        return 0U;
    }

    return g_seq_project.tracks[track].ui_page;
}

void seq_model_set_track_page(seq_track_id_t track, uint8_t page)
{
    if (seq_model_track_is_valid(track) == 0U)
    {
        return;
    }

    g_seq_project.tracks[track].ui_page =
        seq_model_clamp_ui_page_for_length(page, g_seq_project.tracks[track].length_steps);
}

void seq_model_set_track_length(seq_track_id_t track, uint8_t length_steps)
{
    if (seq_model_track_is_valid(track) == 0U)
    {
        return;
    }

    g_seq_project.tracks[track].length_steps = seq_model_clamp_playback_length(length_steps);
    g_seq_project.tracks[track].ui_page =
        seq_model_clamp_ui_page_for_length(g_seq_project.tracks[track].ui_page,
                                           g_seq_project.tracks[track].length_steps);
}

uint8_t seq_model_get_track_length(seq_track_id_t track)
{
    return seq_model_get_track_playback_length(track);
}

uint8_t seq_model_get_editable_step_capacity(void)
{
    return SEQ_MAX_STEPS;
}

uint8_t seq_model_is_step_editable_index(seq_step_id_t step)
{
    return (step < seq_model_get_editable_step_capacity()) ? 1U : 0U;
}

uint8_t seq_model_get_track_playback_length(seq_track_id_t track)
{
    if (seq_model_track_is_valid(track) == 0U)
    {
        return SEQ_MAX_STEPS;
    }

    return seq_model_clamp_playback_length(g_seq_project.tracks[track].length_steps);
}

uint8_t seq_model_is_step_in_track_playback_window(seq_track_id_t track, seq_step_id_t step)
{
    if (seq_model_track_is_valid(track) == 0U)
    {
        return 0U;
    }

    return (step < seq_model_get_track_playback_length(track)) ? 1U : 0U;
}

uint8_t seq_model_step_is_active(seq_track_id_t track, seq_step_id_t step)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if (s == 0)
    {
        return 0U;
    }

    return (s->trig != 0U) ? 1U : 0U;
}

seq_step_content_t seq_model_get_step_content(seq_track_id_t track, seq_step_id_t step)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if (s == 0)
    {
        return SEQ_STEP_CONTENT_EMPTY;
    }
    uint8_t has_play_plock = 0U;
    uint8_t has_non_play_plock = 0U;
    seq_model_step_scan_lock_sets(track, s, &has_play_plock, &has_non_play_plock);

    if ((has_play_plock == 0U) && (has_non_play_plock == 0U))
    {
        return SEQ_STEP_CONTENT_EMPTY;
    }
    if ((has_play_plock != 0U) && (has_non_play_plock != 0U))
    {
        return SEQ_STEP_CONTENT_PLAY_AND_NON_PLAY;
    }
    if (has_play_plock != 0U)
    {
        return SEQ_STEP_CONTENT_PLAY_ONLY;
    }

    return SEQ_STEP_CONTENT_NON_PLAY_ONLY;
}

seq_step_visual_t seq_model_get_step_visual(seq_track_id_t track, seq_step_id_t step)
{
    if (seq_model_step_is_active(track, step) == 0U)
    {
        return SEQ_STEP_VISUAL_OFF;
    }
    if (seq_model_track_can_emit_notes(track) == 0U)
    {
        return SEQ_STEP_VISUAL_BLUE;
    }

    const seq_step_content_t content = seq_model_get_step_content(track, step);
    if ((content == SEQ_STEP_CONTENT_PLAY_ONLY) || (content == SEQ_STEP_CONTENT_PLAY_AND_NON_PLAY))
    {
        return SEQ_STEP_VISUAL_GREEN;
    }
    if (content == SEQ_STEP_CONTENT_NON_PLAY_ONLY)
    {
        return SEQ_STEP_VISUAL_BLUE;
    }

    return SEQ_STEP_VISUAL_OFF;
}

seq_step_state_t seq_model_get_step_state(seq_track_id_t track, seq_step_id_t step)
{
    if (seq_model_step_is_active(track, step) == 0U)
    {
        return SEQ_STEP_STATE_EMPTY;
    }
    if (seq_model_track_can_emit_notes(track) == 0U)
    {
        return SEQ_STEP_STATE_PARAM_LOCK_ONLY;
    }

    const seq_step_content_t content = seq_model_get_step_content(track, step);
    if (content == SEQ_STEP_CONTENT_NON_PLAY_ONLY)
    {
        return SEQ_STEP_STATE_PARAM_LOCK_ONLY;
    }
    if (content == SEQ_STEP_CONTENT_PLAY_AND_NON_PLAY)
    {
        return SEQ_STEP_STATE_NOTE_WITH_PLOCKS;
    }

    return SEQ_STEP_STATE_NOTE;
}

uint8_t seq_model_step_has_play_plock(seq_track_id_t track, seq_step_id_t step)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if (s == 0)
    {
        return 0U;
    }

    uint8_t has_play_plock = 0U;
    seq_model_step_scan_lock_sets(track, s, &has_play_plock, 0);
    return has_play_plock;
}

uint8_t seq_model_step_has_non_play_plock(seq_track_id_t track, seq_step_id_t step)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if (s == 0)
    {
        return 0U;
    }

    uint8_t has_non_play_plock = 0U;
    seq_model_step_scan_lock_sets(track, s, 0, &has_non_play_plock);
    return has_non_play_plock;
}

uint8_t seq_model_step_is_empty(seq_track_id_t track, seq_step_id_t step)
{
    return (seq_model_get_step_content(track, step) == SEQ_STEP_CONTENT_EMPTY) ? 1U : 0U;
}

uint8_t seq_model_step_is_quick_note_eligible(seq_track_id_t track, seq_step_id_t step)
{
    if (seq_model_track_can_emit_notes(track) == 0U)
    {
        return 0U;
    }
    return (uint8_t)((seq_model_step_is_active(track, step) == 0U)
                     && (seq_model_step_is_empty(track, step) != 0U));
}

uint8_t seq_model_step_play_get(seq_track_id_t track,
                                seq_step_id_t step,
                                uint8_t voice,
                                seq_step_play_field_t field,
                                int16_t *out_value)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    return (s != NULL) ? seq_step_play_get(&s->play, voice, field, out_value) : 0U;
}

uint8_t seq_model_step_play_set(seq_track_id_t track,
                                seq_step_id_t step,
                                uint8_t voice,
                                seq_step_play_field_t field,
                                int16_t value)
{
    seq_step_t *const s = seq_model_get_step_mut(track, step);
    return (s != NULL) ? seq_step_play_set(&s->play, voice, field, value) : 0U;
}

uint8_t seq_model_step_play_delete(seq_track_id_t track,
                                   seq_step_id_t step,
                                   uint8_t voice,
                                   seq_step_play_field_t field)
{
    seq_step_t *const s = seq_model_get_step_mut(track, step);
    return (s != NULL) ? seq_step_play_delete(&s->play, voice, field) : 0U;
}

void seq_model_step_play_clear_voice(seq_track_id_t track,
                                     seq_step_id_t step,
                                     uint8_t voice)
{
    seq_step_t *const s = seq_model_get_step_mut(track, step);
    if (s != NULL)
    {
        seq_step_play_clear_voice(&s->play, voice);
    }
}

void seq_model_step_play_clear(seq_track_id_t track, seq_step_id_t step)
{
    seq_step_t *const s = seq_model_get_step_mut(track, step);
    if (s != NULL)
    {
        seq_step_play_clear(&s->play);
    }
}

uint8_t seq_model_step_play_voice_has_any(seq_track_id_t track,
                                          seq_step_id_t step,
                                          uint8_t voice)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    return (s != NULL) ? seq_step_play_voice_has_any(&s->play, voice) : 0U;
}

uint8_t seq_model_step_play_has_any(seq_track_id_t track, seq_step_id_t step)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    return (s != NULL) ? seq_step_play_has_any(&s->play) : 0U;
}

uint8_t seq_model_get_step_lock_limit(seq_track_id_t track)
{
    if (seq_model_track_is_valid(track) == 0U)
    {
        return 0U;
    }
    return (uint8_t)SEQ_STEP_MAX_LOCKS;
}

uint16_t seq_model_get_track_plock_capacity(seq_track_id_t track)
{
    return (seq_model_track_is_valid(track) != 0U) ? seq_model_pool_capacity(track) : 0U;
}

uint16_t seq_model_get_track_plock_count(seq_track_id_t track)
{
    if (seq_model_track_is_valid(track) == 0U)
    {
        return 0U;
    }

    const uint32_t primask = seq_model_enter_critical();
    const uint16_t capacity = seq_model_pool_capacity(track);
    const uint16_t free_count = g_seq_project.free_count[track];
    seq_model_exit_critical(primask);
    return (free_count <= capacity) ? (uint16_t)(capacity - free_count) : 0U;
}

uint8_t seq_model_step_plock_find(seq_track_id_t track,
                                  seq_step_id_t step,
                                  uint8_t set_id,
                                  seq_param_slot_t param_slot,
                                  seq_plock_entry_t *out_entry)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if ((s == 0) || (out_entry == 0))
    {
        return 0U;
    }

    if (set_id == (uint8_t)SEQ_PLOCK_SET_PLAY)
    {
        return seq_model_play_entry_read(track, s, param_slot, out_entry);
    }

    seq_plock_key_t key = 0U;
    if (seq_param_iface_address_to_key(set_id, param_slot, &key) == 0U)
    {
        return 0U;
    }

    const uint16_t idx = seq_model_find_lock_idx(track, s, key, 0);
    if (idx == SEQ_LOCK_NONE)
    {
        return 0U;
    }

    const seq_plock_node_t *const node = seq_model_pool_entry_const(track, idx);
    out_entry->next = node->next;
    out_entry->param_slot = param_slot;
    out_entry->set_id = set_id;
    out_entry->value16 = node->value16;
    out_entry->flags = node->flags;
    out_entry->reserved = 0U;
    return 1U;
}

seq_plock_op_status_t seq_model_step_plock_upsert(seq_track_id_t track,
                                                   seq_step_id_t step,
                                                   uint8_t set_id,
                                                   seq_param_slot_t param_slot,
                                                   seq_value16_t value16,
                                                   uint8_t flags)
{
    seq_step_t *const s = seq_model_get_step_mut(track, step);
    if (s == 0)
    {
        return SEQ_PLOCK_OP_INVALID;
    }

    if (seq_param_iface_is_set_plockable(set_id) == 0U)
    {
        return SEQ_PLOCK_OP_SET_NOT_PLOCKABLE;
    }
    if (seq_param_iface_slot_is_storable(track, set_id, param_slot) == 0U)
    {
        return SEQ_PLOCK_OP_INVALID;
    }

    if (set_id == (uint8_t)SEQ_PLOCK_SET_PLAY)
    {
        uint8_t voice = 0U;
        seq_step_play_field_t field = SEQ_STEP_PLAY_FIELD_NOTE;
        int16_t value = 0;
        if ((seq_model_play_slot_decode(param_slot, &voice, &field) == 0U)
                || (seq_model_play_value_decode(track, param_slot, value16, &value) == 0U))
        {
            return SEQ_PLOCK_OP_INVALID;
        }

        const uint32_t primask = seq_model_enter_critical();
        int16_t previous_value = 0;
        const uint8_t existed = seq_step_play_get(&s->play, voice, field, &previous_value);
        if (seq_step_play_set(&s->play, voice, field, value) == 0U)
        {
            seq_model_exit_critical(primask);
            return SEQ_PLOCK_OP_INVALID;
        }
        seq_model_exit_critical(primask);
        (void)flags;
        return (existed != 0U) ? SEQ_PLOCK_OP_UPDATED : SEQ_PLOCK_OP_CREATED;
    }

    seq_plock_key_t key = 0U;
    if (seq_param_iface_address_to_key(set_id, param_slot, &key) == 0U)
    {
        return SEQ_PLOCK_OP_INVALID;
    }

    const uint32_t primask = seq_model_enter_critical();

    const uint16_t existing_idx = seq_model_find_lock_idx(track, s, key, 0);
    if (existing_idx != SEQ_LOCK_NONE)
    {
        seq_plock_node_t *const existing = seq_model_pool_entry_mut(track, existing_idx);
        existing->value16 = value16;
        existing->flags = flags;
        seq_model_exit_critical(primask);
        return SEQ_PLOCK_OP_UPDATED;
    }

    if (s->lock_count >= seq_model_get_step_lock_limit(track))
    {
        seq_model_exit_critical(primask);
        return SEQ_PLOCK_OP_STEP_FULL;
    }

    const uint16_t new_idx = seq_model_alloc_lock_node(track);
    if (new_idx == SEQ_LOCK_NONE)
    {
        seq_model_exit_critical(primask);
        return SEQ_PLOCK_OP_POOL_EMPTY;
    }

    seq_plock_node_t *const entry = seq_model_pool_entry_mut(track, new_idx);
    entry->key = key;
    entry->value16 = value16;
    entry->flags = flags;
    entry->next = s->lock_head;

    s->lock_head = new_idx;
    s->lock_count++;
    s->lock_set_mask |= seq_param_iface_set_to_mask(set_id);

    seq_model_exit_critical(primask);
    return SEQ_PLOCK_OP_CREATED;
}

seq_plock_op_status_t seq_model_step_plock_delete(seq_track_id_t track,
                                                   seq_step_id_t step,
                                                   uint8_t set_id,
                                                   seq_param_slot_t param_slot)
{
    seq_step_t *const s = seq_model_get_step_mut(track, step);
    if (s == 0)
    {
        return SEQ_PLOCK_OP_INVALID;
    }

    if (set_id == (uint8_t)SEQ_PLOCK_SET_PLAY)
    {
        uint8_t voice = 0U;
        seq_step_play_field_t field = SEQ_STEP_PLAY_FIELD_NOTE;
        if (seq_model_play_slot_decode(param_slot, &voice, &field) == 0U)
        {
            return SEQ_PLOCK_OP_INVALID;
        }
        const uint32_t primask = seq_model_enter_critical();
        const uint8_t deleted = seq_step_play_delete(&s->play, voice, field);
        seq_model_exit_critical(primask);
        return (deleted != 0U) ? SEQ_PLOCK_OP_DELETED : SEQ_PLOCK_OP_NOT_FOUND;
    }

    seq_plock_key_t key = 0U;
    if (seq_param_iface_address_to_key(set_id, param_slot, &key) == 0U)
    {
        return SEQ_PLOCK_OP_INVALID;
    }

    const uint32_t primask = seq_model_enter_critical();
    uint16_t prev = SEQ_LOCK_NONE;
    const uint16_t idx = seq_model_find_lock_idx(track, s, key, &prev);
    if (idx == SEQ_LOCK_NONE)
    {
        seq_model_exit_critical(primask);
        return SEQ_PLOCK_OP_NOT_FOUND;
    }

    const uint16_t next = seq_model_pool_entry_const(track, idx)->next;
    if (prev == SEQ_LOCK_NONE)
    {
        s->lock_head = next;
    }
    else
    {
        seq_model_pool_entry_mut(track, prev)->next = next;
    }

    if (s->lock_count > 0U)
    {
        s->lock_count--;
    }

    s->lock_set_mask = seq_model_compute_step_mask(track, s);
    seq_model_free_lock_node(track, idx);

    seq_model_exit_critical(primask);
    return SEQ_PLOCK_OP_DELETED;
}

void seq_model_step_plock_clear(seq_track_id_t track, seq_step_id_t step)
{
    seq_model_step_param_plock_clear(track, step);
    seq_model_step_play_clear(track, step);
}

void seq_model_step_param_plock_clear(seq_track_id_t track, seq_step_id_t step)
{
    seq_step_t *const s = seq_model_get_step_mut(track, step);
    if (s == 0)
    {
        return;
    }

    const uint32_t primask = seq_model_enter_critical();
    uint16_t idx = s->lock_head;
    while (idx != SEQ_LOCK_NONE)
    {
        if (idx >= seq_model_pool_capacity(track))
        {
            break;
        }

        const uint16_t next = seq_model_pool_entry_const(track, idx)->next;
        seq_model_free_lock_node(track, idx);
        idx = next;
    }

    s->lock_head = SEQ_LOCK_NONE;
    s->lock_count = 0U;
    s->lock_set_mask = 0U;
    seq_model_exit_critical(primask);
}

uint8_t seq_model_step_param_plock_count(seq_track_id_t track, seq_step_id_t step)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    return (s != NULL) ? s->lock_count : 0U;
}

uint8_t seq_model_step_plock_count(seq_track_id_t track, seq_step_id_t step)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if (s == 0)
    {
        return 0U;
    }

    return (uint8_t)(s->lock_count + seq_model_step_play_field_count(s));
}

uint8_t seq_model_step_plock_collect(seq_track_id_t track,
                                     seq_step_id_t step,
                                     seq_plock_entry_t *out_entries,
                                     uint8_t max_entries,
                                     uint8_t *out_count)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if ((s == 0) || (out_entries == 0) || (out_count == 0))
    {
        return 0U;
    }

    *out_count = 0U;
    if (max_entries == 0U)
    {
        return 1U;
    }

    uint8_t count = 0U;
    uint16_t idx = s->lock_head;
    uint16_t guard = 0U;
    while ((idx != SEQ_LOCK_NONE) && (count < s->lock_count) && (count < max_entries))
    {
        if (guard++ >= seq_model_pool_capacity(track))
        {
            return 0U;
        }

        if (idx >= seq_model_pool_capacity(track))
        {
            return 0U;
        }

        const seq_plock_node_t *const node = seq_model_pool_entry_const(track, idx);
        seq_plock_entry_t *const entry = &out_entries[count];
        if (seq_param_iface_key_to_address(node->key, &entry->set_id, &entry->param_slot) == 0U)
        {
            return 0U;
        }
        entry->next = node->next;
        entry->value16 = node->value16;
        entry->flags = node->flags;
        entry->reserved = 0U;
        count++;
        idx = node->next;
    }


    for (seq_param_slot_t slot = 0U;
         (slot < SEQ_PARAM_PLAY_SLOT_COUNT) && (count < max_entries);
         ++slot)
    {
        if (seq_model_play_entry_read(track, s, slot, &out_entries[count]) != 0U)
        {
            count++;
        }
    }

    *out_count = count;
    return 1U;
}

uint8_t seq_model_step_plock_get_at(seq_track_id_t track,
                                    seq_step_id_t step,
                                    uint8_t ordinal,
                                    seq_plock_entry_t *out_entry)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if ((s == 0) || (out_entry == 0)
            || (ordinal >= (uint8_t)(s->lock_count + seq_model_step_play_field_count(s))))
    {
        return 0U;
    }

    uint8_t index = 0U;
    uint16_t idx = s->lock_head;
    uint16_t guard = 0U;
    while (idx != SEQ_LOCK_NONE)
    {
        if (guard++ >= seq_model_pool_capacity(track))
        {
            return 0U;
        }

        if (idx >= seq_model_pool_capacity(track))
        {
            return 0U;
        }

        if (index == ordinal)
        {
            const seq_plock_node_t *const node = seq_model_pool_entry_const(track, idx);
            if (seq_param_iface_key_to_address(node->key,
                                               &out_entry->set_id,
                                               &out_entry->param_slot) == 0U)
            {
                return 0U;
            }
            out_entry->next = node->next;
            out_entry->value16 = node->value16;
            out_entry->flags = node->flags;
            out_entry->reserved = 0U;
            return 1U;
        }

        index++;
        idx = seq_model_pool_entry_const(track, idx)->next;
    }


    for (seq_param_slot_t slot = 0U; slot < SEQ_PARAM_PLAY_SLOT_COUNT; ++slot)
    {
        if (seq_model_play_entry_read(track, s, slot, out_entry) != 0U)
        {
            if (index == ordinal)
            {
                return 1U;
            }
            index++;
        }
    }

    return 0U;
}

uint8_t seq_model_step_param_plock_get_at(seq_track_id_t track,
                                          seq_step_id_t step,
                                          uint8_t ordinal,
                                          seq_plock_entry_t *out_entry)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if ((s == NULL) || (out_entry == NULL) || (ordinal >= s->lock_count))
    {
        return 0U;
    }

    uint8_t index = 0U;
    uint16_t idx = s->lock_head;
    uint16_t guard = 0U;
    while (idx != SEQ_LOCK_NONE)
    {
        if ((guard++ >= seq_model_pool_capacity(track)) || (idx >= seq_model_pool_capacity(track)))
        {
            return 0U;
        }
        if (index == ordinal)
        {
            const seq_plock_node_t *const node = seq_model_pool_entry_const(track, idx);
            if (seq_param_iface_key_to_address(node->key,
                                               &out_entry->set_id,
                                               &out_entry->param_slot) == 0U)
            {
                return 0U;
            }
            out_entry->next = node->next;
            out_entry->value16 = node->value16;
            out_entry->flags = node->flags;
            out_entry->reserved = 0U;
            return 1U;
        }
        index++;
        idx = seq_model_pool_entry_const(track, idx)->next;
    }
    return 0U;
}
