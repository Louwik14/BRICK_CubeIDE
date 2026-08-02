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
    seq_track_data_t tracks[SEQ_TRACK_COUNT];
    seq_plock_entry_t play_pool[TRACK_TOPOLOGY_PLAY_TRACK_COUNT][SEQ_PLAY_PLOCK_POOL_CAP_PER_TRACK];
    seq_plock_entry_t special_pool[TRACK_TOPOLOGY_SPECIAL_TRACK_COUNT][SEQ_SPECIAL_PLOCK_POOL_CAP_PER_TRACK];
    uint16_t free_head[SEQ_TRACK_COUNT];
    uint16_t free_count[SEQ_TRACK_COUNT];
} seq_runtime_project_data_t;

SEQ_STATE_D2 static seq_runtime_project_data_t g_seq_project;

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
    return track_topology_is_active(track);
}

static uint8_t seq_model_track_is_play(seq_track_id_t track)
{
    return track_topology_is_play(track);
}

static uint16_t seq_model_pool_capacity(seq_track_id_t track)
{
    return (seq_model_track_is_play(track) != 0U)
        ? (uint16_t)SEQ_PLAY_PLOCK_POOL_CAP_PER_TRACK
        : (uint16_t)SEQ_SPECIAL_PLOCK_POOL_CAP_PER_TRACK;
}

static seq_plock_entry_t *seq_model_pool_entry_mut(seq_track_id_t track, uint16_t index)
{
    if ((seq_model_track_is_valid(track) == 0U) || (index >= seq_model_pool_capacity(track)))
    {
        return NULL;
    }
    if (seq_model_track_is_play(track) != 0U)
    {
        return &g_seq_project.play_pool[track][index];
    }
    return &g_seq_project.special_pool[track - TRACK_TOPOLOGY_PLAY_TRACK_COUNT][index];
}

static const seq_plock_entry_t *seq_model_pool_entry_const(seq_track_id_t track, uint16_t index)
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
    seq_plock_entry_t *const entry = seq_model_pool_entry_mut(track, idx);
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
                                        uint8_t set_id,
                                        seq_param_slot_t param_slot,
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

        const seq_plock_entry_t *entry = seq_model_pool_entry_const(track, idx);
        if ((entry->set_id == set_id) && (entry->param_slot == param_slot))
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

        const seq_plock_entry_t *entry = seq_model_pool_entry_const(track, idx);
        mask |= seq_param_iface_set_to_mask(entry->set_id);
        idx = entry->next;
    }

    return mask;
}

