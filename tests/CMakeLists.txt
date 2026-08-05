cmake_minimum_required(VERSION 3.20)
project(brick6_note_fx_step1_tests C)

enable_testing()

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

add_test(NAME note_fx_step1_static_validation
         COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                 -File ${CMAKE_CURRENT_SOURCE_DIR}/note_fx_step1_static_validation.ps1)
