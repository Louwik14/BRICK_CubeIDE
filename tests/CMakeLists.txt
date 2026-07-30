cmake_minimum_required(VERSION 3.20)
project(brick6_filter_tests C)

enable_testing()

add_executable(filter_tpt_modulation_test
    filter_tpt_modulation_main.c
    filter_tpt_modulation_test.c
    ../Src/Audio/fx_biquad_filter.c
)

target_include_directories(filter_tpt_modulation_test PRIVATE ../Inc ../Inc/Audio)
target_link_libraries(filter_tpt_modulation_test PRIVATE m)

add_test(NAME filter_tpt_modulation COMMAND filter_tpt_modulation_test)

add_executable(common_modulation_test
    common_modulation_main.c
    common_modulation_test.c
)

target_link_libraries(common_modulation_test PRIVATE m)
add_test(NAME common_modulation COMMAND common_modulation_test)

add_executable(keyboard_arp_multitrack_test
    keyboard_arp_multitrack_test.c
    ../Src/Keyboard/keyboard_arp.c
)

target_include_directories(keyboard_arp_multitrack_test PRIVATE
    stubs
    ../Inc
)
add_test(NAME keyboard_arp_multitrack COMMAND keyboard_arp_multitrack_test)

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

if(WIN32)
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
endif()
