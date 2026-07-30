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

if(WIN32)
    add_test(NAME wave_mipmap_spectral
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                     -File ${CMAKE_CURRENT_SOURCE_DIR}/wave_mipmap_spectral_test.ps1)
endif()
