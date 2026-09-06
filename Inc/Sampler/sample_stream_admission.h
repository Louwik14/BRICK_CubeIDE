#pragma once

#include <stdint.h>

#include "Sampler/sample_page_lease.h"
#include "Sampler/sample_stream_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STREAM_MAX_ACTIVE_READERS SAMPLE_STREAM_TARGET_MAX_VOICES
#define SAMPLE_STREAM_ADMISSION_ENTITY_CAPACITY (16U)
#define SAMPLE_STREAM_ADMISSION_PHYSICAL_SLOT_INVALID UINT8_MAX
#define SAMPLE_STREAM_ADMISSION_PHYSICAL_CLASS_CLASSIC (1U)
#define SAMPLE_STREAM_ADMISSION_PHYSICAL_CLASS_MULTI   (2U)
#define SAMPLE_STREAM_ADMISSION_PHYSICAL_CLASS_LOOPER  (3U)
#define SAMPLE_STREAM_ADMISSION_LOGICAL_INVALID UINT8_MAX
#define SAMPLE_STREAM_ADMISSION_BINDING_CAPACITY \
    (SAMPLE_STREAM_ADMISSION_ENTITY_CAPACITY * 9U)

#if defined(__cplusplus)
static_assert(STREAM_MAX_ACTIVE_READERS == SAMPLE_STREAM_TARGET_MAX_VOICES,
              "global stream admission must match the product voice cap");
#else
_Static_assert(STREAM_MAX_ACTIVE_READERS == SAMPLE_STREAM_TARGET_MAX_VOICES,
               "global stream admission must match the product voice cap");
#endif

typedef enum
{
    SAMPLE_STREAM_ADMISSION_OWNER_NONE = 0,
    SAMPLE_STREAM_ADMISSION_OWNER_CLASSIC,
    SAMPLE_STREAM_ADMISSION_OWNER_MULTI,
    SAMPLE_STREAM_ADMISSION_OWNER_LOOPER
} sample_stream_admission_owner_t;

typedef enum
{
    SAMPLE_STREAM_ADMISSION_ROLE_PRIMARY = 0,
    SAMPLE_STREAM_ADMISSION_ROLE_AUXILIARY
} sample_stream_admission_role_t;

typedef enum
{
    SAMPLE_STREAM_ADMISSION_LIFECYCLE_PHYSICALLY_RELEASED = 0,
    SAMPLE_STREAM_ADMISSION_LIFECYCLE_FREE =
        SAMPLE_STREAM_ADMISSION_LIFECYCLE_PHYSICALLY_RELEASED,
    SAMPLE_STREAM_ADMISSION_LIFECYCLE_ACTIVE,
    SAMPLE_STREAM_ADMISSION_LIFECYCLE_RETIRE_REQUESTED
} sample_stream_admission_lifecycle_t;

typedef struct
{
    uint8_t slot;
    uint8_t credit_count;
    uint16_t reserved;
    uint32_t generation;
} sample_stream_admission_token_t;

typedef struct
{
    uint32_t generation;
    uint8_t owner;
    uint8_t role;
    uint8_t lifecycle;
    uint8_t physical_present;
    uint8_t physical_slot;
    uint8_t physical_class;
    uint8_t reserved[2];
} sample_stream_admission_credit_t;

typedef struct
{
    uint8_t active;
    uint8_t entity;
    uint16_t reserved;
    uint32_t output_id;
    sample_stream_admission_token_t token;
} sample_stream_admission_binding_t;

typedef struct
{
    uint32_t generation;
    uint8_t ledger_slot;
    uint8_t owner;
    uint8_t reserved[2];
} sample_stream_admission_release_stamp_t;

extern volatile sample_stream_admission_credit_t
    g_sample_stream_admission_ledger[STREAM_MAX_ACTIVE_READERS];
extern volatile sample_stream_admission_binding_t
    g_sample_stream_admission_bindings[SAMPLE_STREAM_ADMISSION_BINDING_CAPACITY];
extern volatile uint32_t g_sample_stream_admission_release_mask[2];
extern volatile sample_stream_admission_release_stamp_t
    g_sample_stream_admission_release_stamps[56U];

void sample_stream_admission_control_init(void);
uint8_t sample_stream_admission_control_reserve(
    sample_stream_admission_owner_t owner,
    sample_stream_admission_role_t role,
    uint8_t credit_count,
    sample_stream_admission_token_t *out_token);
