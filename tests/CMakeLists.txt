cmake_minimum_required(VERSION 3.20)
project(brick6_filter_tests C CXX)

enable_testing()

add_executable(filter_tpt_modulation_test
    filter_tpt_modulation_main.c
    filter_tpt_modulation_test.c
    ../Src/Audio/fx_biquad_filter.c
)

target_include_directories(filter_tpt_modulation_test PRIVATE ../Inc ../Inc/Audio)
target_link_libraries(filter_tpt_modulation_test PRIVATE m)

add_test(NAME filter_tpt_modulation COMMAND filter_tpt_modulation_test)

add_executable(reverb_mutable_stability_test
    reverb_mutable_stability_test.cpp
)
target_include_directories(reverb_mutable_stability_test PRIVATE
    ../Inc
    ../mutable_instruments
    ../mutable_instruments/stmlib
)
target_link_libraries(reverb_mutable_stability_test PRIVATE m)
add_test(NAME reverb_mutable_stability COMMAND reverb_mutable_stability_test)

add_executable(common_modulation_test
    common_modulation_main.c
    common_modulation_test.c
)

target_link_libraries(common_modulation_test PRIVATE m)
add_test(NAME common_modulation COMMAND common_modulation_test)

add_executable(note_fx_arp_test note_fx_arp_test.c ../Src/NoteFx/note_fx_arp.c)
target_include_directories(note_fx_arp_test PRIVATE ../Inc)
add_test(NAME note_fx_arp COMMAND note_fx_arp_test)

add_executable(note_fx_runtime_test
    note_fx_runtime_test.c
    ../Src/NoteFx/note_fx_arp.c
    ../Src/NoteFx/note_fx_engine.c
    ../Src/NoteFx/note_fx_pipeline.c
    ../Src/NoteFx/note_fx_state.c
    ../Src/Seq/seq_division_catalog.c
)
target_include_directories(note_fx_runtime_test PRIVATE stubs ../Inc)
target_compile_definitions(note_fx_runtime_test PRIVATE BRICK6_VARIANT_LOWCOST=1)
add_test(NAME note_fx_runtime COMMAND note_fx_runtime_test)

add_executable(hall_engine_lowcost_test
    hall_engine_lowcost_test.c
    ../Src/App/Hall/hall_engine.c
)

target_compile_definitions(hall_engine_lowcost_test PRIVATE BRICK6_VARIANT_LOWCOST=1)
target_include_directories(hall_engine_lowcost_test PRIVATE
    stubs
    ../Inc
)
add_test(NAME hall_engine_lowcost COMMAND hall_engine_lowcost_test)

add_executable(hall_engine_premium_test
    hall_engine_lowcost_test.c
    ../Src/App/Hall/hall_engine.c
)

target_include_directories(hall_engine_premium_test PRIVATE
    stubs
    ../Inc
)
add_test(NAME hall_engine_premium COMMAND hall_engine_premium_test)

add_executable(stack_vca_release_test
    stack_vca_release_test.c
    ../Src/Core/brick6_stack_runtime.c
    ../Src/Core/brick6_stack_waveform.c
)

target_include_directories(stack_vca_release_test PRIVATE
    stubs
    ../Inc
)
target_link_libraries(stack_vca_release_test PRIVATE m)
add_test(NAME stack_vca_release COMMAND stack_vca_release_test)

add_executable(synth_polyphony_budget_test
    synth_polyphony_budget_test.c
    ../Src/Core/synth_polyphony.c
)
target_include_directories(synth_polyphony_budget_test PRIVATE
    stubs
    ../Inc
)
if(CMAKE_CROSSCOMPILING)
    target_link_options(synth_polyphony_budget_test PRIVATE --specs=nosys.specs)
endif()
add_test(NAME synth_polyphony_budget COMMAND synth_polyphony_budget_test)

add_executable(cfg_track_family_order_test
    cfg_track_family_order_test.c
    ../Src/UI/ui_track_catalog.c
    ../Src/Core/track_topology.c
)
target_compile_definitions(cfg_track_family_order_test PRIVATE BRICK6_VARIANT_LOWCOST=1)
target_include_directories(cfg_track_family_order_test PRIVATE ../Inc)
add_test(NAME cfg_track_family_order_unit COMMAND cfg_track_family_order_test)

if(WIN32)
    add_test(NAME cfg_track_family_order_static
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/cfg_track_family_order_validation.ps1)
    add_test(NAME led_track_select_static
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/led_track_select_validation.ps1)
    add_test(NAME engine_output_gain
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/engine_output_gain_validation.ps1)
    add_test(NAME hall_lowcost_integration
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/hall_lowcost_integration_validation.ps1)
    add_test(NAME wave_mipmap_spectral
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/wave_mipmap_spectral_test.ps1)
    add_test(NAME track_paste_playback
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/track_paste_playback_validation.ps1)
    add_test(NAME note_fx_pipeline
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/note_fx_pipeline_validation.ps1)
    add_test(NAME note_fx_plock
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/note_fx_plock_validation.ps1)
    add_test(NAME note_fx_persistence
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/note_fx_persistence_validation.ps1)
    add_test(NAME note_fx_step10_consolidation
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/note_fx_step10_consolidation_validation.ps1)
    add_test(NAME env_ownership
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/env_ownership_validation.ps1)
    add_test(NAME seq_param_compact_contract
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/seq_param_compact_contract_validation.ps1)
    add_test(NAME vca_dispatcher
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/vca_dispatcher_validation.ps1)
    add_test(NAME stream_sampler_vca_lifecycle
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/stream_sampler_vca_lifecycle_validation.ps1)
    add_test(NAME multi_sampler_vca_lifecycle
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/multi_sampler_vca_lifecycle_validation.ps1)
    add_test(NAME multi_polyphony_exhaustive
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/multi_polyphony_exhaustive_validation.ps1)
    add_test(NAME pattern_persistence_classification
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/pattern_persistence_classification_validation.ps1)
    add_test(NAME reverb_mutable_only
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/reverb_mutable_only_validation.ps1)
    add_test(NAME spectral_window
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/spectral_window_validation.ps1)
    add_test(NAME seq_play_scheduler_pair
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/seq_play_scheduler_pair_validation.ps1)
    add_test(NAME sample_stream_coalesced_lifecycle
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/sample_stream_coalesced_lifecycle_validation.ps1)
    add_test(NAME sample_stream_deadline
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/sample_stream_deadline_validation.ps1)
    add_test(NAME sd_stream_browser_lock
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/sd_stream_browser_lock_validation.ps1)
    add_test(NAME synth_polyphony_boundary
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/synth_polyphony_boundary_validation.ps1)
endif()
