#include <stdio.h>
#include <string.h>

#include "Core/engine_tasklet.h"
#include "Core/track_topology.h"
#include "Seq/seq_clipboard.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_step_snapshot.h"
#include "Storage/undo_v2.h"

volatile uint32_t engine_tick_count;

static int g_failures;
static seq_track_id_t g_locked_track = 0xFFU;

#define EXPECT(condition, message) \
    do \
    { \
        if (!(condition)) \
        { \
            fprintf(stderr, "FAIL: %s\\n", (message)); \
            ++g_failures; \
        } \
    } while (0)

static uint8_t test_track_is_valid(seq_track_id_t track)
{
    return track_topology_is_active(track);
}

static void test_init_model(void)
{
    seq_model_init_defaults();
    seq_clipboard_init();
    undo_v2_clear_all();
    g_locked_track = 0xFFU;
    engine_tick_count++;
}

static void test_fill_play_step(seq_track_id_t track, seq_step_id_t step, uint8_t seed)
{
    seq_model_set_trig(track, step, 1U);
    seq_model_set_step_roll(track, step, (uint8_t)SEQ_STEP_ROLL_1_32);
    for (uint8_t i = 0U; i < SEQ_PLAY_STEP_MAX_LOCKS; ++i)
    {
        const uint8_t set_id = (i < SEQ_PARAM_ENV_SLOT_COUNT) ? (uint8_t)SEQ_PLOCK_SET_ENV
                                                               : (uint8_t)SEQ_PLOCK_SET_TONE;
        const uint8_t slot = (i < SEQ_PARAM_ENV_SLOT_COUNT)
            ? i : (uint8_t)(i - SEQ_PARAM_ENV_SLOT_COUNT);
        EXPECT(seq_model_step_plock_upsert(track, step, set_id, slot,
                                           (seq_value16_t)(0x1000U + seed + i),
                                           (uint8_t)(0x80U | i)) == SEQ_PLOCK_OP_CREATED,
               "maximum Play p-lock set must be accepted");
    }
}

static void test_fill_play_step_light(seq_track_id_t track, seq_step_id_t step, uint8_t seed)
{
    seq_model_set_trig(track, step, 1U);
    seq_model_set_step_roll(track, step, (uint8_t)SEQ_STEP_ROLL_1_24);
    EXPECT(seq_model_step_plock_upsert(track, step, (uint8_t)SEQ_PLOCK_SET_PLAY,
                                       (seq_param_slot_t)(seed % SEQ_PARAM_PLAY_SLOT_COUNT),
                                       (seq_value16_t)(0x2200U + seed),
                                       (uint8_t)(0x40U | seed)) == SEQ_PLOCK_OP_CREATED,
           "Play step p-lock is accepted");
}

