#include "Seq/seq_model.h"

#include <string.h>

#include "Storage/memory_layout.h"

#define SEQ_LOCK_NONE 0xFFFFU

SEQ_STATE_D2 static seq_project_data_t g_seq_project;

static uint8_t seq_model_track_is_valid(seq_track_id_t track)
{
    return (track < SEQ_TRACK_COUNT) ? 1U : 0U;
}

static uint8_t seq_model_step_is_valid(seq_step_id_t step)
{
    return (step < SEQ_MAX_STEPS) ? 1U : 0U;
}

void seq_model_init_defaults(void)
{
    memset(&g_seq_project, 0, sizeof(g_seq_project));

    for (uint8_t tr = 0U; tr < SEQ_TRACK_COUNT; ++tr)
    {
        g_seq_project.tracks[tr].length_steps = SEQ_MAX_STEPS;
        g_seq_project.tracks[tr].ui_page = 0U;

        for (uint8_t st = 0U; st < SEQ_MAX_STEPS; ++st)
        {
            g_seq_project.tracks[tr].steps[st].lock_head = SEQ_LOCK_NONE;
        }
    }

    g_seq_project.free_head = 0U;
    g_seq_project.free_count = SEQ_PLOCK_POOL_CAP;

    for (uint16_t i = 0U; i < (uint16_t)SEQ_PLOCK_POOL_CAP; ++i)
    {
        g_seq_project.pool[i].next = (i + 1U < (uint16_t)SEQ_PLOCK_POOL_CAP) ? (uint16_t)(i + 1U) : SEQ_LOCK_NONE;
    }
}

const seq_project_data_t *seq_model_get_project(void)
{
    return &g_seq_project;
}

uint8_t seq_model_get_trig(seq_track_id_t track, seq_step_id_t step)
{
    if ((seq_model_track_is_valid(track) == 0U) || (seq_model_step_is_valid(step) == 0U))
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

    seq_step_t *const s = &g_seq_project.tracks[track].steps[step];
    s->trig = (s->trig == 0U) ? 1U : 0U;
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

    if (page >= SEQ_PAGE_COUNT)
    {
        page = (uint8_t)(SEQ_PAGE_COUNT - 1U);
    }

    g_seq_project.tracks[track].ui_page = page;
}
