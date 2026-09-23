/* One chronological Undo/Redo history for sequence and REC_SOURCE actions. */
#include "Storage/undo_v2.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_param_iface.h"
#include "Storage/rec_source.h"
#include "Track/entity_topology.h"
#include "main.h"

#define UNDO_V2_SEQUENCE_SLOT_COUNT (UNDO_V2_MAX_SEQUENCE_TRANSACTIONS + 1U)
#define UNDO_V2_HISTORY_AUDIO 0xFEU
#define UNDO_V2_HISTORY_FREE  0xFFU
#define UNDO_V2_LOCK_KEY_FREE 0xFFU

#define UNDO_V2_PLAY_NOTE_SHIFT       0U
#define UNDO_V2_PLAY_VELOCITY_SHIFT   8U
#define UNDO_V2_PLAY_LENGTH_SHIFT    16U
#define UNDO_V2_PLAY_MICRO_SHIFT     23U
#define UNDO_V2_PLAY_TERMINAL_SHIFT  29U
#define UNDO_V2_PLAY_BYTE_MASK       0xFFU
#define UNDO_V2_PLAY_LENGTH_MASK     0x7FU
#define UNDO_V2_PLAY_MICRO_MASK      0x3FU
#define UNDO_V2_PLAY_RESERVED_MASK   0xC0000000UL
#define UNDO_V2_PLAY_ABSENT          0x80U

typedef struct
{
    seq_value16_t value16;
    uint8_t key;
    uint8_t flags;
} undo_v2_compact_lock_t;

/* The arrays are deliberately SoA: no alignment hole is paid per step. */
typedef struct
{
    undo_v2_compact_lock_t locks[SEQ_STEP_SNAPSHOT_MAX_STEPS]
                                      [SEQ_STEP_SNAPSHOT_MAX_LOCKS];
    uint32_t play[SEQ_STEP_SNAPSHOT_MAX_STEPS][SEQ_PLAY_MAX_CAPACITY];
    uint8_t state[SEQ_STEP_SNAPSHOT_MAX_STEPS];
    uint32_t step_bitmap[2];
    uint8_t track;
    uint8_t reserved[3];
} undo_v2_sequence_slot_t;

typedef struct
{
    uint32_t before_generation;
    uint32_t after_generation;
} undo_v2_audio_entry_t;

typedef struct
{
    uint8_t tx_open, count, cursor, apply_in_progress;
    uint8_t capture_suspended, pending_slot, audio_apply_pending;
    uint8_t audio_apply_redo;
    undo_v2_status_t last_status;
} undo_v2_runtime_t;

typedef struct
{
    undo_v2_sequence_slot_t sequence_slots[UNDO_V2_SEQUENCE_SLOT_COUNT];
    undo_v2_audio_entry_t audio;
    undo_v2_runtime_t runtime;
    /* Sequence entries contain a slot index; Audio uses the marker above. */
    uint8_t history[UNDO_V2_MAX_TRANSACTIONS];
} undo_v2_arena_t;

_Static_assert(SEQ_STEP_SNAPSHOT_MAX_STEPS == 64U,
               "compact Undo bitmap requires 64 steps");
_Static_assert(SEQ_STEP_SNAPSHOT_MAX_LOCKS == 32U,
               "compact Undo lock capacity changed");
_Static_assert(SEQ_PARAM_RUNTIME_SLOT_COUNT < UNDO_V2_LOCK_KEY_FREE,
               "compact Undo key sentinel collides with a parameter key");
_Static_assert(sizeof(undo_v2_compact_lock_t) == 4U,
               "compact Undo lock size changed");
_Static_assert(sizeof(undo_v2_sequence_slot_t) == 10316U,
               "compact Undo sequence slot size changed");
_Static_assert(sizeof(undo_v2_arena_t) == 92872U,
               "compact Undo arena size changed");

UI_SDRAM static undo_v2_arena_t g_undo_v2;

