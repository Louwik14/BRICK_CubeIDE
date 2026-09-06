#include "Sampler/sample_stream_admission.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "Sampler/sample_page_lease.h"
#include "Sampler/sample_page_lease_control.h"
#include "stm32h7xx.h"

static uint32_t g_sample_stream_admission_generation;

static uint8_t sample_stream_admission_token_shape_valid(
    const sample_stream_admission_token_t *token)
{
    return (uint8_t)((token != 0)
        && (token->slot < STREAM_MAX_ACTIVE_READERS)
        && (token->credit_count != 0U)
        && ((uint16_t)token->slot + token->credit_count
            <= STREAM_MAX_ACTIVE_READERS));
}

static uint8_t sample_stream_admission_token_matches(
    const sample_stream_admission_token_t *token,
    uint8_t expected_owner)
{
    if ((sample_stream_admission_token_shape_valid(token) == 0U)
        || (token->generation == 0U))
        return 0U;
    for (uint8_t i = 0U; i < token->credit_count; ++i)
    {
        const volatile sample_stream_admission_credit_t *const credit =
            &g_sample_stream_admission_ledger[token->slot + i];
        if ((credit->generation != token->generation)
            || (credit->owner != expected_owner)
            || (credit->lifecycle != SAMPLE_STREAM_ADMISSION_LIFECYCLE_ACTIVE))
            return 0U;
    }
    return 1U;
}

void sample_stream_admission_control_init(void)
{
    memset((void *)g_sample_stream_admission_ledger, 0,
           sizeof(g_sample_stream_admission_ledger));
    memset((void *)g_sample_stream_admission_bindings, 0,
           sizeof(g_sample_stream_admission_bindings));
    g_sample_stream_admission_release_mask[0] = 0U;
    g_sample_stream_admission_release_mask[1] = 0U;
    memset((void *)g_sample_stream_admission_release_stamps, 0,
           sizeof(g_sample_stream_admission_release_stamps));
    g_sample_stream_admission_generation = 0U;
    __DMB();
}

uint8_t sample_stream_admission_control_reserve(
    sample_stream_admission_owner_t owner,
    sample_stream_admission_role_t role,
    uint8_t credit_count,
    sample_stream_admission_token_t *out_token)
{
    if ((out_token == 0) || (owner == SAMPLE_STREAM_ADMISSION_OWNER_NONE)
        || (credit_count == 0U) || (credit_count > STREAM_MAX_ACTIVE_READERS))
        return 0U;

    uint8_t first = SAMPLE_STREAM_ADMISSION_PHYSICAL_SLOT_INVALID;
    uint8_t run = 0U;
    for (uint8_t slot = 0U; slot < STREAM_MAX_ACTIVE_READERS; ++slot)
    {
        const volatile sample_stream_admission_credit_t *const credit =
            &g_sample_stream_admission_ledger[slot];
        if (credit->lifecycle == SAMPLE_STREAM_ADMISSION_LIFECYCLE_FREE)
        {
            if (run == 0U) first = slot;
            ++run;
            if (run == credit_count) break;
        }
        else
        {
            first = SAMPLE_STREAM_ADMISSION_PHYSICAL_SLOT_INVALID;
            run = 0U;
        }
    }
    if (run != credit_count || first >= STREAM_MAX_ACTIVE_READERS)
        return 0U;

    uint32_t generation = ++g_sample_stream_admission_generation;
    if (generation == 0U)
        generation = ++g_sample_stream_admission_generation;
    for (uint8_t i = 0U; i < credit_count; ++i)
    {
        volatile sample_stream_admission_credit_t *const credit =
            &g_sample_stream_admission_ledger[first + i];
        *credit = (sample_stream_admission_credit_t){
            .generation = generation,
            .owner = (uint8_t)owner,
            .role = (uint8_t)((i == 0U) ? role
                : SAMPLE_STREAM_ADMISSION_ROLE_AUXILIARY),
            .lifecycle = SAMPLE_STREAM_ADMISSION_LIFECYCLE_ACTIVE,
            .physical_present = 0U,
            .physical_slot = SAMPLE_STREAM_ADMISSION_PHYSICAL_SLOT_INVALID,
            .physical_class = 0U
        };
    }
    *out_token = (sample_stream_admission_token_t){
        .slot = first,
        .credit_count = credit_count,
        .generation = generation
    };
    __DMB();
    return 1U;
}

