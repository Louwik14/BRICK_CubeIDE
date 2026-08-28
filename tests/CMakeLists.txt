cmake_minimum_required(VERSION 3.20)
project(brick6_note_fx_step1_tests C)

enable_testing()

if(NOT CMAKE_CROSSCOMPILING)
    add_executable(restore_plan_contract_test
        restore_plan_contract_test.c
    )
    target_include_directories(restore_plan_contract_test PRIVATE ../Inc)
    add_test(NAME restore_plan_contract COMMAND restore_plan_contract_test)

    add_executable(param_restore_pass1_contract_test
        param_restore_pass1_contract_test.c
        ../Src/Param/param_registry_value_contract.c
        ../Src/Param/param_registry_runtime_state.c
    )
    target_include_directories(param_restore_pass1_contract_test PRIVATE
        ../Inc
        ../Inc/Param
    )
    add_test(NAME param_restore_pass1_contract
             COMMAND param_restore_pass1_contract_test)

    add_executable(param_value_policy_test
        param_value_policy_test.c
        ../Src/Param/param_value_policy.c
    )
    target_include_directories(param_value_policy_test PRIVATE ../Inc)
    if(UNIX)
        target_link_libraries(param_value_policy_test PRIVATE m)
    endif()
    add_test(NAME param_value_policy COMMAND param_value_policy_test)

    add_executable(fx_modfx_daisy_stereo_test
        fx_modfx_daisy_stereo_test.c
        ../Src/Audio/fx_modfx_global.c
    )
    target_compile_definitions(fx_modfx_daisy_stereo_test PRIVATE
        BRICK6_DAISY_STEREO_TEST
    )
    target_include_directories(fx_modfx_daisy_stereo_test PRIVATE ../Inc)
    if(UNIX)
        target_link_libraries(fx_modfx_daisy_stereo_test PRIVATE m)
    endif()
    add_test(NAME fx_modfx_daisy_stereo COMMAND fx_modfx_daisy_stereo_test)

    add_executable(persistent_control_codec_roundtrip_test
        persistent_control_codec_roundtrip_test.c
        ../Src/Core/entity_topology.c
        ../Src/Storage/persistent_control_codec.c
        ../Src/Storage/persistent_entity_topology.c
        ../Src/Storage/persistent_key_catalog.c
    )
    target_include_directories(persistent_control_codec_roundtrip_test PRIVATE ../Inc)
    if(CMAKE_CROSSCOMPILING)
        target_link_options(persistent_control_codec_roundtrip_test PRIVATE --specs=nosys.specs)
    endif()
    if(UNIX)
        target_link_libraries(persistent_control_codec_roundtrip_test PRIVATE m)
    endif()
    add_test(NAME persistent_control_codec_roundtrip
             COMMAND persistent_control_codec_roundtrip_test)

    add_executable(audio_recorder_wav_test
        audio_recorder_wav_test.c
        ../Src/Storage/audio_recorder_wav.c
    )
    target_include_directories(audio_recorder_wav_test PRIVATE ../Inc)
    add_test(NAME audio_recorder_wav COMMAND audio_recorder_wav_test)

    add_executable(generic_recorder_test
        generic_recorder_test.c
        ../Src/Storage/generic_recorder.c
        ../Src/SD/sd_scheduler.c
    )
    target_include_directories(generic_recorder_test PRIVATE
        stubs
        ../Inc
        ../Inc/SD
        ../Inc/Storage
        ../App/Middlewares/Third_Party/FatFs/src
    )
    add_test(NAME generic_recorder COMMAND generic_recorder_test)

    add_executable(sd_scheduler_test
        sd_scheduler_test.c
        ../Src/SD/sd_scheduler.c
    )
    target_include_directories(sd_scheduler_test PRIVATE ../Inc)
    add_test(NAME sd_scheduler COMMAND sd_scheduler_test)

    add_executable(sd_block_device_async_write_test
        sd_block_device_async_write_test.c
        ../Src/SD/sd_block_device.c
    )
    target_include_directories(sd_block_device_async_write_test PRIVATE
        sd_block_device_stubs
        ../Inc
    )
    add_test(NAME sd_block_device_async_write COMMAND sd_block_device_async_write_test)

    add_executable(recorder_file_reservation_test
        recorder_file_reservation_test.c
        ../Src/Storage/recorder_file_reservation.c
        ../Src/Storage/audio_recorder_wav.c
        ../Src/Storage/wav_parser.c
        ../App/Middlewares/Third_Party/FatFs/src/ff.c
        ../App/Middlewares/Third_Party/FatFs/src/option/ccsbcs.c
    )
    target_compile_definitions(recorder_file_reservation_test PRIVATE
        BRICK6_VARIANT_PREMIUM
    )
    target_include_directories(recorder_file_reservation_test PRIVATE
        stubs
        ../Inc
        ../Inc/SD
        ../Inc/Storage
        ../App/Middlewares/Third_Party/FatFs/src
    )
    add_test(NAME recorder_file_reservation COMMAND recorder_file_reservation_test)