static void set_status(undo_v2_status_t status)
{
    g_undo_v2.runtime.last_status = status;
}

static uint8_t bitmap_has(const uint32_t bitmap[2], seq_step_id_t step)
{
    return (uint8_t)((bitmap[step >> 5U] & (1UL << (step & 31U))) != 0U);
}

static void bitmap_add(uint32_t bitmap[2], seq_step_id_t step)
{
    bitmap[step >> 5U] |= 1UL << (step & 31U);
}

static uint8_t sequence_slot_referenced(uint8_t slot)
{
    for (uint8_t i = 0U; i < g_undo_v2.runtime.count; ++i)
        if (g_undo_v2.history[i] == slot) return 1U;
    return 0U;
}

static uint8_t find_free_sequence_slot(void)
{
    for (uint8_t slot = 0U; slot < UNDO_V2_SEQUENCE_SLOT_COUNT; ++slot)
        if (sequence_slot_referenced(slot) == 0U) return slot;
    return UNDO_V2_HISTORY_FREE;
}

static uint32_t pack_play_item(const seq_play_item_t *item)
{
    const uint8_t mask = item->present_mask;
    const uint32_t note = ((mask & SEQ_STEP_PLAY_PRESENT_NOTE) != 0U)
        ? item->note : UNDO_V2_PLAY_ABSENT;
    const uint32_t velocity = ((mask & SEQ_STEP_PLAY_PRESENT_VELOCITY) != 0U)
        ? item->velocity : UNDO_V2_PLAY_ABSENT;
    const uint32_t length = ((mask & SEQ_STEP_PLAY_PRESENT_LENGTH) != 0U)
        ? item->length : 0U;
    const uint32_t micro = ((mask & SEQ_STEP_PLAY_PRESENT_MICROTIMING) != 0U)
        ? (uint32_t)((int16_t)item->microtiming + 25) : 0U;
    const uint32_t terminal = ((mask & SEQ_STEP_PLAY_TERMINAL) != 0U) ? 1U : 0U;
    return (note << UNDO_V2_PLAY_NOTE_SHIFT)
        | (velocity << UNDO_V2_PLAY_VELOCITY_SHIFT)
        | (length << UNDO_V2_PLAY_LENGTH_SHIFT)
        | (micro << UNDO_V2_PLAY_MICRO_SHIFT)
        | (terminal << UNDO_V2_PLAY_TERMINAL_SHIFT);
}

static uint8_t unpack_play_item(uint32_t packed, seq_play_item_t *item)
{
    if ((item == NULL) || ((packed & UNDO_V2_PLAY_RESERVED_MASK) != 0U)) return 0U;
    memset(item, 0, sizeof(*item));

    const uint8_t note = (uint8_t)((packed >> UNDO_V2_PLAY_NOTE_SHIFT)
                                   & UNDO_V2_PLAY_BYTE_MASK);
    const uint8_t velocity = (uint8_t)((packed >> UNDO_V2_PLAY_VELOCITY_SHIFT)
                                       & UNDO_V2_PLAY_BYTE_MASK);
    const uint8_t length = (uint8_t)((packed >> UNDO_V2_PLAY_LENGTH_SHIFT)
                                     & UNDO_V2_PLAY_LENGTH_MASK);
    const uint8_t micro = (uint8_t)((packed >> UNDO_V2_PLAY_MICRO_SHIFT)
                                    & UNDO_V2_PLAY_MICRO_MASK);

    if (note < UNDO_V2_PLAY_ABSENT)
    {
        item->note = note;
        item->present_mask |= SEQ_STEP_PLAY_PRESENT_NOTE;
    }
    else if (note != UNDO_V2_PLAY_ABSENT) return 0U;

    if (velocity < UNDO_V2_PLAY_ABSENT)
    {
        item->velocity = velocity;
        item->present_mask |= SEQ_STEP_PLAY_PRESENT_VELOCITY;
    }
    else if (velocity != UNDO_V2_PLAY_ABSENT) return 0U;

    if (length != 0U)
    {
        if (length > 64U) return 0U;
        item->length = length;
        item->present_mask |= SEQ_STEP_PLAY_PRESENT_LENGTH;
    }

    if (micro != 0U)
    {
        if (micro > 49U) return 0U;
        item->microtiming = (int8_t)((int16_t)micro - 25);
        item->present_mask |= SEQ_STEP_PLAY_PRESENT_MICROTIMING;
    }

    if (((packed >> UNDO_V2_PLAY_TERMINAL_SHIFT) & 1UL) != 0U)
        item->present_mask |= SEQ_STEP_PLAY_TERMINAL;
    return 1U;
}

