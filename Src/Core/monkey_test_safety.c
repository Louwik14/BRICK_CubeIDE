#include "Core/monkey_test_safety.h"

#include <string.h>

#include "Keyboard/keyboard_engine.h"
#include "Keyboard/keyboard_runtime.h"
#include "Seq/seq_output_guard.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Storage/memory_layout.h"
#include "Storage/audio_recorder.h"
#include "Storage/project_v1.h"
#include "Storage/sd_access_gate.h"
#include "Storage/undo_v2.h"
#include "Storage/wav_convert.h"
#include "Param/param_registry.h"
#include "UI/ui_core.h"

#define MONKEY_TEST_SAFE_MASTER_GAIN 0.25f

typedef struct
{
    uint8_t active;
    uint8_t snapshot_valid;
    uint8_t saved_active_track;
    ui_hall_mode_t saved_hall_mode;
    uint8_t restore_transport;
    uint8_t saved_playhead[SEQ_TRACK_COUNT];
    float saved_master_gain;
} monkey_test_safety_runtime_t;

static monkey_test_safety_runtime_t g_monkey_test_safety;
STORAGE_STATE_SDRAM static ProjectSaveV1 g_monkey_test_saved_project;

static void monkey_test_safety_silence(void)
{
    seq_runtime_stop();
    seq_output_guard_panic(1U);
    keyboard_runtime_all_notes_off();
    keyboard_engine_all_notes_off();
}

void monkey_test_safety_init(void)
{
    memset(&g_monkey_test_safety, 0, sizeof(g_monkey_test_safety));
    sd_access_gate_set_diagnostic_read_only(0U);
}

uint8_t monkey_test_safety_prepare(void)
{
    if (g_monkey_test_safety.active != 0U)
    {
        return 0U;
    }
    if ((sd_access_gate_current_owner() != SD_ACCESS_CLIENT_NONE)
        || (audio_recorder_is_active() != 0U)
        || (wav_convert_is_active() != 0U))
    {
        return 0U;
    }

    memset(&g_monkey_test_safety, 0, sizeof(g_monkey_test_safety));
    g_monkey_test_safety.saved_active_track = ui_get_active_track();
    g_monkey_test_safety.saved_hall_mode = ui_get_hall_mode();
    g_monkey_test_safety.restore_transport = seq_runtime_is_running();
    g_monkey_test_safety.saved_master_gain = param_get(PARAM_MASTER_GAIN);
    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; track++)
    {
        (void)seq_runtime_get_playhead_step(
            track, &g_monkey_test_safety.saved_playhead[track]);
    }

    if (project_v1_capture_current(&g_monkey_test_saved_project) == 0U)
    {
        return 0U;
    }
    g_monkey_test_safety.snapshot_valid = 1U;

    monkey_test_safety_silence();
    undo_v2_set_capture_suspended(1U);
    sd_access_gate_set_diagnostic_read_only(1U);
    if (pattern_live_apply_boot_snapshot(0U) == 0U)
    {
        sd_access_gate_set_diagnostic_read_only(0U);
        undo_v2_set_capture_suspended(0U);
        (void)project_v1_apply_snapshot(&g_monkey_test_saved_project, 0U);
        g_monkey_test_safety.snapshot_valid = 0U;
        return 0U;
    }

    const float safe_gain =
        (g_monkey_test_safety.saved_master_gain < MONKEY_TEST_SAFE_MASTER_GAIN)
            ? g_monkey_test_safety.saved_master_gain
            : MONKEY_TEST_SAFE_MASTER_GAIN;
    param_set(PARAM_MASTER_GAIN, safe_gain);
    g_monkey_test_safety.active = 1U;
    return 1U;
}

uint8_t monkey_test_safety_restore(void)
{
    if (g_monkey_test_safety.active == 0U)
    {
        return (g_monkey_test_safety.snapshot_valid == 0U) ? 1U : 0U;
    }

    monkey_test_safety_silence();
    sd_access_gate_set_diagnostic_read_only(0U);

    uint8_t ok = 1U;
    if ((g_monkey_test_safety.snapshot_valid == 0U)
        || (project_v1_apply_snapshot(&g_monkey_test_saved_project, 0U) == 0U))
    {
        ok = 0U;
    }
    else
    {
        ui_restore_active_track(g_monkey_test_safety.saved_active_track);
        ui_set_hall_mode(g_monkey_test_safety.saved_hall_mode);
        for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; track++)
        {
            (void)seq_runtime_set_playhead_step(
                track, g_monkey_test_safety.saved_playhead[track]);
        }
        if (g_monkey_test_safety.restore_transport != 0U)
        {
            seq_runtime_start();
        }
    }

    param_set(PARAM_MASTER_GAIN, g_monkey_test_safety.saved_master_gain);
    undo_v2_set_capture_suspended(0U);
    g_monkey_test_safety.active = 0U;
    g_monkey_test_safety.snapshot_valid = 0U;
    return ok;
}

uint8_t monkey_test_safety_is_active(void)
{
    return g_monkey_test_safety.active;
}
