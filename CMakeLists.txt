cmake_minimum_required(VERSION 3.22)

# --------------------------------------------------------------------------
# Project
# --------------------------------------------------------------------------
set(CMAKE_PROJECT_NAME BRICK6_CUBE)

project(${CMAKE_PROJECT_NAME} LANGUAGES C CXX ASM)

message("Build type: ${CMAKE_BUILD_TYPE}")

set(BRICK6_VARIANT "premium" CACHE STRING "Firmware product variant")
set_property(CACHE BRICK6_VARIANT PROPERTY STRINGS premium lowcost)

if(NOT BRICK6_VARIANT STREQUAL "premium" AND NOT BRICK6_VARIANT STREQUAL "lowcost")
    message(FATAL_ERROR "Unsupported BRICK6_VARIANT='${BRICK6_VARIANT}'. Expected 'premium' or 'lowcost'.")
endif()

message("BRICK6 variant: ${BRICK6_VARIANT}")

option(BRICK_TEST_BUILD "Include diagnostic-only firmware features" OFF)
message("BRICK test build: ${BRICK_TEST_BUILD}")

option(BRICK6_MULTI_STREAM_DIAG "Capture the first Multi stream failure for GDB" OFF)
option(BRICK6_MULTI_PITCH_TRACE "Capture bounded Multi pitched voice trace for GDB" OFF)
option(BRICK6_SD_INIT_DIAG "Capture SD initialization diagnostics" OFF)
option(BRICK6_STREAM_UNDERRUN_TRACE
       "Capture bounded causal streaming underrun trace for GDB"
       OFF)
option(BRICK6_STREAM_CALIBRATION "Enable manual streamer calibration cases in Multi TONE" OFF)
set(BRICK6_STREAM_CALIBRATION_PAGE_KIB "32" CACHE STRING
    "Streamer calibration page size (32 KiB)")
set(BRICK6_STREAM_READ_CHUNK_KIB "32" CACHE STRING
    "Streamer application read quantum in KiB")
if(BRICK6_STREAM_CALIBRATION
   AND NOT BRICK6_STREAM_CALIBRATION_PAGE_KIB STREQUAL "32")
    message(FATAL_ERROR "Manual stream calibration requires 32 KiB pages")
endif()
if(BRICK6_STREAM_CALIBRATION
   AND NOT BRICK6_STREAM_READ_CHUNK_KIB MATCHES "^(4|8|16|32)$")
    message(FATAL_ERROR "BRICK6_STREAM_READ_CHUNK_KIB must be 4, 8, 16 or 32")
endif()

# Common sampler/streamer product contract. Board variants only adapt hardware.
set(BRICK6_STREAM_PRODUCT_PAGE_KIB 32)
set(BRICK6_STREAM_PRODUCT_READ_CHUNK_KIB 32)
set(BRICK6_STREAM_PRODUCT_MULTI_PRESOCLE_PAGES 2)
set(BRICK6_STREAM_PRODUCT_MULTI_MOBILE_PAGES 3)
set(BRICK6_STREAM_PRODUCT_VOICE_LOOP_CACHE_PAGES 2)
set(BRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST 1)
if(BRICK6_STREAM_CALIBRATION)
    set(BRICK6_STREAM_EFFECTIVE_READ_CHUNK_KIB ${BRICK6_STREAM_READ_CHUNK_KIB})
else()
    set(BRICK6_STREAM_EFFECTIVE_READ_CHUNK_KIB ${BRICK6_STREAM_PRODUCT_READ_CHUNK_KIB})
endif()
option(BRICK6_MULTI_STREAM_DIAG_RELEASE_CODEGEN
       "Use Release code generation for the Debug Multi stream diagnostic build"
       OFF)

set(BRICK6_RELEASE_LIKE_CONFIG
    "$<OR:$<CONFIG:Release>,$<AND:$<CONFIG:Debug>,$<BOOL:${BRICK6_MULTI_STREAM_DIAG_RELEASE_CODEGEN}>>>")