uint8_t sample_stream_admission_control_request_release(
    const sample_stream_admission_token_t *token)
{
    if ((token == 0) || (sample_stream_admission_token_shape_valid(token) == 0U))
        return 0U;
    for (uint8_t i = 0U; i < token->credit_count; ++i)
    {
        volatile sample_stream_admission_credit_t *const credit =
            &g_sample_stream_admission_ledger[token->slot + i];
        if ((credit->generation != token->generation)
            || (credit->lifecycle == SAMPLE_STREAM_ADMISSION_LIFECYCLE_FREE))
            return 0U;
    }
    uint8_t physical_present = 0U;
    for (uint8_t i = 0U; i < token->credit_count; ++i)
    {
        g_sample_stream_admission_ledger[token->slot + i].lifecycle =
            SAMPLE_STREAM_ADMISSION_LIFECYCLE_RETIRE_REQUESTED;
        physical_present |= g_sample_stream_admission_ledger[
            token->slot + i].physical_present;
    }
    if (physical_present == 0U)
    {
        for (uint8_t i = 0U; i < token->credit_count; ++i)
            g_sample_stream_admission_ledger[token->slot + i] =
                (sample_stream_admission_credit_t){0};
        for (uint16_t i = 0U; i < SAMPLE_STREAM_ADMISSION_BINDING_CAPACITY; ++i)
            if ((g_sample_stream_admission_bindings[i].active != 0U)
                && (g_sample_stream_admission_bindings[i].token.generation
                    == token->generation)
                && (g_sample_stream_admission_bindings[i].token.slot
                    == token->slot))
                g_sample_stream_admission_bindings[i].active = 0U;
    }
    __DMB();
    return 1U;
}

static int16_t sample_stream_admission_find_binding(uint8_t entity,
                                                    uint32_t output_id)
{
    for (uint16_t i = 0U; i < SAMPLE_STREAM_ADMISSION_BINDING_CAPACITY; ++i)
    {
        const volatile sample_stream_admission_binding_t *const binding =
            &g_sample_stream_admission_bindings[i];
        if ((binding->active != 0U) && (binding->entity == entity)
            && (binding->output_id == output_id))
            return (int16_t)i;
    }
    return -1;
}

static int16_t sample_stream_admission_find_free_binding(void)
{
    for (uint16_t i = 0U; i < SAMPLE_STREAM_ADMISSION_BINDING_CAPACITY; ++i)
        if (g_sample_stream_admission_bindings[i].active == 0U)
            return (int16_t)i;
    return -1;
}

uint8_t sample_stream_admission_control_bind_output(
    uint8_t entity, uint32_t output_id,
    const sample_stream_admission_token_t *token)
{
    if ((entity >= SAMPLE_STREAM_ADMISSION_ENTITY_CAPACITY) || (output_id == 0U)
        || (token == 0))
        return 0U;
    uint8_t owner = SAMPLE_STREAM_ADMISSION_OWNER_NONE;
    if (sample_stream_admission_token_shape_valid(token) != 0U)
        owner = g_sample_stream_admission_ledger[token->slot].owner;
    if ((owner != SAMPLE_STREAM_ADMISSION_OWNER_CLASSIC)
        && (owner != SAMPLE_STREAM_ADMISSION_OWNER_MULTI))
        return 0U;
    if (sample_stream_admission_token_matches(token, owner) == 0U)
        return 0U;
    int16_t index = sample_stream_admission_find_binding(entity, output_id);
    if (index >= 0)
    {
        const sample_stream_admission_token_t existing =
            g_sample_stream_admission_bindings[index].token;
        if (sample_stream_admission_token_equal(&existing, token) == 0U)
            return 0U;
    }
    if (index < 0) index = sample_stream_admission_find_free_binding();
    if (index < 0) return 0U;
    g_sample_stream_admission_bindings[index] =
        (sample_stream_admission_binding_t){
            .active = 1U, .entity = entity, .output_id = output_id,
            .token = *token
        };
    __DMB();
    return 1U;
}