static uint8_t compact_lock_count(const undo_v2_sequence_slot_t *slot,
                                  uint8_t row)
{
    uint8_t count = 0U;
    while ((count < SEQ_STEP_SNAPSHOT_MAX_LOCKS)
            && (slot->locks[row][count].key != UNDO_V2_LOCK_KEY_FREE))
        ++count;
    return count;
}

static uint8_t pack_step(undo_v2_sequence_slot_t *slot, uint8_t row,
                         const seq_step_snapshot_t *snapshot)
{
    if ((slot == NULL) || (snapshot == NULL)
            || (row >= SEQ_STEP_SNAPSHOT_MAX_STEPS)
            || (snapshot->lock_count > SEQ_STEP_SNAPSHOT_MAX_LOCKS)) return 0U;

    slot->state[row] = (uint8_t)((snapshot->trig & 1U)
                                 | ((snapshot->roll & 0x0FU) << 1U));
    for (uint8_t voice = 0U; voice < SEQ_PLAY_MAX_CAPACITY; ++voice)
        slot->play[row][voice] = pack_play_item(&snapshot->play.items[voice]);

    memset(slot->locks[row], 0, sizeof(slot->locks[row]));
    for (uint8_t i = 0U; i < SEQ_STEP_SNAPSHOT_MAX_LOCKS; ++i)
        slot->locks[row][i].key = UNDO_V2_LOCK_KEY_FREE;
    for (uint8_t i = 0U; i < snapshot->lock_count; ++i)
    {
        seq_plock_key_t key = 0U;
        if (seq_param_iface_address_to_key(snapshot->locks[i].set_id,
                snapshot->locks[i].param_slot, &key) == 0U) return 0U;
        slot->locks[row][i].value16 = snapshot->locks[i].value16;
        slot->locks[row][i].key = key;
        slot->locks[row][i].flags = snapshot->locks[i].flags;
    }
    return 1U;
}

static uint8_t unpack_step(const undo_v2_sequence_slot_t *slot, uint8_t row,
                           seq_step_snapshot_t *snapshot)
{
    if ((slot == NULL) || (snapshot == NULL)
            || (row >= SEQ_STEP_SNAPSHOT_MAX_STEPS)) return 0U;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->valid = 1U;
    snapshot->trig = slot->state[row] & 1U;
    snapshot->roll = (slot->state[row] >> 1U) & 0x0FU;
    if ((slot->state[row] & 0xE0U) != 0U) return 0U;

    for (uint8_t voice = 0U; voice < SEQ_PLAY_MAX_CAPACITY; ++voice)
        if (unpack_play_item(slot->play[row][voice],
                            &snapshot->play.items[voice]) == 0U) return 0U;

    snapshot->lock_count = compact_lock_count(slot, row);
    for (uint8_t i = 0U; i < snapshot->lock_count; ++i)
    {
        const undo_v2_compact_lock_t *const lock = &slot->locks[row][i];
        if (seq_param_iface_key_to_address(lock->key,
                &snapshot->locks[i].set_id,
                &snapshot->locks[i].param_slot) == 0U) return 0U;
        snapshot->locks[i].value16 = lock->value16;
        snapshot->locks[i].flags = lock->flags;
    }
    for (uint8_t i = snapshot->lock_count;
         i < SEQ_STEP_SNAPSHOT_MAX_LOCKS; ++i)
        if (slot->locks[row][i].key != UNDO_V2_LOCK_KEY_FREE) return 0U;
    return 1U;
}

