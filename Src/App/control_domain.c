#include "App/control_domain.h"

#include "App/brick6_boot_defaults.h"
#include "App/brick6_boot_fx_policy.h"
#include "App/engine_tasklet.h"
#include "App/Hall/hall_calibration.h"
#include "App/Hall/hall_keyboard_bridge.h"
#include "App/Hall/hall_loop.h"
#include "Board/board_usb.h"
#include "midi.h"
#include "Param/param_registry.h"
#include "Sampler/multi_sample_loader.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Seq/seq_runtime.h"
#include "Storage/audio_recorder.h"
#include "Storage/brick6_stream_service_task.h"
#include "Storage/patch_product.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/project_control.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/project_product.h"
#include "Storage/sd_access_gate.h"
#include "Storage/sd_preview.h"
#include "Storage/undo_v2.h"
#include "Storage/wav_convert.h"
#include "Storage/wav_loader.h"
#include "Storage/waveform_cache.h"
#include "Track/track_state.h"
#include "UI/ui_active_track_sync.h"
#include "ControlRT/control_rt_publication.h"
#include "IPC/live_clock_control.h"
#include "ui_boot_loading.h"
#include "ui_core.h"
#include "ui_page_manager.h"

void control_domain_init(void)
{
    control_rt_publication_init();
    live_clock_control_init();
    project_load_quiesce_init();
    board_usb_device_init();
    brick6_boot_fx_policy_init();

    sd_access_gate_init();
    wav_convert_init();
    waveform_cache_init();
    (void)waveform_cache_ensure_dirs();
    wav_loader_catalog_init_load();
    sd_preview_init();
    sample_page_cache_init();
    sample_global_pool_init();
    sampler_ram_pool_init();
    wavetable_pool_init();
    multi_sample_pool_init();
    multi_sample_loader_init();
    sample_cache_init();
    audio_recorder_init();
}

void control_domain_start(float postgain, float output_compensation)
{
    engine_tasklet_init(48000U);
    param_registry_init();
    track_state_init();
    seq_runtime_init();
    ui_core_init();
    (void)param_registry_commit_global(PARAM_POST_GAIN, postgain);
    (void)param_registry_commit_global(PARAM_OUTPUT_COMP, output_compensation);
    brick6_boot_apply_param_defaults();
    project_control_init();
    pattern_live_init();
    patch_product_init();
    project_product_init();
    ui_boot_loading_begin();
    undo_v2_init();
    hall_loop_init();
    hall_keyboard_bridge_init();
    if (hall_calibration_load() != 0U)
    {
        ui_page_set(UI_PAGE_TEMPLATE_CFG);
    }
    else
    {
        ui_page_set(UI_PAGE_CALIBRATION);
    }
    ui_active_track_sync_full_after_global_restore();
    brick6_stream_service_task_init();
    midi_init();
}