uint8_t sample_stream_admission_control_bind_looper(
    uint8_t track, const sample_stream_admission_token_t *token)
{
    if ((track >= SAMPLE_STREAM_ADMISSION_ENTITY_CAPACITY) || (token == 0)
        || (token->credit_count != 2U)
        || (sample_stream_admission_token_matches(
            token, SAMPLE_STREAM_ADMISSION_OWNER_LOOPER) == 0U))
        return 0U;
    const uint32_t output_id = 0x80000000UL | track;
    int16_t index = sample_stream_admission_find_binding(track, output_id);
    if (index >= 0)
    {
        const sample_stream_admission_token_t existing =
            g_sample_stream_admission_bindings[index].token;
        if (sample_stream_admission_token_equal(&existing, token) == 0U)
            return 0U;
    }
    if (index < 0) index = sample_stream_admission_find_free_binding();
    if (index < 0) return 0U;
    g_sample_stream_admission_bindings[index] =
        (sample_stream_admission_binding_t){
            .active = 1U, .entity = track, .output_id = output_id,
            .token = *token
        };
    __DMB();
    return 1U;
}

uint8_t sample_stream_admission_control_lookup_looper(
    uint8_t track, sample_stream_admission_token_t *out_token)
{
    if ((track >= SAMPLE_STREAM_ADMISSION_ENTITY_CAPACITY)
            || (out_token == 0))
        return 0U;

    const uint32_t output_id = 0x80000000UL | track;
    for (uint16_t i = 0U; i < SAMPLE_STREAM_ADMISSION_BINDING_CAPACITY; ++i)
    {
        const volatile sample_stream_admission_binding_t *const binding =
            &g_sample_stream_admission_bindings[i];
        if ((binding->active == 0U) || (binding->entity != track)
                || (binding->output_id != output_id))
            continue;

        const sample_stream_admission_token_t token = binding->token;
        if ((token.credit_count == 2U)
                && (sample_stream_admission_token_matches(
                    &token, SAMPLE_STREAM_ADMISSION_OWNER_LOOPER) != 0U))
        {
            *out_token = token;
            return 1U;
        }
    }
    return 0U;
}

uint8_t sample_stream_admission_control_request_output_release(
    uint8_t entity, uint32_t output_id)
{
    const int16_t index = sample_stream_admission_find_binding(entity, output_id);
    if (index < 0) return 0U;
    const sample_stream_admission_token_t token =
        g_sample_stream_admission_bindings[index].token;
    (void)sample_stream_admission_control_request_release(&token);
    return 1U;
}

uint8_t sample_stream_admission_control_request_looper_release(uint8_t track)
{
    if (track >= SAMPLE_STREAM_ADMISSION_ENTITY_CAPACITY) return 0U;
    return sample_stream_admission_control_request_output_release(
        track, 0x80000000UL | track);
}

void sample_stream_admission_control_unbind_output(uint8_t entity,
                                                   uint32_t output_id)
{
    const int16_t index = sample_stream_admission_find_binding(entity, output_id);
    if (index >= 0)
        g_sample_stream_admission_bindings[index].active = 0U;
}

void sample_stream_admission_control_retire_loopers(void)
{
    for (uint16_t i = 0U; i < SAMPLE_STREAM_ADMISSION_BINDING_CAPACITY; ++i)
    {
        const volatile sample_stream_admission_binding_t *const binding =
            &g_sample_stream_admission_bindings[i];
        if ((binding->active != 0U)
            && (binding->token.credit_count == 2U)
            && (g_sample_stream_admission_ledger[binding->token.slot].owner
                == SAMPLE_STREAM_ADMISSION_OWNER_LOOPER))
        {
            const sample_stream_admission_token_t token = binding->token;
            (void)sample_stream_admission_control_request_release(&token);
        }
    }
}

uint8_t sample_stream_admission_control_release_pending(void)
{
    return ((g_sample_stream_admission_release_mask[0] != 0U)
        || (g_sample_stream_admission_release_mask[1] != 0U)) ? 1U : 0U;
}

static uint32_t sample_stream_admission_control_exchange_word(
    volatile uint32_t *word)
{
    uint32_t value;
    uint32_t status;
    do
    {
        value = __LDREXW(word);
        status = __STREXW(0U, word);
    } while (status != 0U);
    __DMB();
    return value;
}

