#include "Seq/seq_model.h"

#include <string.h>

#define SEQ_LOCK_NONE 0xFFFFU

static seq_project_data_t g_seq_project;

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