static uint8_t step_matches(const undo_v2_sequence_slot_t *slot, uint8_t row,
                            const seq_step_snapshot_t *snapshot)
{
    const uint8_t state = (uint8_t)((snapshot->trig & 1U)
                                    | ((snapshot->roll & 0x0FU) << 1U));
    if ((slot->state[row] != state)
            || (compact_lock_count(slot, row) != snapshot->lock_count)) return 0U;
    for (uint8_t voice = 0U; voice < SEQ_PLAY_MAX_CAPACITY; ++voice)
        if (slot->play[row][voice] != pack_play_item(&snapshot->play.items[voice]))
            return 0U;
    for (uint8_t i = 0U; i < snapshot->lock_count; ++i)
    {
        seq_plock_key_t key = 0U;
        if ((seq_param_iface_address_to_key(snapshot->locks[i].set_id,
                 snapshot->locks[i].param_slot, &key) == 0U)
                || (slot->locks[row][i].key != key)
                || (slot->locks[row][i].value16 != snapshot->locks[i].value16)
                || (slot->locks[row][i].flags != snapshot->locks[i].flags))
            return 0U;
    }
    return 1U;
}

static uint8_t capture_bitmap(seq_track_id_t track, const uint32_t bitmap[2],
                              undo_v2_sequence_slot_t *slot)
{
    if ((slot == NULL) || ((bitmap[0] == 0U) && (bitmap[1] == 0U))) return 0U;
    memset(slot, 0, sizeof(*slot));
    slot->track = track;
    slot->step_bitmap[0] = bitmap[0];
    slot->step_bitmap[1] = bitmap[1];
    uint8_t row = 0U;
    for (seq_step_id_t step = 0U; step < SEQ_STEP_SNAPSHOT_MAX_STEPS; ++step)
    {
        if (bitmap_has(bitmap, step) == 0U) continue;
        seq_step_snapshot_t snapshot;
        if ((seq_step_snapshot_capture(track, step, &snapshot) == 0U)
                || (pack_step(slot, row, &snapshot) == 0U)) return 0U;
        ++row;
    }
    return 1U;
}

static uint8_t capture_steps(seq_track_id_t track, const seq_step_id_t *steps,
                             uint8_t count, undo_v2_sequence_slot_t *slot)
{
    if ((steps == NULL) || (slot == NULL) || (count == 0U)
            || (count > SEQ_STEP_SNAPSHOT_MAX_STEPS)) return 0U;
    uint32_t bitmap[2] = {0U, 0U};
    for (uint8_t i = 0U; i < count; ++i)
    {
        if ((seq_model_is_step_editable_index(steps[i]) == 0U)
                || (bitmap_has(bitmap, steps[i]) != 0U)) return 0U;
        bitmap_add(bitmap, steps[i]);
    }
    return capture_bitmap(track, bitmap, slot);
}

static uint8_t compact_can_apply(const undo_v2_sequence_slot_t *slot)
{
    if ((slot == NULL) || (slot->track >= SEQ_LANE_CAPACITY)
            || ((slot->step_bitmap[0] == 0U) && (slot->step_bitmap[1] == 0U)))
        return 0U;
    uint32_t replaced = 0U;
    uint32_t incoming = 0U;
    uint8_t row = 0U;
    for (seq_step_id_t step = 0U; step < SEQ_STEP_SNAPSHOT_MAX_STEPS; ++step)
    {
        if (bitmap_has(slot->step_bitmap, step) == 0U) continue;
        seq_step_snapshot_t snapshot;
        if ((unpack_step(slot, row, &snapshot) == 0U)
                || (seq_step_snapshot_validate_for_track(slot->track,
                                                         &snapshot) == 0U))
            return 0U;
        replaced += seq_model_step_param_plock_count(slot->track, step);
        incoming += snapshot.lock_count;
        ++row;
    }
    const uint32_t current = seq_model_get_track_plock_count(slot->track);
    return (uint8_t)((current >= replaced)
        && ((current - replaced + incoming)
            <= seq_model_get_track_plock_capacity(slot->track)));
}

