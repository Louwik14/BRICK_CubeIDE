#include "Seq/seq_runtime.h"

#include <string.h>

#include "Seq/seq_model.h"
#include "Seq/seq_edit.h"

static seq_runtime_state_t g_seq_runtime;

void seq_runtime_init(void)
{
    seq_model_init_defaults();
    memset(&g_seq_runtime, 0, sizeof(g_seq_runtime));
    g_seq_runtime.clock_src = SEQ_CLOCK_SRC_INTERNAL;
    seq_edit_init();
}

void seq_runtime_process(void)
{
    /* Step 0 scaffold only: runtime hook intentionally no-op. */
}

const seq_runtime_state_t *seq_runtime_get_state(void)
{
    return &g_seq_runtime;
}
