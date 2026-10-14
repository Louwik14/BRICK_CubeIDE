cmake_minimum_required(VERSION 3.24)
project(brick_ui_sim LANGUAGES C ASM)

if(NOT WIN32)
  message(FATAL_ERROR "BRICK UI Simulator supports native Windows only")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(BRICK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
find_package(SDL3 CONFIG REQUIRED)

set(U8G2_SOURCES
  "${BRICK_ROOT}/Drivers/U8g2/u8g2_bitmap.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8g2_box.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8g2_buffer.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8g2_circle.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8g2_font.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8g2_fonts.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8g2_hvline.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8g2_intersection.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8g2_kerning.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8g2_line.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8g2_ll_hvline.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8g2_setup.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8x8_8x8.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8x8_byte.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8x8_cad.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8x8_capture.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8x8_debounce.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8x8_d_ssd1309.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8x8_display.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8x8_gpio.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8x8_message.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8x8_setup.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8x8_string.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8x8_u16toa.c"
  "${BRICK_ROOT}/Drivers/U8g2/u8x8_u8toa.c"
)

set_source_files_properties(
  "${BRICK_ROOT}/Drivers/U8g2/u8g2_fonts.c"
  PROPERTIES COMPILE_OPTIONS "-Wno-overlength-strings"
)

file(GLOB BRICK_UI_PAGES CONFIGURE_DEPENDS
  "${BRICK_ROOT}/Src/UI/pages/*.c"
)

set(BRICK_UI_NAVIGATION_SOURCES
  "${BRICK_ROOT}/Src/UI/ui_active_track_sync.c"
  "${BRICK_ROOT}/Src/UI/ui_algo_icons.c"
  "${BRICK_ROOT}/Src/UI/ui_bootstrap.c"
  "${BRICK_ROOT}/Src/UI/ui_core.c"
  "${BRICK_ROOT}/Src/UI/ui_core_clipboard.c"
  "${BRICK_ROOT}/Src/UI/ui_core_feedback.c"
  "${BRICK_ROOT}/Src/UI/ui_core_mute.c"
  "${BRICK_ROOT}/Src/UI/ui_core_pattern.c"
  "${BRICK_ROOT}/Src/UI/ui_core_seq_transport.c"
  "${BRICK_ROOT}/Src/UI/ui_core_shortcuts.c"
  "${BRICK_ROOT}/Src/UI/ui_edit_context_sync.c"
  "${BRICK_ROOT}/Src/UI/ui_event.c"
  "${BRICK_ROOT}/Src/UI/ui_hall_input_service.c"
  "${BRICK_ROOT}/Src/UI/ui_hall_mode_contract.c"
  "${BRICK_ROOT}/Src/UI/ui_hall_mode_flow.c"
  "${BRICK_ROOT}/Src/UI/ui_hall_mode_projection.c"
  "${BRICK_ROOT}/Src/UI/ui_hall_mode_state.c"
  "${BRICK_ROOT}/Src/UI/ui_macro_interaction.c"
  "${BRICK_ROOT}/Src/UI/ui_navigation.c"
  "${BRICK_ROOT}/Src/UI/ui_page_manager.c"
  "${BRICK_ROOT}/Src/UI/ui_param.c"
  "${BRICK_ROOT}/Src/UI/ui_renderer_oled.c"
  "${BRICK_ROOT}/Src/UI/ui_renderer_template.c"
  "${BRICK_ROOT}/Src/UI/ui_roll_popup.c"
  "${BRICK_ROOT}/Src/UI/ui_sample_playhead.c"
  "${BRICK_ROOT}/Src/UI/ui_step_led_ownership.c"
  "${BRICK_ROOT}/Src/UI/ui_template_page.c"
  "${BRICK_ROOT}/Src/UI/ui_theme.c"
  "${BRICK_ROOT}/Src/UI/ui_track_catalog.c"
  "${BRICK_ROOT}/Src/UI/ui_track_led_projection.c"
  "${BRICK_ROOT}/Src/UI/ui_widgets.c"
)

add_executable(brick-ui-sim
  src/sim_main.c
  src/sim_engine.c
  src/sim_cli.c
  src/sim_json.c
  src/sim_keymap.c
  src/sim_png.c
  src/sim_path.c
  src/sim_sdl.c
  mocks/sim_firmware_mocks.c
  mocks/sim_display_transport.c
  mocks/sim_input_hw.c
  mocks/sim_state.c
  mocks/sim_assets.c
  mocks/sim_unimplemented.c
  "${BRICK_ROOT}/Drivers/Drv_app/Src/drv_display.c"
  "${BRICK_ROOT}/Drivers/Drv_app/Src/buttons.c"
  "${BRICK_ROOT}/Drivers/Drv_app/Src/encoders.c"
  "${BRICK_ROOT}/Src/App/name_contract.c"
  "${BRICK_ROOT}/Src/Param/param_registry.c"
  "${BRICK_ROOT}/Src/Param/param_registry_catalog.c"
  "${BRICK_ROOT}/Src/Param/param_registry_value_contract.c"
  "${BRICK_ROOT}/Src/Param/param_value_policy.c"
  ${BRICK_UI_NAVIGATION_SOURCES}
  ${BRICK_UI_PAGES}
  ${U8G2_SOURCES}
)

target_include_directories(brick-ui-sim PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat"
  "${BRICK_ROOT}/Inc"
  "${BRICK_ROOT}/Inc/UI"
  "${BRICK_ROOT}/Inc/Audio"
  "${BRICK_ROOT}/Inc/Board"
  "${BRICK_ROOT}/Inc/MIDI"
  "${BRICK_ROOT}/Inc/Param"
  "${BRICK_ROOT}/Inc/Storage"
  "${BRICK_ROOT}/Inc/SD"
  "${BRICK_ROOT}/App/Middlewares/Third_Party/FatFs/src"
  "${BRICK_ROOT}/Drivers/Drv_app/Inc"
  "${BRICK_ROOT}/Drivers/U8g2"
  "${BRICK_ROOT}/Src/UI"
)

target_compile_definitions(brick-ui-sim PRIVATE
  SDL_MAIN_HANDLED
  SIM_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  target_compile_options(brick-ui-sim PRIVATE
    -Wall -Wextra
    -include "${CMAKE_CURRENT_SOURCE_DIR}/compat/sim_host_prelude.h"
  )
endif()

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/mocks/unimplemented-symbols.txt"
     SIM_UNIMPLEMENTED_SYMBOLS)
set(SIM_UNIMPLEMENTED_ASM ".text\n.extern sim_unimplemented\n")
foreach(symbol IN LISTS SIM_UNIMPLEMENTED_SYMBOLS)
  string(APPEND SIM_UNIMPLEMENTED_ASM
    ".globl ${symbol}\n${symbol}:\njmp sim_unimplemented\n")
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/sim_unimplemented_aliases.S"
     CONTENT "${SIM_UNIMPLEMENTED_ASM}")