static uint8_t compact_apply(const undo_v2_sequence_slot_t *slot)
{
    if (compact_can_apply(slot) == 0U) return 0U;
    for (seq_step_id_t step = 0U; step < SEQ_STEP_SNAPSHOT_MAX_STEPS; ++step)
    {
        if (bitmap_has(slot->step_bitmap, step) == 0U) continue;
        seq_model_step_plock_clear(slot->track, step);
        seq_model_play_clear_step(slot->track, step);
    }
    uint8_t row = 0U;
    for (seq_step_id_t step = 0U; step < SEQ_STEP_SNAPSHOT_MAX_STEPS; ++step)
    {
        if (bitmap_has(slot->step_bitmap, step) == 0U) continue;
        seq_step_snapshot_t snapshot;
        if ((unpack_step(slot, row, &snapshot) == 0U)
                || (seq_step_snapshot_apply(slot->track, step, &snapshot) == 0U))
            return 0U;
        ++row;
    }
    return 1U;
}

static void release_entry(uint8_t descriptor)
{
    if (descriptor == UNDO_V2_HISTORY_AUDIO)
        rec_source_release_history_pair(g_undo_v2.audio.before_generation,
                                        g_undo_v2.audio.after_generation);
}

static void remove_at(uint8_t index)
{
    if (index >= g_undo_v2.runtime.count) return;
    release_entry(g_undo_v2.history[index]);
    if ((uint8_t)(index + 1U) < g_undo_v2.runtime.count)
        memmove(&g_undo_v2.history[index], &g_undo_v2.history[index + 1U],
                (size_t)(g_undo_v2.runtime.count - index - 1U));
    g_undo_v2.runtime.count--;
    g_undo_v2.history[g_undo_v2.runtime.count] = UNDO_V2_HISTORY_FREE;
    if (g_undo_v2.runtime.cursor > index) g_undo_v2.runtime.cursor--;
}

static void purge_redo(void)
{
    while (g_undo_v2.runtime.count > g_undo_v2.runtime.cursor)
        remove_at((uint8_t)(g_undo_v2.runtime.count - 1U));
}

static uint8_t sequence_count(void)
{
    uint8_t count = 0U;
    for (uint8_t i = 0U; i < g_undo_v2.runtime.count; ++i)
        if (g_undo_v2.history[i] < UNDO_V2_SEQUENCE_SLOT_COUNT) count++;
    return count;
}

static uint8_t capture_allowed(void)
{
    return (uint8_t)((g_undo_v2.runtime.capture_suspended == 0U)
        && (g_undo_v2.runtime.apply_in_progress == 0U)
        && (g_undo_v2.runtime.audio_apply_pending == 0U)
        && (__get_IPSR() == 0U));
}

static void clear_pending(void)
{
    g_undo_v2.runtime.pending_slot = UNDO_V2_HISTORY_FREE;
}

static uint8_t pending_change(void)
{
    const uint8_t slot_id = g_undo_v2.runtime.pending_slot;
    if (slot_id >= UNDO_V2_SEQUENCE_SLOT_COUNT) return 2U;
    const undo_v2_sequence_slot_t *const slot = &g_undo_v2.sequence_slots[slot_id];
    uint8_t row = 0U;
    for (seq_step_id_t step = 0U; step < SEQ_STEP_SNAPSHOT_MAX_STEPS; ++step)
    {
        if (bitmap_has(slot->step_bitmap, step) == 0U) continue;
        seq_step_snapshot_t current;
        if (seq_step_snapshot_capture(slot->track, step, &current) == 0U) return 2U;
        if (step_matches(slot, row, &current) == 0U) return 1U;
        ++row;
    }
    return 0U;
}