uint8_t sample_stream_admission_control_request_release(
    const sample_stream_admission_token_t *token);
uint8_t sample_stream_admission_control_bind_output(
    uint8_t entity, uint32_t output_id,
    const sample_stream_admission_token_t *token);
uint8_t sample_stream_admission_control_bind_looper(
    uint8_t track, const sample_stream_admission_token_t *token);
uint8_t sample_stream_admission_control_lookup_looper(
    uint8_t track, sample_stream_admission_token_t *out_token);
uint8_t sample_stream_admission_control_request_output_release(
    uint8_t entity, uint32_t output_id);
uint8_t sample_stream_admission_control_request_looper_release(uint8_t track);
void sample_stream_admission_control_unbind_output(uint8_t entity,
                                                   uint32_t output_id);
void sample_stream_admission_control_retire_loopers(void);
uint8_t sample_stream_admission_control_release_pending(void);
void sample_stream_admission_control_service_releases(void);
void sample_stream_admission_audio_signal_lease(uint8_t slot);

static inline uint8_t sample_stream_admission_token_equal(
    const sample_stream_admission_token_t *left,
    const sample_stream_admission_token_t *right)
{
    return (uint8_t)((left != 0) && (right != 0)
        && (left->slot == right->slot)
        && (left->credit_count == right->credit_count)
        && (left->generation == right->generation));
}

static inline uint8_t sample_stream_admission_audio_token_valid(
    const sample_stream_admission_token_t *token,
    sample_stream_admission_owner_t owner)
{
    if ((token == 0) || (token->slot >= STREAM_MAX_ACTIVE_READERS)
        || (token->credit_count == 0U)
        || ((uint16_t)token->slot + token->credit_count
            > STREAM_MAX_ACTIVE_READERS))
        return 0U;
    for (uint8_t i = 0U; i < token->credit_count; ++i)
    {
        const volatile sample_stream_admission_credit_t *const credit =
            &g_sample_stream_admission_ledger[token->slot + i];
        if ((credit->generation != token->generation)
            || (credit->owner != (uint8_t)owner)
            || (credit->lifecycle != SAMPLE_STREAM_ADMISSION_LIFECYCLE_ACTIVE))
            return 0U;
    }
    return 1U;
}

static inline uint8_t sample_stream_admission_audio_lease_valid(
    const sample_stream_admission_token_t *token,
    sample_stream_admission_owner_t owner,
    sample_stream_admission_role_t role,
    uint8_t physical_class,
    uint8_t physical_slot)
{
    if ((physical_slot == SAMPLE_STREAM_ADMISSION_PHYSICAL_SLOT_INVALID)
        || (sample_stream_admission_audio_token_valid(token, owner) == 0U))
        return 0U;

    uint8_t associated = 0U;
    for (uint8_t i = 0U; i < token->credit_count; ++i)
    {
        const volatile sample_stream_admission_credit_t *const credit =
            &g_sample_stream_admission_ledger[token->slot + i];
        if ((credit->role == (uint8_t)role)
            && (credit->physical_present != 0U)
            && (credit->physical_slot == physical_slot)
            && (credit->physical_class == physical_class))
        {
            associated = 1U;
            break;
        }
    }
    if (associated == 0U)
        return 0U;

    for (uint16_t i = 0U; i < SAMPLE_STREAM_ADMISSION_BINDING_CAPACITY; ++i)
    {
        const volatile sample_stream_admission_binding_t *const binding =
            &g_sample_stream_admission_bindings[i];
        const sample_stream_admission_token_t binding_token = binding->token;
        if ((binding->active != 0U)
            && (sample_stream_admission_token_equal(&binding_token, token) != 0U))
            return 1U;
    }
    return 0U;
}

static inline uint8_t sample_stream_admission_audio_associate_role(
    const sample_stream_admission_token_t *token,
    sample_stream_admission_owner_t owner,
    sample_stream_admission_role_t role,
    uint8_t physical_class,
    uint8_t physical_slot);

static inline uint8_t sample_stream_admission_audio_physical_slot_available(
    const sample_stream_admission_token_t *token,
    uint8_t physical_slot)
{
    for (uint8_t slot = 0U; slot < STREAM_MAX_ACTIVE_READERS; ++slot)
    {
        const volatile sample_stream_admission_credit_t *const credit =
            &g_sample_stream_admission_ledger[slot];
        if ((credit->physical_present == 0U)
            || (credit->physical_slot != physical_slot))
            continue;

        const uint8_t same_token =
            (uint8_t)((credit->generation == token->generation)
                && (slot >= token->slot)
                && (slot < (uint8_t)(token->slot + token->credit_count)));
        if (same_token == 0U)
            return 0U;
    }
    return 1U;
}

