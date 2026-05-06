cmake_minimum_required(VERSION 3.22)

# --------------------------------------------------------------------------
# Project
# --------------------------------------------------------------------------
set(CMAKE_PROJECT_NAME BRICK6_CUBE)

project(${CMAKE_PROJECT_NAME} LANGUAGES C CXX ASM)

message("Build type: ${CMAKE_BUILD_TYPE}")

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
file(GLOB APP_USB_STACK_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/App/usb_stack/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/App/usb_stack/*.cpp"
)

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

file(GLOB DRV_APP_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/*.cpp"
)

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
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/App/Hall/hall_keyboard_bridge.c"
)

file(GLOB SRC_AUDIO_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/*.cpp"
)

list(APPEND SRC_AUDIO_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/adsr_daisy.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/fx_reverb_gverb.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/fx_reverb_oliverb.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/fx_reverb_revb.cpp"
)

file(GLOB SRC_CORE_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/*.cpp"
)

list(APPEND SRC_CORE_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/track_state.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/track_sound_state.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Core/brick6_sampler_runtime.c"
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
)

file(GLOB SRC_SD_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/SD/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/SD/*.cpp"
)

file(GLOB SRC_SAMPLER_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Sampler/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Sampler/*.cpp"
)

file(GLOB SRC_SEQ_SRC
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
)

file(GLOB SRC_UI_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/pages/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI/pages/*.cpp"
)

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
)

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
set(EXCLUDED_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/md-drum-synth-main/FmClapModel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/md-drum-synth-main/FmCowbellModel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/md-drum-synth-main/FmCymbalModel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/md-drum-synth-main/FmKickModel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/md-drum-synth-main/FmRimshotModel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/md-drum-synth-main/FmSnareModel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/md-drum-synth-main/FmTomModel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/md-drum-synth-main/TRXBassDrum.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/md-drum-synth-main/TRXClaves.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/md-drum-synth-main/TRXHiHat.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/md-drum-synth-main/TRXSnareDrum.cpp"
)

set(ALL_USER_SOURCES
    ${APP_USB_STACK_SRC}
    ${USB_DEVICE_LIB_SRC}
    ${USB_HOST_LIB_SRC}
    ${FATFS_SRC}
    ${CMSIS_DSP_SRC}
    ${DRV_APP_SRC}
    ${U8G2_SRC}
    ${SRC_APP_SRC}
    ${SRC_AUDIO_SRC}
    ${SRC_CORE_SRC}
    ${SRC_KEYBOARD_SRC}
    ${SRC_MIDI_SRC}
    ${SRC_MOD_SRC}
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

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${ALL_USER_SOURCES})
# --------------------------------------------------------------------------
# Include directories
# --------------------------------------------------------------------------
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/App
    ${CMAKE_CURRENT_SOURCE_DIR}/App/usb_stack
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/Third_Party/FatFs/src

    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Audio
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/MIDI
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Param
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Storage
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/UI
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/SD

    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/U8g2
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS_DSP/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS_DSP/PrivateInclude
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Daisy_SP

    ${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments
    ${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/stmlib
    ${CMAKE_CURRENT_SOURCE_DIR}/md-drum-synth-main
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyusb
)

# --------------------------------------------------------------------------
# Defines
# --------------------------------------------------------------------------
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USE_PWR_LDO_SUPPLY
    USE_HAL_DRIVER
    STM32H743xx
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)

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

    $<$<CONFIG:Debug>:-Og>
    $<$<CONFIG:Release>:-O1>

    $<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C>>:-fno-math-errno>
    $<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C>>:-ffast-math>
    $<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C>>:-fsingle-precision-constant>
    $<$<COMPILE_LANGUAGE:C>:-fstack-usage>

    $<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:-fno-math-errno>
    $<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:-ffast-math>
    $<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:-fsingle-precision-constant>
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
    COMPILE_OPTIONS "$<$<CONFIG:Release>:-O2>"
)

# Audio en O2 : fichiers C et C++ du dossier Src/Audio
set(AUDIO_ALL_SOURCES ${SRC_AUDIO_SRC})
list(FILTER AUDIO_ALL_SOURCES INCLUDE REGEX ".*\\.(c|cc|cpp|cxx)$")

set_source_files_properties(${AUDIO_ALL_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<$<CONFIG:Release>:-O2>"
)

# Storage en O0 : fichiers C et C++ du dossier Src/Storage
set(STORAGE_ALL_SOURCES ${SRC_STORAGE_SRC})
list(FILTER STORAGE_ALL_SOURCES INCLUDE REGEX ".*\\.(c|cc|cpp|cxx)$")

set_source_files_properties(${STORAGE_ALL_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<$<CONFIG:Release>:-O0>"
)

# U8g2 en O0
set_source_files_properties(${U8G2_SRC} PROPERTIES
    COMPILE_OPTIONS "$<$<CONFIG:Release>:-O0>"
)
list(FILTER AUDIO_C_SOURCES INCLUDE REGEX ".*\\.c$")

set_source_files_properties(${AUDIO_C_SOURCES} PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Release>:-O3>")
# --------------------------------------------------------------------------
# Link options aligned with CubeIDE
# --------------------------------------------------------------------------
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -mcpu=cortex-m7
    -mfpu=fpv5-d16
    -mfloat-abi=hard
    -mthumb
    -T${CMAKE_CURRENT_SOURCE_DIR}/STM32H743IITX_FLASH.ld
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
    $<$<CONFIG:Release>:-flto>
)

# C++ linker because project contains cpp files
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

# Remove wrong implicit lib if present
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Keep CubeMX link style consistent
target_link_libraries(${CMAKE_PROJECT_NAME} stm32cubemx)