static undo_v2_status_t exchange_sequence(uint8_t history_index)
{
    const uint8_t old_slot_id = g_undo_v2.history[history_index];
    if (old_slot_id >= UNDO_V2_SEQUENCE_SLOT_COUNT)
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;
    undo_v2_sequence_slot_t *const old_slot =
        &g_undo_v2.sequence_slots[old_slot_id];
    if ((seq_edit_track_sequence_is_locked(old_slot->track) != 0U)
            || (compact_can_apply(old_slot) == 0U))
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;

    const uint8_t new_slot_id = find_free_sequence_slot();
    if (new_slot_id >= UNDO_V2_SEQUENCE_SLOT_COUNT)
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;
    if (capture_bitmap(old_slot->track, old_slot->step_bitmap,
            &g_undo_v2.sequence_slots[new_slot_id]) == 0U)
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;
    if (compact_apply(old_slot) == 0U)
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;
    g_undo_v2.history[history_index] = new_slot_id;
    return UNDO_V2_STATUS_OK;
}

void undo_v2_init(void)
{
    /* SDRAM is NOLOAD: establish a safe count before releasing history. */
    memset(&g_undo_v2.runtime, 0, sizeof(g_undo_v2.runtime));
    undo_v2_clear_all();
}

void undo_v2_clear_all(void)
{
    for (uint8_t i = 0U;
         (i < g_undo_v2.runtime.count) && (i < UNDO_V2_MAX_TRANSACTIONS);
         ++i)
        release_entry(g_undo_v2.history[i]);
    memset(&g_undo_v2, 0, sizeof(g_undo_v2));
    memset(g_undo_v2.history, UNDO_V2_HISTORY_FREE,
           sizeof(g_undo_v2.history));
    clear_pending();
    set_status(UNDO_V2_STATUS_OK);
}

void undo_v2_invalidate_history(void)
{
    const uint8_t suspended = g_undo_v2.runtime.capture_suspended;
    undo_v2_clear_all();
    g_undo_v2.runtime.capture_suspended = suspended;
}

void undo_v2_expire_audio(void)
{
    for (uint8_t i = 0U; i < g_undo_v2.runtime.count; ++i)
        if (g_undo_v2.history[i] == UNDO_V2_HISTORY_AUDIO)
        { remove_at(i); break; }
}

undo_v2_status_t undo_v2_begin_sequence_transaction(seq_track_id_t track,
    const seq_step_id_t *steps, uint8_t step_count)
{
    if ((g_undo_v2.runtime.tx_open != 0U) || (capture_allowed() == 0U)
            || (track >= SEQ_LANE_CAPACITY) || (steps == NULL)
            || (step_count == 0U)
            || (step_count > (uint8_t)SEQ_STEP_SNAPSHOT_MAX_STEPS)
            || (seq_edit_track_sequence_is_locked(track) != 0U))
    { set_status(UNDO_V2_STATUS_ERR_INVALID_ARG); return g_undo_v2.runtime.last_status; }
    const uint8_t slot_id = find_free_sequence_slot();
    if ((slot_id >= UNDO_V2_SEQUENCE_SLOT_COUNT)
            || (capture_steps(track, steps, step_count,
                    &g_undo_v2.sequence_slots[slot_id]) == 0U))
    { clear_pending(); set_status(UNDO_V2_STATUS_ERR_CAPTURE_BLOCKED);
      return g_undo_v2.runtime.last_status; }
    g_undo_v2.runtime.pending_slot = slot_id;
    g_undo_v2.runtime.tx_open = 1U;
    set_status(UNDO_V2_STATUS_OK);
    return UNDO_V2_STATUS_OK;
}