static void test_step_snapshot_codec(void)
{
    const seq_track_id_t special_track = TRACK_TOPOLOGY_MASTER_TRACK_INDEX;
    const seq_step_id_t page_steps[SEQ_STEPS_PER_PAGE] = {
        0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
        8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U
    };
    seq_step_snapshot_t play_snapshot;
    seq_step_snapshot_t restored_snapshot;
    seq_step_snapshot_t special_snapshot;
    seq_step_snapshot_list_t page_snapshot;

    test_init_model();
    test_fill_play_step(0U, 3U, 7U);
    EXPECT(seq_step_snapshot_capture(0U, 3U, &play_snapshot) != 0U,
           "Play snapshot captures the complete maximum step");
    seq_model_step_plock_clear(0U, 3U);
    seq_model_set_trig(0U, 3U, 0U);
    EXPECT(seq_step_snapshot_apply(0U, 3U, &play_snapshot) != 0U,
           "Play snapshot applies atomically");
    EXPECT(seq_step_snapshot_capture(0U, 3U, &restored_snapshot) != 0U,
           "restored Play step can be recaptured");
    EXPECT(seq_step_snapshot_equal(&play_snapshot, &restored_snapshot) != 0U,
           "Play snapshot round-trip preserves every field and p-lock flag");

    seq_model_set_special_action(special_track, 2U, (uint8_t)SEQ_SPECIAL_ACTION_TRIGGER);
    EXPECT(seq_model_step_plock_upsert(special_track, 2U,
                                       (uint8_t)SEQ_PLOCK_SET_ENV, 0U,
                                       0x3456U, 0xA5U) == SEQ_PLOCK_OP_CREATED,
           "Special snapshot source accepts its p-lock");
    EXPECT(seq_step_snapshot_capture(special_track, 2U, &special_snapshot) != 0U,
           "Special snapshot captures without Play payload");
    EXPECT(special_snapshot.role == (uint8_t)SEQ_STEP_SNAPSHOT_ROLE_SPECIAL,
           "Special snapshot carries its role");
    EXPECT(special_snapshot.trig == 0U && special_snapshot.roll == 0U,
           "Special snapshot contains no Play trig or roll");
    EXPECT(seq_step_snapshot_validate_for_track(special_track, &special_snapshot) != 0U,
           "Special snapshot validates on a Special track");
    special_snapshot.locks[0].set_id = (uint8_t)SEQ_PLOCK_SET_PLAY;
    EXPECT(seq_step_snapshot_validate_for_track(special_track, &special_snapshot) == 0U,
           "Special snapshot rejects Play p-locks");

    test_init_model();
    for (seq_step_id_t step = 0U; step < SEQ_STEPS_PER_PAGE; ++step)
    {
        test_fill_play_step_light(0U, step, step);
    }
    EXPECT(seq_step_snapshot_capture_list(0U, page_steps, SEQ_STEPS_PER_PAGE,
                                          &page_snapshot) != 0U,
           "snapshot codec captures an explicit page list");
    for (seq_step_id_t step = 0U; step < SEQ_STEPS_PER_PAGE; ++step)
    {
        seq_model_step_plock_clear(0U, step);
        seq_model_set_trig(0U, step, 0U);
    }
    EXPECT(seq_step_snapshot_apply_list(0U, &page_snapshot) != 0U,
           "snapshot codec restores an explicit page atomically");
    EXPECT(seq_model_step_plock_count(0U, SEQ_STEPS_PER_PAGE - 1U) == 1U,
           "page restore preserves its final step");

    test_init_model();
    for (seq_step_id_t step = 0U; step < SEQ_PLAY_PLOCK_POOL_CAP_PER_TRACK / SEQ_PLAY_STEP_MAX_LOCKS; ++step)
    {
        test_fill_play_step(0U, step, step);
    }
    EXPECT(seq_step_snapshot_capture(0U, 0U, &play_snapshot) != 0U,
           "full-pool source step captures before capacity failure");
    EXPECT(seq_step_snapshot_apply(0U, SEQ_PLAY_PLOCK_POOL_CAP_PER_TRACK / SEQ_PLAY_STEP_MAX_LOCKS,
                                  &play_snapshot) == 0U,
           "codec rejects restore when the p-lock pool has no capacity");
    EXPECT(seq_model_step_is_empty(0U, SEQ_PLAY_PLOCK_POOL_CAP_PER_TRACK / SEQ_PLAY_STEP_MAX_LOCKS) != 0U,
           "capacity rejection leaves the target step unchanged");
}

static void test_begin_capture_commit(seq_track_id_t track,
                                      const seq_step_id_t *steps,
                                      uint8_t step_count)
{
    EXPECT(undo_v2_begin_sequence_transaction(track,
                                               steps,
                                               step_count) == UNDO_V2_STATUS_OK,
           "sequence transaction begins and captures the before image");
}

static void test_finish_capture_commit(void)
{
    EXPECT(undo_v2_commit_sequence_transaction() == UNDO_V2_STATUS_OK,
           "sequence transaction commits only on effective change");
}

