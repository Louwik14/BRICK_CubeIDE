cmake_minimum_required(VERSION 3.24)
project(brick_ui_sim LANGUAGES C)

if(NOT WIN32)
  message(FATAL_ERROR "BRICK UI Simulator pass 1 supports native Windows only")
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

add_executable(brick-ui-sim
  src/sim_main.c
  src/sim_engine.c
  src/sim_cli.c
  src/sim_json.c
  src/sim_png.c
  src/sim_path.c
  src/sim_sdl.c
  mocks/sim_firmware_mocks.c
  mocks/sim_display_transport.c
  "${BRICK_ROOT}/Drivers/Drv_app/Src/drv_display.c"
  "${BRICK_ROOT}/Src/App/name_contract.c"
  "${BRICK_ROOT}/Src/UI/ui_page_manager.c"
  "${BRICK_ROOT}/Src/UI/ui_renderer_oled.c"
  "${BRICK_ROOT}/Src/UI/pages/ui_page_name_edit.c"
  ${U8G2_SOURCES}
)

target_include_directories(brick-ui-sim PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat"
  "${BRICK_ROOT}/Inc"
  "${BRICK_ROOT}/Inc/UI"
  "${BRICK_ROOT}/Drivers/Drv_app/Inc"
  "${BRICK_ROOT}/Drivers/U8g2"
  "${BRICK_ROOT}/Src/UI"
)

target_compile_definitions(brick-ui-sim PRIVATE
  SDL_MAIN_HANDLED
  SIM_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  target_compile_options(brick-ui-sim PRIVATE -Wall -Wextra -Wpedantic)
endif()

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
add_test(NAME cli_smoke
  COMMAND brick-ui-sim --self-test "${CMAKE_CURRENT_SOURCE_DIR}/build/windows-ucrt/self-test"
)