undo_v2_status_t undo_v2_commit_sequence_transaction(void)
{
    if (g_undo_v2.runtime.tx_open == 0U)
    { set_status(UNDO_V2_STATUS_ERR_NO_TX); return g_undo_v2.runtime.last_status; }
    const uint8_t changed = pending_change();
    if (changed != 1U)
    {
        undo_v2_cancel_transaction();
        set_status((changed == 0U) ? UNDO_V2_STATUS_OK
                                  : UNDO_V2_STATUS_ERR_APPLY_FAILED);
        return g_undo_v2.runtime.last_status;
    }
    const uint8_t pending_slot = g_undo_v2.runtime.pending_slot;
    purge_redo();
    if (sequence_count() >= UNDO_V2_MAX_SEQUENCE_TRANSACTIONS)
        for (uint8_t i = 0U; i < g_undo_v2.runtime.count; ++i)
            if (g_undo_v2.history[i] < UNDO_V2_SEQUENCE_SLOT_COUNT)
            { remove_at(i); break; }
    if ((pending_slot >= UNDO_V2_SEQUENCE_SLOT_COUNT)
            || (g_undo_v2.runtime.count >= UNDO_V2_MAX_TRANSACTIONS))
    { undo_v2_cancel_transaction(); set_status(UNDO_V2_STATUS_ERR_APPLY_FAILED);
      return g_undo_v2.runtime.last_status; }
    g_undo_v2.history[g_undo_v2.runtime.count++] = pending_slot;
    g_undo_v2.runtime.cursor = g_undo_v2.runtime.count;
    g_undo_v2.runtime.tx_open = 0U;
    clear_pending();
    set_status(UNDO_V2_STATUS_OK);
    return UNDO_V2_STATUS_OK;
}

undo_v2_status_t undo_v2_commit_audio_transition(uint32_t before_generation,
                                                 uint32_t after_generation)
{
    if (undo_v2_audio_transition_can_commit(before_generation,
                                            after_generation) == 0U)
    { set_status(UNDO_V2_STATUS_ERR_INVALID_ARG); return g_undo_v2.runtime.last_status; }
    purge_redo();
    undo_v2_expire_audio();
    if (g_undo_v2.runtime.count >= UNDO_V2_MAX_TRANSACTIONS) remove_at(0U);
    g_undo_v2.audio.before_generation = before_generation;
    g_undo_v2.audio.after_generation = after_generation;
    g_undo_v2.history[g_undo_v2.runtime.count++] = UNDO_V2_HISTORY_AUDIO;
    g_undo_v2.runtime.cursor = g_undo_v2.runtime.count;
    set_status(UNDO_V2_STATUS_OK);
    return UNDO_V2_STATUS_OK;
}

uint8_t undo_v2_audio_transition_can_commit(uint32_t before_generation,
                                            uint32_t after_generation)
{
    return (uint8_t)((g_undo_v2.runtime.tx_open == 0U)
        && (capture_allowed() != 0U)
        && (before_generation != after_generation));
}

void undo_v2_cancel_transaction(void)
{
    g_undo_v2.runtime.tx_open = 0U;
    clear_pending();
    set_status(UNDO_V2_STATUS_OK);
}

static undo_v2_status_t apply_entry(uint8_t history_index, uint8_t redo)
{
    const uint8_t descriptor = g_undo_v2.history[history_index];
    if (descriptor < UNDO_V2_SEQUENCE_SLOT_COUNT)
        return exchange_sequence(history_index);
    if (descriptor == UNDO_V2_HISTORY_AUDIO)
    {
        const uint32_t target = (redo != 0U)
            ? g_undo_v2.audio.after_generation : g_undo_v2.audio.before_generation;
        return (rec_source_switch_current(target) != 0U)
            ? UNDO_V2_STATUS_OK : UNDO_V2_STATUS_ERR_APPLY_FAILED;
    }
    return UNDO_V2_STATUS_ERR_UNSUPPORTED;
}