static void test_play_step_round_trip(void)
{
    test_init_model();
    const seq_step_id_t step = 3U;
    test_begin_capture_commit(0U, &step, 1U);
    test_fill_play_step(0U, 3U, 7U);
    test_finish_capture_commit();
    EXPECT(undo_v2_undo() == UNDO_V2_STATUS_OK, "Play step undo succeeds");
    EXPECT(seq_model_step_is_empty(0U, 3U) != 0U, "undo restores empty Play step");
    EXPECT(undo_v2_redo() == UNDO_V2_STATUS_OK, "Play step redo succeeds");
    EXPECT(seq_model_get_trig(0U, 3U) != 0U, "redo restores Play trig");
    EXPECT(seq_model_step_plock_count(0U, 3U) == SEQ_PLAY_STEP_MAX_LOCKS,
           "redo restores all Play p-locks");
}

static void test_special_round_trip(void)
{
    const seq_track_id_t track = TRACK_TOPOLOGY_MASTER_TRACK_INDEX;
    test_init_model();
    const seq_step_id_t step = 2U;
    test_begin_capture_commit(track, &step, 1U);
    seq_model_set_special_action(track, 2U, (uint8_t)SEQ_SPECIAL_ACTION_TRIGGER);
    EXPECT(seq_model_step_plock_upsert(track, 2U, (uint8_t)SEQ_PLOCK_SET_ENV,
                                       0U, 0x3456U, 0xA5U) == SEQ_PLOCK_OP_CREATED,
           "Special p-lock is accepted");
    EXPECT(seq_model_step_plock_upsert(track, 2U, (uint8_t)SEQ_PLOCK_SET_PLAY,
                                       0U, 0xFFFFU, 0U) == SEQ_PLOCK_OP_SET_NOT_PLOCKABLE,
           "Special rejects PLAY p-locks");
    test_finish_capture_commit();
    EXPECT(undo_v2_undo() == UNDO_V2_STATUS_OK, "Special undo succeeds");
    EXPECT(seq_model_get_special_action(track, 2U) == SEQ_SPECIAL_ACTION_NONE,
           "Special undo restores empty action");
    EXPECT(undo_v2_redo() == UNDO_V2_STATUS_OK, "Special redo succeeds");
    EXPECT(seq_model_get_special_action(track, 2U) == SEQ_SPECIAL_ACTION_TRIGGER,
           "Special redo restores action");
    EXPECT(seq_model_step_plock_count(track, 2U) == 1U,
           "Special redo restores only Special p-locks");
}

static void test_copy_paste_scope(seq_step_id_t count)
{
    seq_step_id_t steps[SEQ_MAX_STEPS];
    for (seq_step_id_t i = 0U; i < count; ++i)
    {
        steps[i] = i;
    }

    test_init_model();
    for (seq_step_id_t i = 0U; i < count; ++i)
    {
        test_fill_play_step_light(0U, i, (uint8_t)i);
    }
    EXPECT(seq_clipboard_copy(0U, steps, count) != 0U, "Copy succeeds");
    EXPECT(undo_v2_undo() == UNDO_V2_STATUS_ERR_NO_TX, "Copy alone creates no Undo");
    for (seq_step_id_t i = 0U; i < count; ++i)
    {
        seq_model_step_plock_clear(0U, i);
        seq_model_set_trig(0U, i, 0U);
    }
    test_begin_capture_commit(0U, steps, count);
    seq_clipboard_paste_result_t result;
    EXPECT(seq_clipboard_paste(0U, steps, count, &result) != 0U,
           "Paste succeeds");
    test_finish_capture_commit();
    EXPECT(undo_v2_undo() == UNDO_V2_STATUS_OK, "Paste undo succeeds");
    EXPECT(undo_v2_redo() == UNDO_V2_STATUS_OK, "Paste redo succeeds");
    EXPECT(seq_model_step_plock_count(0U, (seq_step_id_t)(count - 1U)) == 1U,
           "Paste redo restores complete scope");
}