static void seq_model_step_scan_lock_sets(seq_track_id_t track,
                                          const seq_step_t *step,
                                          uint8_t *out_has_play_plock,
                                          uint8_t *out_has_non_play_plock)
{
    uint8_t has_play_plock = 0U;
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

        const seq_plock_entry_t *entry = seq_model_pool_entry_const(track, idx);
        if (entry->set_id == (uint8_t)SEQ_PLOCK_SET_PLAY)
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

    for (uint8_t tr = 0U; tr < track_topology_get_logical_track_count(); ++tr)
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

    if (seq_model_track_is_play(track) == 0U)
    {
        return ((s->action != (uint8_t)SEQ_SPECIAL_ACTION_NONE) || (s->lock_count != 0U)) ? 1U : 0U;
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
    if (seq_model_track_is_play(track) == 0U)
    {
        return ((s->action != (uint8_t)SEQ_SPECIAL_ACTION_NONE) || (s->lock_count != 0U))
            ? SEQ_STEP_CONTENT_NON_PLAY_ONLY : SEQ_STEP_CONTENT_EMPTY;
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
    if (seq_model_track_is_play(track) == 0U)
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
    if (seq_model_track_is_play(track) == 0U)
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
    if (seq_model_track_is_play(track) == 0U)
    {
        return 0U;
    }
    return (uint8_t)((seq_model_step_is_active(track, step) == 0U)
                     && (seq_model_step_is_empty(track, step) != 0U));
}

uint8_t seq_model_get_step_lock_limit(seq_track_id_t track)
{
    if (seq_model_track_is_valid(track) == 0U)
    {
        return 0U;
    }
    return (seq_model_track_is_play(track) != 0U)
        ? (uint8_t)SEQ_PLAY_STEP_MAX_LOCKS
        : (uint8_t)SEQ_SPECIAL_STEP_MAX_LOCKS;
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

uint8_t seq_model_get_special_action(seq_track_id_t track, seq_step_id_t step)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if ((s == NULL) || (seq_model_track_is_play(track) != 0U))
    {
        return (uint8_t)SEQ_SPECIAL_ACTION_NONE;
    }
    return (s->action < (uint8_t)SEQ_SPECIAL_ACTION_COUNT)
        ? s->action : (uint8_t)SEQ_SPECIAL_ACTION_NONE;
}

void seq_model_set_special_action(seq_track_id_t track, seq_step_id_t step, uint8_t action)
{
    seq_step_t *const s = seq_model_get_step_mut(track, step);
    if ((s == NULL) || (seq_model_track_is_play(track) != 0U))
    {
        return;
    }
    const uint32_t primask = seq_model_enter_critical();
    s->action = (action < (uint8_t)SEQ_SPECIAL_ACTION_COUNT)
        ? action : (uint8_t)SEQ_SPECIAL_ACTION_NONE;
    s->special_reserved[0] = 0U;
    s->special_reserved[1] = 0U;
    s->special_reserved[2] = 0U;
    seq_model_exit_critical(primask);
}

void seq_model_toggle_special_action(seq_track_id_t track, seq_step_id_t step)
{
    const uint8_t action = seq_model_get_special_action(track, step);
    seq_model_set_special_action(track,
                                 step,
                                 (action == (uint8_t)SEQ_SPECIAL_ACTION_NONE)
                                     ? (uint8_t)SEQ_SPECIAL_ACTION_TRIGGER
                                     : (uint8_t)SEQ_SPECIAL_ACTION_NONE);
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

    const uint16_t idx = seq_model_find_lock_idx(track, s, set_id, param_slot, 0);
    if (idx == SEQ_LOCK_NONE)
    {
        return 0U;
    }

    *out_entry = *seq_model_pool_entry_const(track, idx);
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
    if ((seq_model_track_is_play(track) == 0U) && (set_id == (uint8_t)SEQ_PLOCK_SET_PLAY))
    {
        return SEQ_PLOCK_OP_SET_NOT_PLOCKABLE;
    }
    if (seq_param_iface_slot_is_supported(track, set_id, param_slot) == 0U)
    {
        return SEQ_PLOCK_OP_INVALID;
    }

    const uint32_t primask = seq_model_enter_critical();

    const uint16_t existing_idx = seq_model_find_lock_idx(track, s, set_id, param_slot, 0);
    if (existing_idx != SEQ_LOCK_NONE)
    {
        seq_plock_entry_t *const existing = seq_model_pool_entry_mut(track, existing_idx);
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

    seq_plock_entry_t *const entry = seq_model_pool_entry_mut(track, new_idx);
    entry->set_id = set_id;
    entry->param_slot = param_slot;
    entry->value16 = value16;
    entry->flags = flags;
    entry->reserved = 0U;
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

    const uint32_t primask = seq_model_enter_critical();
    uint16_t prev = SEQ_LOCK_NONE;
    const uint16_t idx = seq_model_find_lock_idx(track, s, set_id, param_slot, &prev);
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

uint8_t seq_model_step_plock_count(seq_track_id_t track, seq_step_id_t step)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if (s == 0)
    {
        return 0U;
    }

    return s->lock_count;
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

        out_entries[count++] = *seq_model_pool_entry_const(track, idx);
        idx = seq_model_pool_entry_const(track, idx)->next;
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
    if ((s == 0) || (out_entry == 0) || (ordinal >= s->lock_count))
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
            *out_entry = *seq_model_pool_entry_const(track, idx);
            return 1U;
        }

        index++;
        idx = seq_model_pool_entry_const(track, idx)->next;
    }

    return 0U;
}