undo_v2_status_t undo_v2_undo(void)
{
    if ((g_undo_v2.runtime.tx_open != 0U)
            || (g_undo_v2.runtime.audio_apply_pending != 0U)
            || (g_undo_v2.runtime.cursor == 0U))
    { set_status(UNDO_V2_STATUS_ERR_NO_TX); return g_undo_v2.runtime.last_status; }
    const uint8_t index = (uint8_t)(g_undo_v2.runtime.cursor - 1U);
    g_undo_v2.runtime.apply_in_progress = 1U;
    const undo_v2_status_t status = apply_entry(index, 0U);
    g_undo_v2.runtime.apply_in_progress = 0U;
    if ((status == UNDO_V2_STATUS_ERR_APPLY_FAILED)
            && (g_undo_v2.history[index] == UNDO_V2_HISTORY_AUDIO))
    {
        g_undo_v2.runtime.audio_apply_pending = 1U;
        g_undo_v2.runtime.audio_apply_redo = 0U;
        set_status(UNDO_V2_STATUS_OK);
        return UNDO_V2_STATUS_OK;
    }
    if (status == UNDO_V2_STATUS_OK) g_undo_v2.runtime.cursor--;
    set_status(status);
    return status;
}

undo_v2_status_t undo_v2_redo(void)
{
    if ((g_undo_v2.runtime.tx_open != 0U)
            || (g_undo_v2.runtime.audio_apply_pending != 0U)
            || (g_undo_v2.runtime.cursor >= g_undo_v2.runtime.count))
    { set_status(UNDO_V2_STATUS_ERR_NO_TX); return g_undo_v2.runtime.last_status; }
    const uint8_t index = g_undo_v2.runtime.cursor;
    g_undo_v2.runtime.apply_in_progress = 1U;
    const undo_v2_status_t status = apply_entry(index, 1U);
    g_undo_v2.runtime.apply_in_progress = 0U;
    if ((status == UNDO_V2_STATUS_ERR_APPLY_FAILED)
            && (g_undo_v2.history[index] == UNDO_V2_HISTORY_AUDIO))
    {
        g_undo_v2.runtime.audio_apply_pending = 1U;
        g_undo_v2.runtime.audio_apply_redo = 1U;
        set_status(UNDO_V2_STATUS_OK);
        return UNDO_V2_STATUS_OK;
    }
    if (status == UNDO_V2_STATUS_OK) g_undo_v2.runtime.cursor++;
    set_status(status);
    return status;
}

void undo_v2_set_capture_suspended(uint8_t suspended)
{
    g_undo_v2.runtime.capture_suspended = (suspended != 0U) ? 1U : 0U;
    set_status(UNDO_V2_STATUS_OK);
}

void undo_v2_service(void)
{
    if (g_undo_v2.runtime.audio_apply_pending == 0U) return;
    const uint8_t redo = g_undo_v2.runtime.audio_apply_redo;
    const uint8_t index = (redo != 0U) ? g_undo_v2.runtime.cursor
                                      : (uint8_t)(g_undo_v2.runtime.cursor - 1U);
    if ((index >= g_undo_v2.runtime.count)
            || (g_undo_v2.history[index] != UNDO_V2_HISTORY_AUDIO))
    {
        g_undo_v2.runtime.audio_apply_pending = 0U;
        set_status(UNDO_V2_STATUS_ERR_APPLY_FAILED);
        return;
    }
    g_undo_v2.runtime.apply_in_progress = 1U;
    const undo_v2_status_t status = apply_entry(index, redo);
    g_undo_v2.runtime.apply_in_progress = 0U;
    if (status != UNDO_V2_STATUS_OK) return;
    if (redo != 0U) g_undo_v2.runtime.cursor++;
    else g_undo_v2.runtime.cursor--;
    g_undo_v2.runtime.audio_apply_pending = 0U;
    set_status(UNDO_V2_STATUS_OK);
}
