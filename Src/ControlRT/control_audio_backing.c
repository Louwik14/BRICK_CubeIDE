#include "ControlRT/control_audio_fifo_layout.h"
#include "ControlRT/audio_state_transaction.h"
#include "ControlRT/prepared_audio_state.h"
#include "ControlRT/pattern_recall_diag.h"
#include "ControlRT/patch_preview_contract.h"
#include "Platform/memory_layout.h"
#include <string.h>

CTRL_STATE control_audio_fifo_layout_t g_control_audio_fifo_layout;
CONTROL_STATE_SDRAM control_audio_command_t
    g_control_audio_fifo_commands[CONTROL_AUDIO_FIFO_CAPACITY];
CONTROL_STATE_SDRAM audio_state_transaction_t g_audio_state_transaction;
CONTROL_STATE_SDRAM prepared_audio_slot_t
    g_prepared_audio_slots[PREPARED_AUDIO_SLOT_COUNT];
CONTROL_STATE_SDRAM patch_preview_publication_t
    g_patch_preview_publication[PATCH_PREVIEW_PUBLICATION_SLOT_COUNT];
CONTROL_STATE_SDRAM volatile uint32_t g_patch_preview_audio_consumed_generation;

#if BRICK_PATTERN_RECALL_DIAG
IRQ_SHARED_D2 pattern_recall_diag_t g_pattern_recall_diag;

void pattern_recall_diag_reset(uint32_t candidate_generation,
                               uint32_t prepared_seq_generation,
                               uint32_t prepared_audio_generation)
{
    memset(&g_pattern_recall_diag, 0, sizeof(g_pattern_recall_diag));
    g_pattern_recall_diag.magic = PATTERN_RECALL_DIAG_MAGIC;
    g_pattern_recall_diag.version = PATTERN_RECALL_DIAG_VERSION;
    g_pattern_recall_diag.size = (uint16_t)sizeof(g_pattern_recall_diag);
    g_pattern_recall_diag.record_capacity = PATTERN_RECALL_DIAG_RECORD_CAPACITY;
    g_pattern_recall_diag.contract_tag = PATTERN_RECALL_DIAG_CONTRACT_TAG;
    g_pattern_recall_diag.candidate_generation = candidate_generation;
    g_pattern_recall_diag.prepared_seq_generation = prepared_seq_generation;
    g_pattern_recall_diag.prepared_audio_generation = prepared_audio_generation;
}

void pattern_recall_diag_identity(uint32_t candidate_generation,
                                  uint32_t prepared_seq_generation,
                                  uint32_t prepared_audio_generation,
                                  uint8_t transition,
                                  uint64_t effective_sample_time)
{
    g_pattern_recall_diag.candidate_generation = candidate_generation;
    g_pattern_recall_diag.prepared_seq_generation = prepared_seq_generation;
    g_pattern_recall_diag.prepared_audio_generation = prepared_audio_generation;
    g_pattern_recall_diag.transition = transition;
    g_pattern_recall_diag.effective_sample_time = effective_sample_time;
}

void pattern_recall_diag_runtime_begin(void)
{
    /* The diagnostic block lives in a NOLOAD shared-RAM window. A debugger
     * flash/reset can therefore preserve records from a previous image whose
     * generation happens to restart at the same value. Runtime evidence is
     * transaction-local and starts empty once the static sweep has passed. */
    g_pattern_recall_diag.runtime_failure_count = 0U;
    g_pattern_recall_diag.changed_program_mask = 0U;
    memset(g_pattern_recall_diag.polyphony, 0,
           sizeof(g_pattern_recall_diag.polyphony));
    if (g_pattern_recall_diag.static_failure_count == 0U)
    {
        g_pattern_recall_diag.record_count = 0U;
        g_pattern_recall_diag.dropped_record_count = 0U;
    }
}

void pattern_recall_diag_phase(uint8_t phase)
{
    g_pattern_recall_diag.phase = phase;
    if (phase > g_pattern_recall_diag.max_phase)
        g_pattern_recall_diag.max_phase = phase;
}

void pattern_recall_diag_failure(uint8_t runtime, uint8_t subsystem,
    uint8_t entity, uint16_t field, uint8_t code, uint32_t actual,
    uint32_t expected_min, uint32_t max_capacity, uint32_t context)
{
    if (runtime != 0U) ++g_pattern_recall_diag.runtime_failure_count;
    else ++g_pattern_recall_diag.static_failure_count;
    if (g_pattern_recall_diag.record_count
            >= PATTERN_RECALL_DIAG_RECORD_CAPACITY)
    {
        ++g_pattern_recall_diag.dropped_record_count;
        return;
    }
    pattern_recall_diag_record_t *const record =
        &g_pattern_recall_diag.records[g_pattern_recall_diag.record_count++];
    *record = (pattern_recall_diag_record_t){
        .stage = g_pattern_recall_diag.phase,
        .subsystem = subsystem, .entity = entity, .code = code,
        .field = field, .actual = actual, .expected_min = expected_min,
        .max_capacity = max_capacity, .context = context
    };
}

_Static_assert(sizeof(pattern_recall_diag_record_t) == 24U,
               "Pattern Recall diagnostic record size changed");
_Static_assert(sizeof(pattern_recall_diag_t) == 3216U,
               "Pattern Recall diagnostic block size changed");
#endif