endif()

add_executable(encoder_detent_test
    encoder_detent_test.c
    ../Drivers/Drv_app/Src/encoders_hw.c
)
target_compile_definitions(encoder_detent_test PRIVATE
    BRICK6_VARIANT_LOWCOST
    LOWCOST_ENCODER_TYPE=BOARD_ENCODER_TYPE_MECHANICAL_24_DETENTS
)
target_include_directories(encoder_detent_test PRIVATE
    stubs
    ../Inc
    ../Drivers/Drv_app/Inc
    ../Drivers/CMSIS/Include
)
if(CMAKE_CROSSCOMPILING)
    target_link_options(encoder_detent_test PRIVATE --specs=nosys.specs)
endif()
if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME encoder_detent COMMAND encoder_detent_test)
endif()

add_executable(mod_lfo_segment_test
    mod_lfo_segment_test.c
    ../Src/Mod/mod_lfo_segment.c
)
target_include_directories(mod_lfo_segment_test PRIVATE ../Inc)
if(NOT CMAKE_CROSSCOMPILING)
    if(UNIX)
        target_link_libraries(mod_lfo_segment_test PRIVATE m)
    endif()
    add_test(NAME mod_lfo_segment COMMAND mod_lfo_segment_test)
endif()

add_executable(mod_matrix_ramp_test
    mod_matrix_ramp_test.c
    ../Src/Mod/mod_ramp.c
)
target_include_directories(mod_matrix_ramp_test PRIVATE ../Inc)
if(NOT CMAKE_CROSSCOMPILING)
    if(UNIX)
        target_link_libraries(mod_matrix_ramp_test PRIVATE m)
    endif()
    add_test(NAME mod_matrix_ramp COMMAND mod_matrix_ramp_test)
endif()

add_executable(sample_stream_sequence_test
    sample_stream_sequence_test.c
    ../Src/Sampler/sample_stream_sequence.c
)
target_include_directories(sample_stream_sequence_test PRIVATE ../Inc)
if(CMAKE_CROSSCOMPILING)
    target_link_options(sample_stream_sequence_test PRIVATE --specs=nosys.specs)
endif()
if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME sample_stream_sequence COMMAND sample_stream_sequence_test)
endif()

add_executable(sample_stream_snapshot_test
    sample_stream_snapshot_test.c
    sample_stream_snapshot_admission_stub.c
    ../Src/Sampler/sample_stream_snapshot.c
)
target_include_directories(sample_stream_snapshot_test PRIVATE ../Inc)
if(CMAKE_CROSSCOMPILING)
    target_link_options(sample_stream_snapshot_test PRIVATE --specs=nosys.specs)
endif()
if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME sample_stream_snapshot COMMAND sample_stream_snapshot_test)
endif()

add_executable(sample_stream_needs_test
    sample_stream_needs_test.c
    sample_stream_snapshot_admission_stub.c
    ../Src/Sampler/sample_stream_needs.c
    ../Src/Sampler/sample_stream_sequence.c
    ../Src/Sampler/sample_stream_snapshot.c
)
target_include_directories(sample_stream_needs_test PRIVATE ../Inc)
if(CMAKE_CROSSCOMPILING)
    target_link_options(sample_stream_needs_test PRIVATE --specs=nosys.specs)
endif()

add_executable(sample_stream_scheduler_test
    sample_stream_scheduler_test.c
    ../Src/Sampler/sample_stream_scheduler.c
)
target_include_directories(sample_stream_scheduler_test PRIVATE ../Inc)
if(CMAKE_CROSSCOMPILING)
    target_link_options(sample_stream_scheduler_test PRIVATE --specs=nosys.specs)