# --------------------------------------------------------------------------
# Standards
# --------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Useful for clangd / indexing
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

if(BRICK6_MULTI_STREAM_DIAG_RELEASE_CODEGEN AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    # The diagnostic image keeps DWARF but removes the toolchain Debug -Og.
    set(CMAKE_C_FLAGS_DEBUG "-g3")
    set(CMAKE_CXX_FLAGS_DEBUG "-g3")
endif()

# --------------------------------------------------------------------------
# Target
# --------------------------------------------------------------------------
add_executable(${CMAKE_PROJECT_NAME})

# CubeMX-generated base
add_subdirectory(cmake/stm32cubemx)

# --------------------------------------------------------------------------
# User sources
# --------------------------------------------------------------------------

# App / middleware
set(APP_USB_STACK_SRC "")
set(BRICK6_VARIANT_USB_INC_DIR "")

file(GLOB USB_DEVICE_LIB_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/ST/STM32_USB_Device_Library/Core/Src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Src/*.c"
)

file(GLOB USB_HOST_LIB_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/ST/STM32_USB_Host_Library/Core/Src/*.c"
)

file(GLOB FATFS_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/Third_Party/FatFs/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/Third_Party/FatFs/src/option/*.c"
)

# Drivers
set(CMSIS_DSP_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS_DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_f32.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS_DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f32.c"
)

set(DRV_APP_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/buttons.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/buttons_hw.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/drv_display.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/drv_encoders.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/encoders.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/encoders_hw.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/led_anim.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/led_framebuffer.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/led_hw.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/led_layer.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/led_rgb.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/sdram.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/sdram_alloc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/w9825g6kh.c"
)

set(BRICK6_VARIANT_DRIVER_INC_DIRS "")
set(BRICK6_VARIANT_DRIVER_SOURCES "")

if(BRICK6_VARIANT STREQUAL "premium")
    list(APPEND BRICK6_VARIANT_DRIVER_INC_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/Drivers"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/Src"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Drivers"
    )
    list(APPEND BRICK6_VARIANT_DRIVER_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Drivers/tlv320aic3204.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/Src/board_audio_premium.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/Src/board_controls_premium.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/Src/board_display_transport_premium.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/Src/board_led_transport_premium.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/Src/board_power_premium.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/Src/board_product_premium.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/Src/board_surface_premium.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/Src/board_usb_premium.c"
    )
    set(APP_USB_STACK_SRC
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/UsbStack/usb_device.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/UsbStack/usb_host.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/UsbStack/usbd_conf.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/UsbStack/usbd_desc.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/UsbStack/usbh_conf.c"
    )
    set(BRICK6_VARIANT_USB_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/UsbStack")
elseif(BRICK6_VARIANT STREQUAL "lowcost")
    list(APPEND BRICK6_VARIANT_DRIVER_INC_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Drivers"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Src"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/UsbStack"
    )
    list(APPEND BRICK6_VARIANT_DRIVER_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Drivers/tlv320aic3204.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Src/board_audio_lowcost.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Src/board_controls_lowcost.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Src/board_display_lowcost.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Src/board_leds_lowcost.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Src/board_power_lowcost.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Src/board_product_lowcost.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Src/board_surface_lowcost.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Src/board_usb_lowcost.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/UsbStack/fusb302.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/UsbStack/usb_role_manager.c"
    )
    set(APP_USB_STACK_SRC
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/UsbStack/usb_device.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/UsbStack/usb_host.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/UsbStack/usbd_conf.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/UsbStack/usbd_desc.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/UsbStack/usbh_conf.c"
    )
    set(BRICK6_VARIANT_USB_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/UsbStack")
endif()

file(GLOB U8G2_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/U8g2/*.c"
)

# Project source tree (custom code only; base Src/*.c already added by CubeMX)
file(GLOB SRC_APP_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/App/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/App/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/App/Hall/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/App/Hall/*.cpp"
)

list(APPEND SRC_APP_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/App/Hall/hall_keymap.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/App/Hall/hall_keyboard_bridge.c"
)

file(GLOB SRC_AUDIO_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/*.cpp"
)

file(GLOB SRC_CORE_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/*.cpp"
)

list(APPEND SRC_CORE_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/encoder_control_dispatcher.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/live_parameter_audio_queue.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/live_parameter_audio_runtime.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/live_parameter_event.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/live_parameter_migration.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/track_state.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/track_snapshot.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/track_sound_state.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_sampler_runtime.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_stack_runtime.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_stack_waveform.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/rec_live_debug.c"
)

file(GLOB SRC_KEYBOARD_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Keyboard/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Keyboard/*.cpp"
)

file(GLOB SRC_MIDI_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/MIDI/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/MIDI/*.cpp"
)

file(GLOB SRC_MOD_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Mod/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Mod/*.cpp"
)

file(GLOB SRC_PARAM_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Param/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Param/*.cpp"
)

list(APPEND SRC_PARAM_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Param/param_registry_transition.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Param/param_registry_tone_backends.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Param/param_prism_labels.c"
)

file(GLOB SRC_SD_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/SD/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/SD/*.cpp"
)

file(GLOB SRC_SAMPLER_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Sampler/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Sampler/*.cpp"
)

list(APPEND SRC_SAMPLER_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Sampler/sample_play_plan.c"
)

file(GLOB SRC_SEQ_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Seq/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Seq/*.cpp"
)

file(GLOB SRC_STORAGE_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Storage/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Storage/*.cpp"
)

list(APPEND SRC_STORAGE_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Storage/sd_preview.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Storage/wav_audio_codec.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Storage/wav_audio_stream.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Storage/waveform_cache.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Storage/patch_sd_bank.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Storage/patch_v1.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Storage/kit_sd_bank.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Storage/kit_v1.c"
)

file(GLOB SRC_UI_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/pages/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/pages/*.cpp"
)

set(UI_ROLL_POPUP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/ui_roll_popup.c")

list(APPEND SRC_UI_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/ui_system_sync_internal.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/ui_edit_context_sync.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/ui_active_track_sync.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/ui_core_clipboard.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/ui_core_feedback.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/ui_core_mute.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/ui_core_pattern.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/ui_core_seq_transport.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/ui_core_shortcuts.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/pages/ui_page_patch_assign.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/pages/ui_page_kit_assign.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/pages/ui_page_name_edit.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/pages/ui_page_lowcost_button_test.c"
)
list(REMOVE_ITEM SRC_UI_SRC "${UI_ROLL_POPUP_SRC}")
list(REMOVE_DUPLICATES SRC_UI_SRC)

# Mutable Instruments
file(GLOB MUTABLE_CLOUDS_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/clouds/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/clouds/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/clouds/*.cpp"
)

file(GLOB MUTABLE_CLOUDS_DSP_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/clouds/dsp/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/clouds/dsp/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/clouds/dsp/*.cpp"
)

file(GLOB MUTABLE_CLOUDS_PVOC_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/clouds/dsp/pvoc/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/clouds/dsp/pvoc/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/clouds/dsp/pvoc/*.cpp"
)

file(GLOB MUTABLE_PLAITS_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/*.cpp"
)

file(GLOB MUTABLE_PLAITS_CHORDS_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/chords/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/chords/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/chords/*.cpp"
)

file(GLOB MUTABLE_PLAITS_ENGINE_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/engine/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/engine/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/engine/*.cpp"
)

file(GLOB MUTABLE_PLAITS_ENGINE2_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/engine2/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/engine2/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/engine2/*.cpp"
)

file(GLOB MUTABLE_PLAITS_FM_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/fm/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/fm/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/fm/*.cpp"
)

file(GLOB MUTABLE_PLAITS_PM_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/physical_modelling/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/physical_modelling/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/physical_modelling/*.cpp"
)

file(GLOB MUTABLE_PLAITS_SPEECH_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/speech/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/speech/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/plaits/dsp/speech/*.cpp"
)

set(MUTABLE_BRAIDS_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/braids/analog_oscillator.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/braids/digital_oscillator.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/braids/macro_oscillator.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/braids/resources.cc"
)

file(GLOB MUTABLE_STMLIB_DSP_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/stmlib/dsp/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/stmlib/dsp/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/stmlib/dsp/*.cpp"
)

file(GLOB MUTABLE_STMLIB_UTILS_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/stmlib/utils/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/stmlib/utils/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/stmlib/utils/*.cpp"
)

# --------------------------------------------------------------------------
# Exclusions
# --------------------------------------------------------------------------
set(EXCLUDED_SOURCES "")

file(GLOB SRC_NOTE_FX_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/NoteFx/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/NoteFx/*.cpp"
)

set(BRICK_TEST_ONLY_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/audio_track_diag.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/audio_test_runner.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/audio_test2.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/crash_capsule.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/diagnostic_input.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/diagnostic_watchdog.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/monkey_test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/monkey_test_action.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/monkey_test_monitor.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/monkey_test_replay.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/monkey_test_safety.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Storage/audio_test_csv.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Storage/monkey_test_log.c"
)

if(NOT BRICK_TEST_BUILD)
    list(APPEND EXCLUDED_SOURCES ${BRICK_TEST_ONLY_SOURCES})
    if(BRICK6_MULTI_STREAM_DIAG)
        list(REMOVE_ITEM EXCLUDED_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/crash_capsule.c"
        )
    endif()
endif()

set(ALL_USER_SOURCES
    ${APP_USB_STACK_SRC}
    ${USB_DEVICE_LIB_SRC}
    ${USB_HOST_LIB_SRC}
    ${FATFS_SRC}
    ${CMSIS_DSP_SRC}
    ${DRV_APP_SRC}
    ${BRICK6_VARIANT_DRIVER_SOURCES}
    ${U8G2_SRC}
    ${SRC_APP_SRC}
    ${SRC_AUDIO_SRC}
    ${SRC_CORE_SRC}
    ${SRC_KEYBOARD_SRC}
    ${SRC_MIDI_SRC}
    ${SRC_MOD_SRC}
    ${SRC_NOTE_FX_SRC}
    ${SRC_PARAM_SRC}
    ${SRC_SD_SRC}
    ${SRC_SAMPLER_SRC}
    ${SRC_SEQ_SRC}
    ${SRC_STORAGE_SRC}
    ${SRC_UI_SRC}
    ${MUTABLE_CLOUDS_SRC}
    ${MUTABLE_CLOUDS_DSP_SRC}
    ${MUTABLE_CLOUDS_PVOC_SRC}
    ${MUTABLE_PLAITS_SRC}
    ${MUTABLE_PLAITS_CHORDS_SRC}
    ${MUTABLE_PLAITS_ENGINE_SRC}
    ${MUTABLE_PLAITS_ENGINE2_SRC}
    ${MUTABLE_PLAITS_FM_SRC}
    ${MUTABLE_PLAITS_PM_SRC}
    ${MUTABLE_PLAITS_SPEECH_SRC}
    ${MUTABLE_BRAIDS_SRC}
    ${MUTABLE_STMLIB_DSP_SRC}
    ${MUTABLE_STMLIB_UTILS_SRC}
)

list(REMOVE_ITEM ALL_USER_SOURCES ${EXCLUDED_SOURCES})
list(REMOVE_DUPLICATES ALL_USER_SOURCES)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${ALL_USER_SOURCES}
    "${UI_ROLL_POPUP_SRC}"
)

# --------------------------------------------------------------------------
# Include directories
# --------------------------------------------------------------------------
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/App
    ${BRICK6_VARIANT_USB_INC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/Third_Party/FatFs/src

    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Board
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Audio
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/MIDI
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Param
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Storage
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/UI
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/SD

    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Inc
    ${BRICK6_VARIANT_DRIVER_INC_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/U8g2
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS_DSP/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS_DSP/PrivateInclude
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Daisy_SP/Source/Utility

    ${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments
    ${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/stmlib
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyusb
)

# --------------------------------------------------------------------------
# Defines
# --------------------------------------------------------------------------
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USE_PWR_LDO_SUPPLY
    USE_HAL_DRIVER
    STM32H743xx
    BRICK_TEST_BUILD=$<BOOL:${BRICK_TEST_BUILD}>
    $<$<BOOL:${BRICK6_MULTI_STREAM_DIAG}>:BRICK6_MULTI_STREAM_DIAG>
    $<$<BOOL:${BRICK6_MULTI_PITCH_TRACE}>:BRICK6_MULTI_PITCH_TRACE=1>
    $<$<BOOL:${BRICK6_SD_INIT_DIAG}>:BRICK6_SD_INIT_DIAG=1>
    $<$<BOOL:${BRICK6_STREAM_UNDERRUN_TRACE}>:BRICK6_STREAM_UNDERRUN_TRACE=1>
    $<$<BOOL:${BRICK6_STREAM_CALIBRATION}>:BRICK6_STREAM_CALIBRATION=1>
    $<$<BOOL:${BRICK6_STREAM_CALIBRATION}>:BRICK6_STREAM_BENCH=1>
    $<$<BOOL:${BRICK6_STREAM_CALIBRATION}>:BRICK6_STREAM_CALIBRATION_PAGE_KIB=${BRICK6_STREAM_CALIBRATION_PAGE_KIB}>
    BRICK6_STREAM_PRODUCT_PAGE_KIB=${BRICK6_STREAM_PRODUCT_PAGE_KIB}
    BRICK6_STREAM_PRODUCT_MULTI_PRESOCLE_PAGES=${BRICK6_STREAM_PRODUCT_MULTI_PRESOCLE_PAGES}
    BRICK6_STREAM_PRODUCT_MULTI_MOBILE_PAGES=${BRICK6_STREAM_PRODUCT_MULTI_MOBILE_PAGES}
    BRICK6_STREAM_PRODUCT_VOICE_LOOP_CACHE_PAGES=${BRICK6_STREAM_PRODUCT_VOICE_LOOP_CACHE_PAGES}
    BRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST=${BRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST}
    BRICK6_STREAM_READ_CHUNK_KIB=${BRICK6_STREAM_EFFECTIVE_READ_CHUNK_KIB}
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    $<$<AND:$<CONFIG:Debug>,$<NOT:$<BOOL:${BRICK6_MULTI_STREAM_DIAG_RELEASE_CODEGEN}>>>:DEBUG>
)

if(BRICK6_VARIANT STREQUAL "premium")
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        BRICK6_VARIANT_PREMIUM
        TLV320AIC3204_I2C_HANDLE=hi2c2
    )
elseif(BRICK6_VARIANT STREQUAL "lowcost")
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE BRICK6_VARIANT_LOWCOST)
endif()

# --------------------------------------------------------------------------
# Compile options aligned with CubeIDE
# --------------------------------------------------------------------------
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
    -mcpu=cortex-m7
    -mfpu=fpv5-d16
    -mfloat-abi=hard
    -mthumb
    -ffunction-sections
    -fdata-sections
    -Wall
    --specs=nano.specs

    $<$<AND:$<CONFIG:Debug>,$<NOT:$<BOOL:${BRICK6_MULTI_STREAM_DIAG_RELEASE_CODEGEN}>>>:-Og>
    $<$<CONFIG:Debug>:-g3>
    $<${BRICK6_RELEASE_LIKE_CONFIG}:-O1>

    $<$<AND:${BRICK6_RELEASE_LIKE_CONFIG},$<COMPILE_LANGUAGE:C>>:-fno-math-errno>
    $<$<AND:${BRICK6_RELEASE_LIKE_CONFIG},$<COMPILE_LANGUAGE:C>>:-ffast-math>
    $<$<AND:${BRICK6_RELEASE_LIKE_CONFIG},$<COMPILE_LANGUAGE:C>>:-fsingle-precision-constant>
    $<$<COMPILE_LANGUAGE:C>:-fstack-usage>

    $<$<AND:${BRICK6_RELEASE_LIKE_CONFIG},$<COMPILE_LANGUAGE:CXX>>:-fno-math-errno>
    $<$<AND:${BRICK6_RELEASE_LIKE_CONFIG},$<COMPILE_LANGUAGE:CXX>>:-ffast-math>
    $<$<AND:${BRICK6_RELEASE_LIKE_CONFIG},$<COMPILE_LANGUAGE:CXX>>:-fsingle-precision-constant>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
    $<$<COMPILE_LANGUAGE:CXX>:-fstack-usage>
)

# Tous les fichiers C++ en O2 en Release
get_target_property(ALL_TARGET_SOURCES ${CMAKE_PROJECT_NAME} SOURCES)

set(ALL_CPP_SOURCES ${ALL_TARGET_SOURCES})
list(FILTER ALL_CPP_SOURCES INCLUDE REGEX ".*\\.(cpp|cc|cxx)$")

set_source_files_properties(${ALL_CPP_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O2>"
)

# Braids only: favor IRQ throughput and enable cross-TU optimization in Release.
set(BRAIDS_O3_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/braids/analog_oscillator.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/braids/digital_oscillator.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/braids/macro_oscillator.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/braids/resources.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_braids_runtime.cpp"
)
set_source_files_properties(${BRAIDS_O3_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O3>;$<${BRICK6_RELEASE_LIKE_CONFIG}:-flto>"
)

# Common audio engine: keep LTO scoped to audio, DSP/modulation and synth runtime.
set(AUDIO_ALL_SOURCES ${SRC_AUDIO_SRC})
list(FILTER AUDIO_ALL_SOURCES INCLUDE REGEX ".*\\.(c|cc|cpp|cxx)$")

set(AUDIO_LTO_SOURCES
    ${AUDIO_ALL_SOURCES} ${CMSIS_DSP_SRC} ${SRC_MOD_SRC}
    ${MUTABLE_BRAIDS_SRC} ${MUTABLE_PLAITS_CHORDS_SRC} ${MUTABLE_PLAITS_ENGINE_SRC}
    ${MUTABLE_PLAITS_ENGINE2_SRC} ${MUTABLE_PLAITS_FM_SRC} ${MUTABLE_PLAITS_PM_SRC}
    ${MUTABLE_PLAITS_SPEECH_SRC} ${MUTABLE_STMLIB_DSP_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_audio_runtime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_braids_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_stack_runtime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_stack_waveform.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_wave_runtime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_sampler_runtime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_looper_runtime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/synth_polyphony.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Param/param_registry_apply_wrappers.c
)
set_source_files_properties(${AUDIO_LTO_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-flto>"
)

# LFO control/polyphonic preparation: keep the optimization scope limited to
# the two translation units reached from the hard real-time audio path.
set(MOD_LFO_O3_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Mod/mod_lfo_v1.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Mod/mod_lfo_segment.c"
)
set_source_files_properties(${MOD_LFO_O3_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O1>;$<${BRICK6_RELEASE_LIKE_CONFIG}:-flto>"
)

set(AUDIO_O3_SOURCES
    ${AUDIO_ALL_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_audio_runtime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_braids_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_stack_runtime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_stack_waveform.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_wave_runtime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_sampler_runtime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_looper_runtime.c
    ${MUTABLE_BRAIDS_SRC} ${MUTABLE_PLAITS_CHORDS_SRC} ${MUTABLE_PLAITS_ENGINE_SRC}
    ${MUTABLE_PLAITS_ENGINE2_SRC} ${MUTABLE_PLAITS_FM_SRC} ${MUTABLE_PLAITS_PM_SRC}
    ${MUTABLE_PLAITS_SPEECH_SRC} ${MUTABLE_STMLIB_DSP_SRC}
)
set_source_files_properties(${AUDIO_O3_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O3>;$<${BRICK6_RELEASE_LIKE_CONFIG}:-flto>"
)

set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/mixer.c"
    PROPERTIES COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O2>;$<${BRICK6_RELEASE_LIKE_CONFIG}:-flto>"
)

# GCC 13.3 LTO can emit a corrupted .init_array relocation for the sole
# non-trivial global C++ initializer in the diagnostic image. Keep this TU at
# Release optimization while making its constructor relocation ordinary.
if(BRICK6_MULTI_STREAM_DIAG AND BRICK6_MULTI_STREAM_DIAG_RELEASE_CODEGEN)
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/fx_reverb_revb.cpp"
        PROPERTIES COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O3>;-fno-lto"
    )
endif()

set_source_files_properties(${CMSIS_DSP_SRC} PROPERTIES
    COMPILE_DEFINITIONS "ARM_MATH_CM7;ARM_MATH_LOOPUNROLL"
)

# Hot sampler render code reached from the common audio block.
# Keep sample_cache/sample_stream_manager and the other Sampler sources out:
# they also own SD/FatFs, refill, page scheduling or asynchronous state.
set(SAMPLE_RENDER_HOT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Sampler/sample_voice_reader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Sampler/sample_play_plan.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Sampler/voice_manager.c"
)
set_source_files_properties(${SAMPLE_RENDER_HOT_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O3>;$<${BRICK6_RELEASE_LIKE_CONFIG}:-flto>"
)

# Storage en O0 : fichiers C et C++ du dossier Src/Storage
set(STORAGE_ALL_SOURCES ${SRC_STORAGE_SRC})
list(FILTER STORAGE_ALL_SOURCES INCLUDE REGEX ".*\\.(c|cc|cpp|cxx)$")

set_source_files_properties(${STORAGE_ALL_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O0>"
)

# U8g2 en O0
set_source_files_properties(${U8G2_SRC} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O0>"
)
list(FILTER AUDIO_C_SOURCES INCLUDE REGEX ".*\\.c$")

# --------------------------------------------------------------------------
# Link options aligned with CubeIDE
# --------------------------------------------------------------------------
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -mcpu=cortex-m7
    -mfpu=fpv5-d16
    -mfloat-abi=hard
    -mthumb
    -T${CMAKE_CURRENT_SOURCE_DIR}/Board/$<IF:$<STREQUAL:${BRICK6_VARIANT},premium>,Premium,LowCost>/Generated/Linker/STM32H743IITX_FLASH.ld
    --specs=nosys.specs
    --specs=nano.specs
    -Wl,-Map=${CMAKE_PROJECT_NAME}.map
    -Wl,--gc-sections
    -static
    -u
    _printf_float
    -Wl,--start-group
    -lc
    -lm
    -lstdc++
    -lsupc++
    -Wl,--end-group
)

target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    $<${BRICK6_RELEASE_LIKE_CONFIG}:-flto>
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${BRICK6_MULTI_STREAM_DIAG_RELEASE_CODEGEN}>>:-g3>
)

# C++ linker because project contains cpp files
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

# Remove wrong implicit lib if present
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Keep CubeMX link style consistent
target_link_libraries(${CMAKE_PROJECT_NAME} stm32cubemx)