static void test_depth_and_branching(void)
{
    test_init_model();
    for (uint8_t i = 0U; i < 8U; ++i)
    {
        const seq_step_id_t step = i;
        test_begin_capture_commit(0U, &step, 1U);
        seq_model_set_trig(0U, i, 1U);
        test_finish_capture_commit();
    }
    for (uint8_t i = 0U; i < 8U; ++i)
    {
        EXPECT(undo_v2_undo() == UNDO_V2_STATUS_OK, "successive Undo succeeds");
    }
    EXPECT(undo_v2_undo() == UNDO_V2_STATUS_ERR_NO_TX, "exactly eight Undo levels are exposed");
    for (uint8_t i = 0U; i < 8U; ++i)
    {
        EXPECT(undo_v2_redo() == UNDO_V2_STATUS_OK, "successive Redo succeeds");
    }
    EXPECT(undo_v2_redo() == UNDO_V2_STATUS_ERR_NO_TX, "exactly eight Redo levels are exposed");

    test_init_model();
    const seq_step_id_t branch_step = 0U;
    test_begin_capture_commit(0U, &branch_step, 1U);
    seq_model_set_trig(0U, 0U, 1U);
    test_finish_capture_commit();
    EXPECT(undo_v2_undo() == UNDO_V2_STATUS_OK, "branch setup Undo succeeds");
    seq_model_step_plock_upsert(0U, 0U, (uint8_t)SEQ_PLOCK_SET_PLAY, 0U, 0x7777U, 1U);
    EXPECT(undo_v2_redo() == UNDO_V2_STATUS_OK,
           "non-undoable step content edit preserves Redo");
    EXPECT(undo_v2_undo() == UNDO_V2_STATUS_OK,
           "restoring the undone structural state succeeds");
    const seq_step_id_t next_branch_step = 1U;
    test_begin_capture_commit(0U, &next_branch_step, 1U);
    seq_model_set_trig(0U, 1U, 1U);
    test_finish_capture_commit();
    EXPECT(undo_v2_redo() == UNDO_V2_STATUS_ERR_NO_TX,
           "new structural action invalidates Redo branch");
}

static void test_pool_near_capacity(void)
{
    const seq_step_id_t filled_steps =
        (seq_step_id_t)(SEQ_PLAY_PLOCK_POOL_CAP_PER_TRACK / SEQ_PLAY_STEP_MAX_LOCKS);
    test_init_model();
    for (seq_step_id_t step = 0U; step < filled_steps; ++step)
    {
        test_fill_play_step(0U, step, step);
    }
    EXPECT(seq_model_step_plock_count(0U, (seq_step_id_t)(filled_steps - 1U))
               == SEQ_PLAY_STEP_MAX_LOCKS,
           "Play p-lock pool reaches its bounded capacity");
    seq_step_id_t steps[SEQ_MAX_STEPS];
    for (seq_step_id_t step = 0U; step < filled_steps; ++step)
    {
        steps[step] = step;
    }
    test_begin_capture_commit(0U, steps, filled_steps);
    for (seq_step_id_t step = 0U; step < filled_steps; ++step)
    {
        seq_model_step_plock_clear(0U, step);
        seq_model_set_trig(0U, step, 0U);
    }
    test_finish_capture_commit();
    EXPECT(undo_v2_undo() == UNDO_V2_STATUS_OK,
           "restore succeeds with a nearly full Play p-lock pool");
    EXPECT(seq_model_step_plock_count(0U, (seq_step_id_t)(filled_steps - 1U))
               == SEQ_PLAY_STEP_MAX_LOCKS,
           "near-capacity restore is complete");
}

