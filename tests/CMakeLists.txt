cmake_minimum_required(VERSION 3.20)
project(brick6_note_fx_step1_tests C)

enable_testing()

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

add_executable(live_parameter_event_test
    live_parameter_event_test.c
)
target_include_directories(live_parameter_event_test PRIVATE ../Inc)
target_include_directories(live_parameter_event_test PRIVATE ../Inc/Param)
if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME live_parameter_event COMMAND live_parameter_event_test)
endif()

add_test(NAME note_fx_step1_static_validation
         COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                 -File ${CMAKE_CURRENT_SOURCE_DIR}/note_fx_step1_static_validation.ps1)