target_sources(brick-ui-sim PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/sim_unimplemented_aliases.S")

target_link_libraries(brick-ui-sim PRIVATE SDL3::SDL3)

add_custom_command(TARGET brick-ui-sim POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:SDL3::SDL3>"
          "$<TARGET_FILE_DIR:brick-ui-sim>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "C:/msys64/ucrt64/bin/libiconv-2.dll"
          "$<TARGET_FILE_DIR:brick-ui-sim>"
  VERBATIM
)

enable_testing()
add_test(NAME keymap_contract
  COMMAND brick-ui-sim --validate-keymap
)
add_test(NAME cli_smoke
  COMMAND brick-ui-sim --self-test "${CMAKE_CURRENT_SOURCE_DIR}/build/windows-ucrt/self-test"
)
add_test(NAME scenario_core
  COMMAND brick-ui-sim --headless --scenario "${CMAKE_CURRENT_SOURCE_DIR}/scenarios/manual-core.jsonl"
)
add_test(NAME scenario_browsers
  COMMAND brick-ui-sim --headless --scenario "${CMAKE_CURRENT_SOURCE_DIR}/scenarios/manual-browsers.jsonl"
)
add_test(NAME scenario_patch_name
  COMMAND brick-ui-sim --headless --scenario "${CMAKE_CURRENT_SOURCE_DIR}/scenarios/manual-patch-name.jsonl"
)