static inline uint8_t sample_stream_admission_audio_associate(
    const sample_stream_admission_token_t *token,
    sample_stream_admission_owner_t owner,
    uint8_t physical_class,
    uint8_t physical_slot)
{
    if ((sample_stream_admission_audio_token_valid(token, owner) == 0U)
        || (physical_slot == SAMPLE_STREAM_ADMISSION_PHYSICAL_SLOT_INVALID)
        || (physical_slot >= SAMPLE_PAGE_LEASE_SLOT_COUNT)
        || (sample_stream_admission_audio_physical_slot_available(
                token, physical_slot) == 0U))
        return 0U;
    return sample_stream_admission_audio_associate_role(
        token, owner, SAMPLE_STREAM_ADMISSION_ROLE_PRIMARY,
        physical_class, physical_slot);
}

static inline uint8_t sample_stream_admission_audio_associate_role(
    const sample_stream_admission_token_t *token,
    sample_stream_admission_owner_t owner,
    sample_stream_admission_role_t role,
    uint8_t physical_class,
    uint8_t physical_slot)
{
    if ((sample_stream_admission_audio_token_valid(token, owner) == 0U)
        || (physical_slot == SAMPLE_STREAM_ADMISSION_PHYSICAL_SLOT_INVALID)
        || (physical_slot >= SAMPLE_PAGE_LEASE_SLOT_COUNT)
        || (sample_stream_admission_audio_physical_slot_available(
                token, physical_slot) == 0U))
        return 0U;
    for (uint8_t i = 0U; i < token->credit_count; ++i)
    {
        volatile sample_stream_admission_credit_t *const credit =
            &g_sample_stream_admission_ledger[token->slot + i];
        if (credit->role != (uint8_t)role)
            continue;
        if (credit->physical_present != 0U
            && (credit->physical_slot != physical_slot
                || credit->physical_class != physical_class))
            return 0U;
        credit->physical_slot = physical_slot;
        credit->physical_class = physical_class;
        credit->physical_present = 1U;
        return 1U;
    }
    return 0U;
}

static inline uint8_t sample_stream_admission_audio_lookup_output(
    uint8_t entity, uint32_t output_id,
    sample_stream_admission_token_t *out_token)
{
    if ((out_token == 0) || (entity >= SAMPLE_STREAM_ADMISSION_ENTITY_CAPACITY)
        || (output_id == 0U))
        return 0U;
    for (uint16_t i = 0U; i < SAMPLE_STREAM_ADMISSION_BINDING_CAPACITY; ++i)
    {
        const volatile sample_stream_admission_binding_t *const binding =
            &g_sample_stream_admission_bindings[i];
        if ((binding->active != 0U) && (binding->entity == entity)
            && (binding->output_id == output_id))
        {
            const sample_stream_admission_token_t token = binding->token;
            if ((token.slot < STREAM_MAX_ACTIVE_READERS)
                && (sample_stream_admission_audio_token_valid(
                    &token, (sample_stream_admission_owner_t)
                        g_sample_stream_admission_ledger[token.slot].owner)
                    != 0U))
            {
                *out_token = token;
                return 1U;
            }
        }
    }
    return 0U;
}

static inline uint8_t sample_stream_admission_audio_lookup_entity(
    uint8_t entity, sample_stream_admission_owner_t owner,
    sample_stream_admission_token_t *out_token)
{
    if ((out_token == 0) || (entity >= SAMPLE_STREAM_ADMISSION_ENTITY_CAPACITY)) return 0U;
    for (uint16_t i = 0U; i < SAMPLE_STREAM_ADMISSION_BINDING_CAPACITY; ++i)
    {
        const volatile sample_stream_admission_binding_t *const binding =
            &g_sample_stream_admission_bindings[i];
        if ((binding->active != 0U) && (binding->entity == entity)
            && (binding->output_id < 0x80000000UL)
            && (binding->token.slot < STREAM_MAX_ACTIVE_READERS))
        {
            const sample_stream_admission_token_t token = binding->token;
            if (sample_stream_admission_audio_token_valid(&token, owner) != 0U)
            {
                *out_token = token;
                return 1U;
            }
        }
    }
    return 0U;
}

#ifdef __cplusplus
}
#endif
