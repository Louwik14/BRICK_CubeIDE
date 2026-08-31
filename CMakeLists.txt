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

set(FM_KERNEL_BENCH "0" CACHE STRING
    "Experimental DX7 algorithm-1 renderer: 0=MSFA, 1=sample-centric log kernel")
set_property(CACHE FM_KERNEL_BENCH PROPERTY STRINGS 0 1)
if(NOT FM_KERNEL_BENCH MATCHES "^[01]$")
    message(FATAL_ERROR "FM_KERNEL_BENCH must be 0 or 1")
endif()
message("FM kernel bench: ${FM_KERNEL_BENCH}")

set(BRICK6_STREAM_READ_CHUNK_KIB "32" CACHE STRING
    "Streamer application read quantum in KiB")
if(NOT BRICK6_STREAM_READ_CHUNK_KIB MATCHES "^(4|8|16|32)$")
    message(FATAL_ERROR "BRICK6_STREAM_READ_CHUNK_KIB must be 4, 8, 16 or 32")
endif()

# Common sampler/streamer product contract. Board variants only adapt hardware.
set(BRICK6_STREAM_PRODUCT_PAGE_KIB 32)
set(BRICK6_STREAM_PRODUCT_READ_CHUNK_KIB 32)
set(BRICK6_STREAM_PRODUCT_MULTI_PRESOCLE_PAGES 2)
set(BRICK6_STREAM_PRODUCT_MULTI_MOBILE_PAGES 3)
set(BRICK6_STREAM_PRODUCT_VOICE_LOOP_CACHE_PAGES 2)
set(BRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST 1)
set(BRICK6_STREAM_EFFECTIVE_READ_CHUNK_KIB ${BRICK6_STREAM_PRODUCT_READ_CHUNK_KIB})

set(BRICK6_RELEASE_LIKE_CONFIG "$<CONFIG:Release>")

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

# Drivers and third-party libraries retain their vendor source discovery; they are
# not part of the BRICK ownership inventory below.
set(CMSIS_DSP_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS_DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_f32.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS_DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f32.c"
)
file(GLOB U8G2_SRC "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/U8g2/*.c")
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

set(MUTABLE_INSTRUMENTS_SRC
    ${MUTABLE_CLOUDS_SRC} ${MUTABLE_CLOUDS_DSP_SRC} ${MUTABLE_CLOUDS_PVOC_SRC}
    ${MUTABLE_PLAITS_SRC} ${MUTABLE_PLAITS_CHORDS_SRC} ${MUTABLE_PLAITS_ENGINE_SRC}
    ${MUTABLE_PLAITS_ENGINE2_SRC} ${MUTABLE_PLAITS_FM_SRC} ${MUTABLE_PLAITS_PM_SRC}
    ${MUTABLE_PLAITS_SPEECH_SRC} ${MUTABLE_BRAIDS_SRC}
    ${MUTABLE_STMLIB_DSP_SRC} ${MUTABLE_STMLIB_UTILS_SRC})