static void sample_stream_admission_control_requeue_word(
    volatile uint32_t *word, uint32_t bits)
{
    uint32_t old_value;
    uint32_t new_value;
    do
    {
        old_value = __LDREXW(word);
        new_value = old_value | bits;
    } while (__STREXW(new_value, word) != 0U);
    __DMB();
}

static uint8_t sample_stream_admission_lease_empty_stable(uint8_t slot)
{
    if (slot >= SAMPLE_PAGE_LEASE_SLOT_COUNT)
        return 0U;
    const uint32_t before = g_sample_page_leases[slot].seq;
    sample_page_lease_t lease;
    const uint8_t readable = sample_page_lease_control_read(slot, &lease);
    const uint32_t after = g_sample_page_leases[slot].seq;
    return (uint8_t)((readable == 0U) && (before != 0U)
        && ((before & 1U) == 0U) && (before == after));
}

void sample_stream_admission_control_service_releases(void)
{
    const uint32_t pending_words[2] = {
        sample_stream_admission_control_exchange_word(
            &g_sample_stream_admission_release_mask[0]),
        sample_stream_admission_control_exchange_word(
            &g_sample_stream_admission_release_mask[1])
    };
    for (uint8_t word = 0U; word < 2U; ++word)
    {
        uint32_t pending = pending_words[word];
        while (pending != 0U)
        {
            const uint8_t physical_slot = (uint8_t)(
                (word * 32U) + (uint8_t)__builtin_ctz(pending));
            const uint32_t bit = (uint32_t)1U
                << (physical_slot % 32U);
            pending &= pending - 1U;
            if ((physical_slot >= SAMPLE_PAGE_LEASE_SLOT_COUNT)
                || (sample_stream_admission_lease_empty_stable(
                    physical_slot) == 0U))
            {
                if (physical_slot < SAMPLE_PAGE_LEASE_SLOT_COUNT)
                    sample_stream_admission_control_requeue_word(
                        &g_sample_stream_admission_release_mask[word], bit);
                continue;
            }
            const sample_stream_admission_release_stamp_t stamp =
                g_sample_stream_admission_release_stamps[physical_slot];
            if ((stamp.generation == 0U)
                || (stamp.ledger_slot >= STREAM_MAX_ACTIVE_READERS))
                continue;
            volatile sample_stream_admission_credit_t *const credit =
                &g_sample_stream_admission_ledger[stamp.ledger_slot];
            if ((credit->generation == stamp.generation)
                && (credit->owner == stamp.owner)
                && (credit->physical_present != 0U)
                && (credit->physical_slot == physical_slot))
                credit->physical_present = 0U;
        }
    }
    for (uint8_t i = 0U; i < STREAM_MAX_ACTIVE_READERS; ++i)
    {
        volatile sample_stream_admission_credit_t *const credit =
            &g_sample_stream_admission_ledger[i];
        if (credit->lifecycle != SAMPLE_STREAM_ADMISSION_LIFECYCLE_RETIRE_REQUESTED)
            continue;
        const uint32_t generation = credit->generation;
        const uint8_t owner = credit->owner;
        uint8_t group_free = 1U;
        for (uint8_t j = i; j < STREAM_MAX_ACTIVE_READERS; ++j)
        {
            const volatile sample_stream_admission_credit_t *const member =
                &g_sample_stream_admission_ledger[j];
            if ((member->generation == generation) && (member->owner == owner)
                && (member->lifecycle == SAMPLE_STREAM_ADMISSION_LIFECYCLE_RETIRE_REQUESTED)
                && (member->physical_present != 0U))
                group_free = 0U;
        }
        if (group_free == 0U) continue;
        for (uint8_t j = i; j < STREAM_MAX_ACTIVE_READERS; ++j)
        {
            volatile sample_stream_admission_credit_t *const member =
                &g_sample_stream_admission_ledger[j];
            if ((member->generation == generation) && (member->owner == owner))
                *member = (sample_stream_admission_credit_t){0};
        }
        for (uint16_t j = 0U; j < SAMPLE_STREAM_ADMISSION_BINDING_CAPACITY; ++j)
        {
            volatile sample_stream_admission_binding_t *const binding =
                &g_sample_stream_admission_bindings[j];
            if ((binding->active != 0U)
                && (binding->token.generation == generation)
                && (binding->token.slot == i))
                binding->active = 0U;
        }
    }

    __DMB();
}