endif()

add_executable(vca_env_block_test
    vca_env_block_test.c
    ../Src/Audio/vca_env.c
    ../Src/Audio/env_adsr.c
    ../Src/Audio/env_adsr_peaks.c
)
target_include_directories(vca_env_block_test PRIVATE ../Inc ../Inc/Audio)
target_link_libraries(vca_env_block_test PRIVATE m)
if(CMAKE_CROSSCOMPILING)
    target_link_options(vca_env_block_test PRIVATE --specs=nosys.specs)
endif()
if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME vca_env_block COMMAND vca_env_block_test)
endif()

add_executable(sample_stream_scheduler_n2_test
    sample_stream_scheduler_test.c
    ../Src/Sampler/sample_stream_scheduler.c
)
target_compile_definitions(sample_stream_scheduler_n2_test PRIVATE
    SAMPLE_STREAM_PAGES_PER_VOICE_PER_ROUND=2
)
target_include_directories(sample_stream_scheduler_n2_test PRIVATE ../Inc)
if(CMAKE_CROSSCOMPILING)
    target_link_options(sample_stream_scheduler_n2_test PRIVATE --specs=nosys.specs)
endif()
if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME sample_stream_scheduler_n2 COMMAND sample_stream_scheduler_n2_test)
endif()
if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME sample_stream_scheduler COMMAND sample_stream_scheduler_test)
endif()
if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME sample_stream_needs COMMAND sample_stream_needs_test)
endif()

add_executable(note_fx_state_restore_test
    note_fx_state_restore_test.c
    ../Src/NoteFx/note_fx_state.c
)
target_include_directories(note_fx_state_restore_test PRIVATE ../Inc)
if(CMAKE_CROSSCOMPILING)
    target_link_options(note_fx_state_restore_test PRIVATE --specs=nosys.specs)
endif()
if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME note_fx_state_restore COMMAND note_fx_state_restore_test)
endif()

add_executable(note_fx_euclid_mask_test
    note_fx_euclid_mask_test.c
    ../Src/NoteFx/note_fx_euclid.c
)
target_include_directories(note_fx_euclid_mask_test PRIVATE ../Inc)
if(CMAKE_CROSSCOMPILING)
    target_link_options(note_fx_euclid_mask_test PRIVATE --specs=nosys.specs)
endif()
if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME note_fx_euclid_mask COMMAND note_fx_euclid_mask_test)
endif()

add_executable(note_fx_euclid_runtime_test
    note_fx_euclid_runtime_test.c
    ../Src/NoteFx/note_fx_engine.c
    ../Src/NoteFx/note_fx_arp.c
    ../Src/NoteFx/note_fx_euclid.c
    ../Src/Seq/seq_division_catalog.c
)
target_include_directories(note_fx_euclid_runtime_test PRIVATE ../Inc)
if(CMAKE_CROSSCOMPILING)
    target_link_options(note_fx_euclid_runtime_test PRIVATE --specs=nosys.specs)
endif()
if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME note_fx_euclid_runtime COMMAND note_fx_euclid_runtime_test)
endif()

add_executable(live_parameter_event_test
    live_parameter_event_test.c
    ../Src/Core/live_parameter_event.c
)
target_include_directories(live_parameter_event_test PRIVATE stubs)
target_include_directories(live_parameter_event_test PRIVATE ../Inc)
target_include_directories(live_parameter_event_test PRIVATE ../Inc/Param)
target_include_directories(live_parameter_event_test PRIVATE ../Drivers/CMSIS/Include)
if(CMAKE_CROSSCOMPILING)
    target_link_options(live_parameter_event_test PRIVATE --specs=nosys.specs)
endif()
if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME live_parameter_event COMMAND live_parameter_event_test)
endif()

add_test(NAME note_fx_step1_static_validation
         COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                 -File ${CMAKE_CURRENT_SOURCE_DIR}/note_fx_step1_static_validation.ps1)

add_test(NAME seq_step_note_capture_static_validation
         COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                 -File ${CMAKE_CURRENT_SOURCE_DIR}/seq_step_note_capture_static_validation.ps1)

add_test(NAME note_fx_step10_consolidation_validation
         COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                 -File ${CMAKE_CURRENT_SOURCE_DIR}/note_fx_step10_consolidation_validation.ps1)
