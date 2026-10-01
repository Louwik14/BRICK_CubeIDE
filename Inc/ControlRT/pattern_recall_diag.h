#ifndef PATTERN_RECALL_DIAG_H
#define PATTERN_RECALL_DIAG_H

#include <stdint.h>

#ifndef BRICK_PATTERN_RECALL_DIAG
#define BRICK_PATTERN_RECALL_DIAG 0
#endif

#if BRICK_PATTERN_RECALL_DIAG

#define PATTERN_RECALL_DIAG_MAGIC UINT32_C(0x44525042)
#define PATTERN_RECALL_DIAG_VERSION 2U
#define PATTERN_RECALL_DIAG_CONTRACT_TAG UINT16_C(0x812F)
#define PATTERN_RECALL_DIAG_RECORD_CAPACITY 128U

typedef enum
{
    PATTERN_DIAG_PHASE_NONE = 0U,
    PATTERN_DIAG_PHASE_PATTERN_DECODED,
    PATTERN_DIAG_PHASE_PATTERN_VALIDATED,
    PATTERN_DIAG_PHASE_PATTERN_PREPARED,
    PATTERN_DIAG_PHASE_CONTROL_COMMIT_BEGIN,
    PATTERN_DIAG_PHASE_CONTROL_COMMIT_DONE,
    PATTERN_DIAG_PHASE_PREPARED_AUDIO_FINALIZE_BEGIN,
    PATTERN_DIAG_PHASE_PREPARED_AUDIO_FINALIZE_DONE,
    PATTERN_DIAG_PHASE_SEQ_COMMIT_BEGIN,
    PATTERN_DIAG_PHASE_SEQ_COMMIT_DONE,
    PATTERN_DIAG_PHASE_AUDIO_PUBLISH,
    PATTERN_DIAG_PHASE_AUDIO_IRQ_ENTER,
    PATTERN_DIAG_PHASE_AUDIO_RUNTIME_PREFLIGHT,
    PATTERN_DIAG_PHASE_POLYPHONY_TRIM,
    PATTERN_DIAG_PHASE_PROGRAM_CLOSE,
    PATTERN_DIAG_PHASE_PROGRAM_OFF,
    PATTERN_DIAG_PHASE_PROGRAM_INSTALL,
    PATTERN_DIAG_PHASE_PRODUCT_APPLY,
    PATTERN_DIAG_PHASE_FILTER_APPLY,
    PATTERN_DIAG_PHASE_VCA_APPLY,
    PATTERN_DIAG_PHASE_MIXER_APPLY,
    PATTERN_DIAG_PHASE_FX_APPLY,
    PATTERN_DIAG_PHASE_POLY_APPLY,
    PATTERN_DIAG_PHASE_MOD_APPLY,
    PATTERN_DIAG_PHASE_RESOURCE_APPLY,
    PATTERN_DIAG_PHASE_GLOBAL_APPLY,
    PATTERN_DIAG_PHASE_TEMP_CLEAR,
    PATTERN_DIAG_PHASE_REBIND,
    PATTERN_DIAG_PHASE_AUDIO_COMMIT_DONE,
    PATTERN_DIAG_PHASE_FENCE_DONE
} pattern_recall_diag_phase_t;

typedef enum
{
    PATTERN_DIAG_SUBSYSTEM_META = 0U,
    PATTERN_DIAG_SUBSYSTEM_DIMENSION,
    PATTERN_DIAG_SUBSYSTEM_PROGRAM,
    PATTERN_DIAG_SUBSYSTEM_TONE,
    PATTERN_DIAG_SUBSYSTEM_FM,
    PATTERN_DIAG_SUBSYSTEM_FILTER,
    PATTERN_DIAG_SUBSYSTEM_VCA,
    PATTERN_DIAG_SUBSYSTEM_MIXER,
    PATTERN_DIAG_SUBSYSTEM_FX,
    PATTERN_DIAG_SUBSYSTEM_POLYPHONY,
    PATTERN_DIAG_SUBSYSTEM_MOD,
    PATTERN_DIAG_SUBSYSTEM_RESOURCE,
    PATTERN_DIAG_SUBSYSTEM_GLOBAL,
    PATTERN_DIAG_SUBSYSTEM_TRANSPORT,
    PATTERN_DIAG_SUBSYSTEM_INPUT,
    PATTERN_DIAG_SUBSYSTEM_TEMP_CLEAR,
    PATTERN_DIAG_SUBSYSTEM_FULL_TARGET,
    PATTERN_DIAG_SUBSYSTEM_REBIND
} pattern_recall_diag_subsystem_t;

typedef enum
{
    PATTERN_DIAG_CODE_RANGE = 1U,
    PATTERN_DIAG_CODE_CAPACITY,
    PATTERN_DIAG_CODE_DOMAIN,
    PATTERN_DIAG_CODE_TOPOLOGY,
    PATTERN_DIAG_CODE_MISMATCH,
    PATTERN_DIAG_CODE_STALE,
    PATTERN_DIAG_CODE_ENDPOINT,
    PATTERN_DIAG_CODE_NOT_READY,
    PATTERN_DIAG_CODE_GENERATION,
    PATTERN_DIAG_CODE_INSTALL,
    PATTERN_DIAG_CODE_HELD_OUTPUTS,
    PATTERN_DIAG_CODE_VOICE_BUDGET,
    PATTERN_DIAG_CODE_REBIND
} pattern_recall_diag_code_t;

typedef struct
{
    uint8_t stage;
    uint8_t subsystem;
    uint8_t entity;
    uint8_t code;
    uint16_t field;
    uint16_t reserved;
    uint32_t actual;
    uint32_t expected_min;
    uint32_t max_capacity;
    uint32_t context;
} pattern_recall_diag_record_t;

typedef struct
{
    uint8_t current_renderer;
    uint8_t current_held_count;
    uint8_t target_polyphony;
    uint8_t trim_count;
    uint8_t owner_renderer;
    uint8_t changed;
} pattern_recall_diag_polyphony_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t candidate_generation;
    uint32_t prepared_seq_generation;
    uint32_t prepared_audio_generation;
    uint64_t effective_sample_time;
    uint8_t transition;
    uint8_t phase;
    uint8_t max_phase;
    uint8_t record_capacity;
    uint16_t static_failure_count;
    uint16_t runtime_failure_count;
    uint16_t record_count;
    uint16_t dropped_record_count;
    uint16_t changed_program_mask;
    uint16_t contract_tag;
    pattern_recall_diag_polyphony_t polyphony[16U];
    pattern_recall_diag_record_t records[PATTERN_RECALL_DIAG_RECORD_CAPACITY];
} pattern_recall_diag_t;

extern pattern_recall_diag_t g_pattern_recall_diag;

void pattern_recall_diag_reset(uint32_t candidate_generation,
                               uint32_t prepared_seq_generation,
                               uint32_t prepared_audio_generation);
void pattern_recall_diag_identity(uint32_t candidate_generation,
                                  uint32_t prepared_seq_generation,
                                  uint32_t prepared_audio_generation,
                                  uint8_t transition,
                                  uint64_t effective_sample_time);
void pattern_recall_diag_runtime_begin(void);
void pattern_recall_diag_phase(uint8_t phase);
void pattern_recall_diag_failure(uint8_t runtime, uint8_t subsystem,
    uint8_t entity, uint16_t field, uint8_t code, uint32_t actual,
    uint32_t expected_min, uint32_t max_capacity, uint32_t context);

#endif
#endif
