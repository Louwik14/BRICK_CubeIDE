#ifndef RESTORE_PLAN_CONTRACT_H
#define RESTORE_PLAN_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

#include "Param/param_store.h"
#include "Storage/persistent_control_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed, pointer-free ABI shared by CONTROL and AUDIO.  The payload is a
 * singleton: CONTROL must keep it immutable from publication until AUDIO has
 * copied completed_seq to request_seq. */
#define RESTORE_PLAN_MAGIC                         0x52535452UL /* RSTR */
#define RESTORE_PLAN_ABI_VERSION                   1U
#define RESTORE_PLAN_CACHE_LINE_BYTES             32U
#define RESTORE_PLAN_ENTITY_NONE                  0xFFU
#define RESTORE_PLAN_MAX_TRACK_ITEMS \
    (PERSIST_CONTROL_ENTITY_COUNT * PERSIST_CONTROL_ENTITY_PARAM_COUNT)
#define RESTORE_PLAN_MAX_GLOBAL_ITEMS             PERSIST_CONTROL_GLOBAL_PARAM_COUNT
#define RESTORE_PLAN_MAX_ITEMS \
    (RESTORE_PLAN_MAX_TRACK_ITEMS + RESTORE_PLAN_MAX_GLOBAL_ITEMS)
#define RESTORE_PLAN_SDRAM_BUDGET_BYTES           (48U * 1024U)
#define RESTORE_PLAN_MAILBOX_BUDGET_BYTES         64U

typedef enum
{
    RESTORE_PLAN_PHASE_BINDING = 0,
    RESTORE_PLAN_PHASE_MODEL,
    RESTORE_PLAN_PHASE_DEPENDENT,
    RESTORE_PLAN_PHASE_TRACK,
    RESTORE_PLAN_PHASE_MIX,
    RESTORE_PLAN_PHASE_GLOBAL,
    RESTORE_PLAN_PHASE_FINALIZE,
    RESTORE_PLAN_PHASE_COUNT
} restore_plan_phase_t;

typedef enum
{
    RESTORE_TRANSACTION_IDLE = 0,
    RESTORE_TRANSACTION_PREPARED,
    RESTORE_TRANSACTION_COMMITTING
} restore_transaction_state_t;

typedef enum
{
    RESTORE_RESULT_NONE = 0,
    RESTORE_RESULT_COMPLETE,
    RESTORE_RESULT_REJECTED_CONTRACT
} restore_result_t;

typedef struct
{
    uint16_t param_id;
    uint8_t entity;
    uint8_t phase;
    float value;
} restore_plan_item_t;

typedef struct
{
    uint32_t family_key;
    uint32_t type_key;
    uint32_t input_key;
    uint16_t resource_index;
    uint8_t entity;
    uint8_t midi_channel;
    uint8_t midi_source;
    uint8_t flags;
    uint8_t voice_count;
    uint8_t voice_spread_q7;
} restore_plan_binding_t;

typedef struct
{
    uint32_t magic;
    uint16_t abi_version;
    uint16_t header_bytes;
    uint32_t request_seq;
    uint32_t plan_bytes;
    uint32_t payload_crc32;
    uint16_t item_count;
    uint8_t binding_count;
    uint8_t flags;
    uint32_t reserved[2];
} restore_plan_header_t;

typedef struct
{
    restore_plan_header_t header;
    restore_plan_binding_t bindings[PERSIST_CONTROL_ENTITY_COUNT];
    restore_plan_item_t items[RESTORE_PLAN_MAX_ITEMS];
} restore_audio_plan_t;

/* Only fixed-width scalars cross the core boundary.  The plan address is a
 * linker-owned singleton and is deliberately absent from the mailbox. */
typedef struct
{
    volatile uint32_t request_seq;
    volatile uint32_t completed_seq;
    volatile uint32_t state;
    volatile uint32_t result;
    volatile uint32_t plan_crc32;
    volatile uint32_t plan_bytes;
    volatile uint32_t reserved[10];
} restore_plan_mailbox_t;

#if defined(__cplusplus)
#define RESTORE_CONTRACT_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define RESTORE_CONTRACT_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

RESTORE_CONTRACT_STATIC_ASSERT(PARAM_COUNT <= UINT16_MAX,
                               "restore param_id no longer fits uint16_t");
RESTORE_CONTRACT_STATIC_ASSERT(PERSIST_CONTROL_ENTITY_COUNT < RESTORE_PLAN_ENTITY_NONE,
                               "restore entity sentinel collides with an entity");
RESTORE_CONTRACT_STATIC_ASSERT(sizeof(float) == 4U,
                               "restore ABI requires 32-bit float");
RESTORE_CONTRACT_STATIC_ASSERT(sizeof(restore_plan_item_t) == 8U,
                               "restore item ABI changed");
RESTORE_CONTRACT_STATIC_ASSERT(sizeof(restore_plan_binding_t) == 20U,
                               "restore binding ABI changed");
RESTORE_CONTRACT_STATIC_ASSERT(sizeof(restore_plan_header_t) == 32U,
                               "restore header ABI changed");
RESTORE_CONTRACT_STATIC_ASSERT(sizeof(restore_plan_mailbox_t) ==
                                   RESTORE_PLAN_MAILBOX_BUDGET_BYTES,
                               "restore mailbox ABI changed");
RESTORE_CONTRACT_STATIC_ASSERT(sizeof(restore_audio_plan_t) <=
                                   RESTORE_PLAN_SDRAM_BUDGET_BYTES,
                               "restore plan exceeds its SDRAM budget");
RESTORE_CONTRACT_STATIC_ASSERT((sizeof(restore_audio_plan_t) %
                                RESTORE_PLAN_CACHE_LINE_BYTES) == 0U,
                               "restore plan must occupy whole cache lines");
RESTORE_CONTRACT_STATIC_ASSERT(offsetof(restore_audio_plan_t, items) == 352U,
                               "restore payload offset changed");

#undef RESTORE_CONTRACT_STATIC_ASSERT

extern restore_audio_plan_t g_restore_audio_plan;
extern restore_plan_mailbox_t g_restore_plan_mailbox;

#ifdef __cplusplus
}
#endif

#endif /* RESTORE_PLAN_CONTRACT_H */