static void test_noop_and_atomic_failure(void)
{
    test_init_model();
    const seq_step_id_t noop_step = 0U;
    test_begin_capture_commit(0U, &noop_step, 1U);
    test_finish_capture_commit();
    EXPECT(undo_v2_undo() == UNDO_V2_STATUS_ERR_NO_TX,
           "an action without an effective mutation is not recorded");

    test_init_model();
    test_fill_play_step(0U, 0U, 1U);
    const seq_step_id_t failure_step = 0U;
    test_begin_capture_commit(0U, &failure_step, 1U);
    seq_model_step_plock_clear(0U, 0U);
    test_finish_capture_commit();
    seq_model_set_track_length(0U, 1U);
    g_locked_track = 0U;
    EXPECT(undo_v2_undo() == UNDO_V2_STATUS_ERR_APPLY_FAILED,
           "locked-track restore fails atomically");
    EXPECT(seq_model_get_track_length(0U) == 1U,
           "failed restore leaves unrelated current state unchanged");
}

int main(void)
{
    undo_v2_init();
    test_play_step_round_trip();
    test_special_round_trip();
    test_step_snapshot_codec();
    test_copy_paste_scope(1U);
    test_copy_paste_scope(SEQ_STEPS_PER_PAGE);
    test_copy_paste_scope(SEQ_MAX_STEPS);
    test_pool_near_capacity();
    test_depth_and_branching();
    test_noop_and_atomic_failure();
    if (g_failures != 0)
    {
        fprintf(stderr, "undo_v2_functional_test: %d failure(s)\\n", g_failures);
        return 1;
    }
    puts("undo_v2_functional_test=PASS");
    return 0;
}

uint8_t seq_edit_track_sequence_is_locked(seq_track_id_t track)
{
    return (track == g_locked_track) ? 1U : 0U;
}

uint8_t seq_runtime_get_track_div(seq_track_id_t track, uint8_t *out_div)
{
    (void)track;
    if (out_div != NULL) *out_div = 1U;
    return 1U;
}

uint8_t seq_runtime_get_track_quant(seq_track_id_t track, uint8_t *out_quant)
{
    (void)track;
    if (out_quant != NULL) *out_quant = 0U;
    return 1U;
}

uint8_t seq_runtime_get_track_swing(seq_track_id_t track, uint8_t *out_swing)
{
    (void)track;
    if (out_swing != NULL) *out_swing = 0U;
    return 1U;
}

void seq_runtime_set_track_div(seq_track_id_t track, uint8_t div)
{
    (void)track;
    (void)div;
}

void seq_runtime_set_track_quant(seq_track_id_t track, uint8_t quant)
{
    (void)track;
    (void)quant;
}

void seq_runtime_set_track_swing(seq_track_id_t track, uint8_t swing)
{
    (void)track;
    (void)swing;
}

void seq_runtime_on_track_length_changed(seq_track_id_t track)
{
    (void)track;
}

uint8_t seq_param_iface_is_set_plockable(uint8_t set_id)
{
    return (set_id < (uint8_t)SEQ_PLOCK_SET_COUNT) ? 1U : 0U;
}

uint8_t seq_param_iface_set_to_mask(uint8_t set_id)
{
    return (set_id < 8U) ? (uint8_t)(1U << set_id) : 0U;
}

uint8_t seq_param_iface_slot_is_supported(seq_track_id_t track,
                                          uint8_t set_id,
                                          seq_param_slot_t param_slot)
{
    if ((test_track_is_valid(track) == 0U)
        || (seq_param_iface_is_set_plockable(set_id) == 0U)
        || ((track_topology_is_play(track) == 0U) && (set_id == SEQ_PLOCK_SET_PLAY)))
    {
        return 0U;
    }
    return (param_slot < 32U) ? 1U : 0U;
}

uint8_t seq_param_iface_is_param_supported(seq_track_id_t track,
                                           uint8_t set_id,
                                           seq_param_slot_t param_slot)
{
    return seq_param_iface_slot_is_supported(track, set_id, param_slot);
}

void track_runtime_refresh_track(uint8_t track)
{
    (void)track;
}

float param_get(param_id_t id)
{
    (void)id;
    return 0.0f;
}

void param_set(param_id_t id, float value)
{
    (void)id;
    (void)value;
}

uint8_t param_registry_apply_track_value(param_id_t id, uint8_t track, float value)
{
    (void)id;
    (void)track;
    (void)value;
    return 1U;
}