set(BRICK6_VARIANT_DRIVER_INC_DIRS "")
if(BRICK6_VARIANT STREQUAL "premium")
    list(APPEND BRICK6_VARIANT_DRIVER_INC_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/Drivers"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/Src"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Drivers")
    set(BRICK6_VARIANT_USB_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/UsbStack")
elseif(BRICK6_VARIANT STREQUAL "lowcost")
    list(APPEND BRICK6_VARIANT_DRIVER_INC_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Drivers"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Src"
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/UsbStack")
    set(BRICK6_VARIANT_USB_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/UsbStack")
endif()

# --------------------------------------------------------------------------
# Explicit BRICK execution-domain manifests
# --------------------------------------------------------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/brick_domain_manifest.cmake")

set(DOMAIN_CONTRACT_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Param/param_ids.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Param/param_spec.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Mod/mod_lfo_types.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Mod/mod_destination_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/audio_rec_bus_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/audio_recorder_capture.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/audio_recorder_capture_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/audio_rec_level_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/audio_boot_diagnostic.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/audio_boot_diagnostic_layout.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/audio_waveform_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/audio_wave_table_projection.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/audio_wavetable_registry_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/control_audio_command.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/control_audio_fifo_layout.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/live_parameter_event.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/multi_sample_audio_projection.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/multi_sample_audio_projection_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/sample_classic_audio_projection_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/sampler_ram_audio_projection.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/sampler_ram_audio_projection_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/shared_memory_ref.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/synth_waveform_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC/sd_preview_ring_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sample_audio_format.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/wavetable_config.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sample_audio_key.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/multi_sample_config.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sample_play_plan.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sample_reader_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sample_page_cache_config.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sample_page_cache_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sample_page_cache_shared_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sample_page_lease.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Track/entity_types.h")

# The sole first-party GLOB is an audit inventory. It never classifies or adds a
# build source. Any new translation unit must be named in exactly one manifest.
file(GLOB_RECURSE BRICK_FIRST_PARTY_INVENTORY CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.s" "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/*.cpp")
list(FILTER BRICK_FIRST_PARTY_INVENTORY EXCLUDE REGEX "/Generated/")

set(BRICK_DOMAIN_UNION ${DOMAIN_CONTROL} ${DOMAIN_AUDIO} ${DOMAIN_CONTRACTS} ${PLATFORM_H743})
list(LENGTH BRICK_DOMAIN_UNION BRICK_DOMAIN_ENTRY_COUNT)
set(BRICK_DOMAIN_UNIQUE ${BRICK_DOMAIN_UNION})
list(REMOVE_DUPLICATES BRICK_DOMAIN_UNIQUE)
list(LENGTH BRICK_DOMAIN_UNIQUE BRICK_DOMAIN_UNIQUE_COUNT)
if(NOT BRICK_DOMAIN_ENTRY_COUNT EQUAL BRICK_DOMAIN_UNIQUE_COUNT)
    message(FATAL_ERROR "First-party source classified in more than one execution domain")
endif()
set(BRICK_UNCLASSIFIED ${BRICK_FIRST_PARTY_INVENTORY})
list(REMOVE_ITEM BRICK_UNCLASSIFIED ${BRICK_DOMAIN_UNIQUE})
set(BRICK_STALE_MANIFEST ${BRICK_DOMAIN_UNIQUE})
list(REMOVE_ITEM BRICK_STALE_MANIFEST ${BRICK_FIRST_PARTY_INVENTORY})
if(BRICK_UNCLASSIFIED)
    message(FATAL_ERROR "Unclassified first-party sources: ${BRICK_UNCLASSIFIED}")
endif()
if(BRICK_STALE_MANIFEST)
    message(FATAL_ERROR "Manifest entries do not exist in first-party inventory: ${BRICK_STALE_MANIFEST}")
endif()
foreach(platform_source IN LISTS PLATFORM_H743)
    if(platform_source MATCHES "/Src/IPC/")
        message(FATAL_ERROR "Src/IPC unit classified as PLATFORM_H743: ${platform_source}")
    endif()
endforeach()

set(BRICK_TEST_ONLY_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Platform/crash_capsule.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Platform/diagnostic_watchdog.c")
set(PLATFORM_H743_BUILD ${PLATFORM_H743_COMMON})
if(BRICK6_VARIANT STREQUAL "premium")
    list(APPEND PLATFORM_H743_BUILD ${PLATFORM_H743_PREMIUM})
else()
    list(APPEND PLATFORM_H743_BUILD ${PLATFORM_H743_LOWCOST})
endif()
if(NOT BRICK_TEST_BUILD)
    list(REMOVE_ITEM PLATFORM_H743_BUILD ${BRICK_TEST_ONLY_SOURCES})
endif()

set(DOMAIN_CONTROL_BUILD ${DOMAIN_CONTROL} ${USB_DEVICE_LIB_SRC} ${USB_HOST_LIB_SRC}
    ${FATFS_SRC} ${U8G2_SRC})
set(DOMAIN_AUDIO_BUILD ${DOMAIN_AUDIO} ${CMSIS_DSP_SRC} ${MUTABLE_INSTRUMENTS_SRC})
set(ALL_USER_SOURCES ${DOMAIN_CONTROL_BUILD} ${DOMAIN_AUDIO_BUILD}
    ${DOMAIN_CONTRACTS} ${PLATFORM_H743_BUILD})
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${ALL_USER_SOURCES})
source_group("Domains/CONTROL" FILES ${DOMAIN_CONTROL})
source_group("Domains/AUDIO" FILES ${DOMAIN_AUDIO})
source_group("Domains/CONTRACTS" FILES ${DOMAIN_CONTRACTS} ${DOMAIN_CONTRACT_HEADERS})
source_group("Domains/PLATFORM_H743" FILES ${PLATFORM_H743})

# CONTROL is compiled for its future Cortex-M4 ISA and its transitive include
# graph is rejected when it reaches AUDIO-private implementation surfaces.
set(DOMAIN_CONTROL_CM4 ${DOMAIN_CONTROL_BUILD})
list(FILTER DOMAIN_CONTROL_CM4 INCLUDE REGEX ".*\\.(c|cc|cpp|cxx)$")

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
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/MIDI
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Param
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Storage
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/UI
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/UI
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

set(CONTROL_CM4_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/App"
    "${BRICK6_VARIANT_USB_INC_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/Third_Party/FatFs/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/App"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Board"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/IPC"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Keyboard"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/MIDI"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Mod"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/NoteFx"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Param"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Platform"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/SD"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Seq"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Storage"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Track"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/UI"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/UI"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Inc"
    ${BRICK6_VARIANT_DRIVER_INC_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/U8g2"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx/Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/tinyusb"
)

if(BRICK6_VARIANT STREQUAL "premium")
    set(CONTROL_CM4_VARIANT_DEFINE BRICK6_VARIANT_PREMIUM)
    set(CONTROL_CM4_GENERATED_INC
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/Premium/Generated/Inc")
else()
    set(CONTROL_CM4_VARIANT_DEFINE BRICK6_VARIANT_LOWCOST)
    set(CONTROL_CM4_GENERATED_INC
        "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Generated/Inc")
endif()
list(APPEND CONTROL_CM4_INCLUDE_DIRS "${CONTROL_CM4_GENERATED_INC}")

set(CONTROL_CM4_CONTRACT_HEADERS ${DOMAIN_CONTRACT_HEADERS} ${DOMAIN_CONTRACT_UNITS})

set(CONTROL_CM4_MANIFEST
    "${CMAKE_CURRENT_BINARY_DIR}/control_cm4_compile_manifest.cmake")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/control_cm4_compile_manifest.cmake.in"
    "${CONTROL_CM4_MANIFEST}"
    @ONLY)

add_custom_target(control_cm4_compile_check
    COMMAND "${CMAKE_COMMAND}"
        -DMANIFEST=${CONTROL_CM4_MANIFEST}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/control_cm4_compile_check.cmake"
    SOURCES ${DOMAIN_CONTROL_CM4} ${DOMAIN_CONTRACTS}
    COMMENT "Compile-check DOMAIN_CONTROL for Cortex-M4"
    VERBATIM)

# The symmetric M7 check compiles AUDIO-owned BRICK units only, then audits
# compiler-produced dependency files against CONTROL-private surfaces.
set(DOMAIN_AUDIO_M7 ${DOMAIN_AUDIO})
list(FILTER DOMAIN_AUDIO_M7 INCLUDE REGEX ".*\\.(c|cc|cpp|cxx)$")
set(AUDIO_M7_INCLUDE_DIRS ${CONTROL_CM4_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Audio"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Audio/Engines"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS_DSP/Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS_DSP/PrivateInclude"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Daisy_SP/Source/Utility"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments"
    "${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/stmlib")
set(AUDIO_M7_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/audio_m7_compile_manifest.cmake")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/audio_m7_compile_manifest.cmake.in"
    "${AUDIO_M7_MANIFEST}" @ONLY)
add_custom_target(audio_m7_compile_check
    COMMAND "${CMAKE_COMMAND}" -DMANIFEST=${AUDIO_M7_MANIFEST}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/audio_m7_compile_check.cmake"
    SOURCES ${DOMAIN_AUDIO_M7}
    COMMENT "Compile-check DOMAIN_AUDIO for Cortex-M7"
    VERBATIM)


# --------------------------------------------------------------------------
# Defines
# --------------------------------------------------------------------------
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USE_PWR_LDO_SUPPLY
    USE_HAL_DRIVER
    STM32H743xx
    BRICK_TEST_BUILD=$<BOOL:${BRICK_TEST_BUILD}>
    BRICK6_STREAM_PRODUCT_PAGE_KIB=${BRICK6_STREAM_PRODUCT_PAGE_KIB}
    BRICK6_STREAM_PRODUCT_MULTI_PRESOCLE_PAGES=${BRICK6_STREAM_PRODUCT_MULTI_PRESOCLE_PAGES}
    BRICK6_STREAM_PRODUCT_MULTI_MOBILE_PAGES=${BRICK6_STREAM_PRODUCT_MULTI_MOBILE_PAGES}
    BRICK6_STREAM_PRODUCT_VOICE_LOOP_CACHE_PAGES=${BRICK6_STREAM_PRODUCT_VOICE_LOOP_CACHE_PAGES}
    BRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST=${BRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST}
    BRICK6_STREAM_READ_CHUNK_KIB=${BRICK6_STREAM_EFFECTIVE_READ_CHUNK_KIB}
)

# Keep the runtime-engine marker in the ELF even with LTO and section GC.
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    "-Wl,--undefined=g_fx_audio_lofi_engine_build_id")

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    FM_KERNEL_BENCH=${FM_KERNEL_BENCH}
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

    $<$<CONFIG:Debug>:-Og>
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
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/Engines/prism_engine.cpp"
)
set_source_files_properties(${BRAIDS_O3_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O3>;$<${BRICK6_RELEASE_LIKE_CONFIG}:-flto>"
)

# Explicit optimization exceptions do not affect execution-domain ownership.
set(CONTROL_RUNTIME_O1_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/brick6_clip_shifter.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/live_parameter_audio_runtime.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Storage/brick6_stream_service_task.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Storage/project_control.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Storage/project_load_quiesce.c"
)

# Common audio engine: keep LTO scoped to AUDIO-owned DSP and synth runtime.
set(AUDIO_DSP_O3_LTO_SOURCES ${DOMAIN_AUDIO})
list(FILTER AUDIO_DSP_O3_LTO_SOURCES INCLUDE REGEX ".*\\.(c|cc|cpp|cxx)$")
list(REMOVE_ITEM AUDIO_DSP_O3_LTO_SOURCES ${CONTROL_RUNTIME_O1_SOURCES})

set(AUDIO_LTO_SOURCES
    ${AUDIO_DSP_O3_LTO_SOURCES} ${CMSIS_DSP_SRC} ${MUTABLE_INSTRUMENTS_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/Engines/fm_dx7_log_kernel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/Engines/fm_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/Engines/audio_engine_dispatch.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/Engines/prism_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/Engines/stack_engine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/Engines/wavetable_engine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/Engines/Sampler/brick6_sampler_runtime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/brick6_looper_runtime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Track/synth_polyphony.c
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
    ${AUDIO_DSP_O3_LTO_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/Engines/fm_dx7_log_kernel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/Engines/fm_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/Engines/audio_engine_dispatch.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/Engines/prism_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/Engines/stack_engine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/Engines/wavetable_engine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/Engines/Sampler/brick6_sampler_runtime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/brick6_looper_runtime.c
    ${MUTABLE_INSTRUMENTS_SRC}
)
set_source_files_properties(${AUDIO_O3_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O3>;$<${BRICK6_RELEASE_LIKE_CONFIG}:-flto>"
)
set_property(SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/brick6_looper_runtime.c"
    APPEND PROPERTY COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-g3>"
)

set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/mixer.c"
    PROPERTIES COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O3>;$<${BRICK6_RELEASE_LIKE_CONFIG}:-flto>"
)

set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Audio/Engines/audio_engine_dispatch.c"
    PROPERTIES COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O3>;$<${BRICK6_RELEASE_LIKE_CONFIG}:-flto>"
)

# GCC 13.3 LTO can emit a corrupted .init_array relocation for the sole
# non-trivial global C++ initializer in the diagnostic image. Keep this TU at
# Release optimization while making its constructor relocation ordinary.
set_source_files_properties(${CMSIS_DSP_SRC} PROPERTIES
    COMPILE_DEFINITIONS "ARM_MATH_CM7;ARM_MATH_LOOPUNROLL"
)

# Hot sampler render code reached from the common audio block.
# Keep sample_cache/sample_stream_manager and the other Sampler sources out:
# they also own SD/FatFs, refill, page scheduling or asynchronous state.
set(SAMPLE_RENDER_HOT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Sampler/sample_voice_reader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Sampler/sample_play_plan.c"
)
set_source_files_properties(${SAMPLE_RENDER_HOT_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O3>;$<${BRICK6_RELEASE_LIKE_CONFIG}:-flto>"
)

# Storage I/O/persistence implementation in O0, excluding CONTROL/runtime
# orchestration hosted physically by Storage.
set(STORAGE_IO_O0_SOURCES ${DOMAIN_CONTROL})
list(FILTER STORAGE_IO_O0_SOURCES INCLUDE REGEX "/Src/Storage/")
list(FILTER STORAGE_IO_O0_SOURCES INCLUDE REGEX ".*\\.(c|cc|cpp|cxx)$")
list(REMOVE_ITEM STORAGE_IO_O0_SOURCES ${CONTROL_RUNTIME_O1_SOURCES})

set_source_files_properties(${STORAGE_IO_O0_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O0>"
)

set_source_files_properties(${CONTROL_RUNTIME_O1_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O1>"
)

# U8g2 en O0
set_source_files_properties(${U8G2_SRC} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O0>"
)

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
    $<${BRICK6_RELEASE_LIKE_CONFIG}:-g3>
)

# C++ linker because project contains cpp files
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

# Remove wrong implicit lib if present
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Keep CubeMX link style consistent
target_link_libraries(${CMAKE_PROJECT_NAME} stm32cubemx)
