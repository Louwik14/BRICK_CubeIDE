cmake_minimum_required(VERSION 3.22)

# --------------------------------------------------------------------------
# Project
# --------------------------------------------------------------------------
set(CMAKE_PROJECT_NAME BRICK6_CUBE)

project(${CMAKE_PROJECT_NAME} LANGUAGES C CXX ASM)

message("Build type: ${CMAKE_BUILD_TYPE}")

set(BRICK6_STREAM_READ_CHUNK_KIB "32" CACHE STRING
    "Streamer application read quantum in KiB")
if(NOT BRICK6_STREAM_READ_CHUNK_KIB MATCHES "^(4|8|16|32)$")
    message(FATAL_ERROR "BRICK6_STREAM_READ_CHUNK_KIB must be 4, 8, 16 or 32")
endif()

# Common sampler/streamer product contract. Board variants only adapt hardware.
set(BRICK6_STREAM_PRODUCT_PAGE_KIB 16)
set(BRICK6_STREAM_PRODUCT_READ_CHUNK_KIB 32)
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
option(BRICK_PATTERN_RECALL_DIAG
    "Exhaustive PreparedAudio Pattern Recall diagnostics" ON)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    BRICK_PATTERN_RECALL_DIAG=$<BOOL:${BRICK_PATTERN_RECALL_DIAG}>)
option(BRICK_PERF_DIAG "Instrument the H743 Streamer/Recorder baseline" ON)
if(BRICK_PERF_DIAG)
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
        "LINKER:--undefined=brick_perf_diag_reset"
        "LINKER:--undefined=brick_perf_diag_snapshot")
else()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE BRICK_PERF_DIAG=0)
endif()

# CubeMX-generated base
add_subdirectory(cmake/stm32cubemx)

# --------------------------------------------------------------------------
# User sources
# --------------------------------------------------------------------------

# App / middleware
set(BRICK6_BOARD_USB_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/UsbStack")

set(TINYUSB_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/tusb.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/common/tusb_fifo.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/device/usbd.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/class/audio/audio_device.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/class/midi/midi_device.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/host/usbh.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/host/hub.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/class/midi/midi_host.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/portable/synopsys/dwc2/hcd_dwc2.c"
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

set(BRICK6_BOARD_DRIVER_INC_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Drivers"
    "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Src"
    "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/UsbStack")

# --------------------------------------------------------------------------
# Explicit BRICK execution-domain manifests
# --------------------------------------------------------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/brick_domain_manifest.cmake")

set(DOMAIN_CONTRACT_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Seq/seq_audio_boundary.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Track/audio_fx_xfade_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Platform/crash_library.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Platform/boot_diag.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Platform/brick_fatal.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Param/param_ids.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Param/param_spec.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Mod/mod_lfo_types.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Mod/mod_destination_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Recorder/audio_rec_bus_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Audio/Publications/audio_rec_level_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Audio/Publications/audio_boot_diagnostic.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Audio/Publications/audio_boot_diagnostic_layout.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Audio/Publications/audio_waveform_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/ControlRT/fm_dsp_projection.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/ControlRT/patch_preview_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/audio_wave_table_projection.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/audio_wavetable_registry_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/ControlRT/control_audio_command.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/ControlRT/control_audio_fifo_layout.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/ControlRT/pattern_recall_diag.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/ControlRT/live_parameter_event.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/multi_sample_audio_projection.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/multi_sample_audio_projection_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sample_classic_audio_projection_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/rec_source_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sampler_ram_audio_projection.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sampler_ram_audio_projection_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sampler_ram_playhead_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Board/usb_audio_float_ring.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Audio/Publications/synth_waveform_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Storage/sd_preview_ring_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sample_audio_format.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/wavetable_config.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sample_audio_key.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/multi_sample_config.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sample_play_plan.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sample_reader_contract.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sample_page_cache_config.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc/Sampler/sample_page_cache_contract.h"
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
    "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/*.cpp")
list(FILTER BRICK_FIRST_PARTY_INVENTORY EXCLUDE REGEX "/Generated/")

set(BRICK_DOMAIN_UNION ${DOMAIN_CONTROL} ${DOMAIN_STORAGE} ${DOMAIN_AUDIO}
    ${DOMAIN_CONTRACTS} ${PLATFORM_H743})
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
set(PLATFORM_H743_BUILD ${PLATFORM_H743_COMMON} ${PLATFORM_H743_LOWCOST} ${TINYUSB_SRC})
set(DOMAIN_CONTROL_BUILD ${DOMAIN_CONTROL}
    ${FATFS_SRC} ${U8G2_SRC})
set(DOMAIN_STORAGE_BUILD ${DOMAIN_STORAGE})
set(DOMAIN_AUDIO_BUILD ${DOMAIN_AUDIO} ${CMSIS_DSP_SRC} ${MUTABLE_INSTRUMENTS_SRC})
set(ALL_USER_SOURCES ${DOMAIN_CONTROL_BUILD} ${DOMAIN_STORAGE_BUILD}
    ${DOMAIN_AUDIO_BUILD} ${DOMAIN_CONTRACTS} ${PLATFORM_H743_BUILD})
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${ALL_USER_SOURCES})
source_group("Domains/CONTROL" FILES ${DOMAIN_CONTROL})
source_group("Domains/STORAGE" FILES ${DOMAIN_STORAGE})
source_group("Domains/AUDIO" FILES ${DOMAIN_AUDIO})
source_group("Domains/CONTRACTS" FILES ${DOMAIN_CONTRACTS} ${DOMAIN_CONTRACT_HEADERS})
source_group("Domains/PLATFORM_H743" FILES ${PLATFORM_H743})

# --------------------------------------------------------------------------
# Include directories
# --------------------------------------------------------------------------
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/App
    ${BRICK6_BOARD_USB_INC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Middlewares/Third_Party/FatFs/src

    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Board
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Audio
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/MIDI
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Param
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Storage
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/UI
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/UI
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/SD
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Drv_app/Inc
    ${BRICK6_BOARD_DRIVER_INC_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/U8g2
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS_DSP/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS_DSP/PrivateInclude
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Daisy_SP/Source/Utility

    ${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments
    ${CMAKE_CURRENT_SOURCE_DIR}/mutable_instruments/stmlib
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src
)

# The domain firewall consumes dependency information emitted by this normal
# M7 build.  It performs no secondary compilation and creates no extra objects.
set(DOMAIN_FIREWALL_MANIFEST
    "${CMAKE_CURRENT_BINARY_DIR}/domain_dependency_manifest.cmake")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/domain_dependency_manifest.cmake.in"
    "${DOMAIN_FIREWALL_MANIFEST}"
    @ONLY)
add_custom_target(domain_dependency_check
    COMMAND "${CMAKE_COMMAND}"
        -DMANIFEST=${DOMAIN_FIREWALL_MANIFEST}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/domain_dependency_check.cmake"
    DEPENDS ${CMAKE_PROJECT_NAME}
    COMMENT "Check M7 domain dependencies"
    VERBATIM)

# --------------------------------------------------------------------------
# Defines
# --------------------------------------------------------------------------
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USE_PWR_LDO_SUPPLY
    USE_HAL_DRIVER
    STM32H743xx
    BRICK6_STREAM_PRODUCT_PAGE_KIB=${BRICK6_STREAM_PRODUCT_PAGE_KIB}
    BRICK6_STREAM_READ_CHUNK_KIB=${BRICK6_STREAM_EFFECTIVE_READ_CHUNK_KIB}
)

# Keep the runtime-engine marker in the ELF even with LTO and section GC.
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    "-Wl,--undefined=g_fx_audio_lofi_engine_build_id")

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

# Release optimization is assigned once per source.
set(BRICK_OWNED_SOURCES ${BRICK_DOMAIN_UNIQUE})
list(FILTER BRICK_OWNED_SOURCES INCLUDE REGEX ".*\.(c|cc|cpp|cxx)$")
set_source_files_properties(${BRICK_OWNED_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O2>;$<${BRICK6_RELEASE_LIKE_CONFIG}:-flto>")

# Audio kernels and sampler render loops retain their intentional O3 policy.
set(BRICK_AUDIO_O3_SOURCES ${DOMAIN_AUDIO})
list(FILTER BRICK_AUDIO_O3_SOURCES INCLUDE REGEX ".*\.(c|cc|cpp|cxx)$")
list(APPEND BRICK_AUDIO_O3_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Sampler/sample_voice_reader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Sampler/sample_play_plan.c")
set_source_files_properties(${BRICK_AUDIO_O3_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O3>;$<${BRICK6_RELEASE_LIKE_CONFIG}:-flto>")

set(BRICK_VENDOR_O3_SOURCES ${MUTABLE_INSTRUMENTS_SRC} ${CMSIS_DSP_SRC})
set_source_files_properties(${BRICK_VENDOR_O3_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O3>;$<${BRICK6_RELEASE_LIKE_CONFIG}:-flto>")
set_source_files_properties(${CMSIS_DSP_SRC} PROPERTIES
    COMPILE_DEFINITIONS "ARM_MATH_CM7;ARM_MATH_LOOPUNROLL")

# Generated and third-party sources retain size optimization without LTO.
get_target_property(BRICK_ALL_TARGET_SOURCES ${CMAKE_PROJECT_NAME} SOURCES)
set(BRICK_VENDOR_SOURCES ${BRICK_ALL_TARGET_SOURCES})
list(REMOVE_ITEM BRICK_VENDOR_SOURCES ${BRICK_OWNED_SOURCES} ${BRICK_VENDOR_O3_SOURCES})
list(FILTER BRICK_VENDOR_SOURCES INCLUDE REGEX ".*\.(c|cc|cpp|cxx)$")
set_source_files_properties(${BRICK_VENDOR_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-Os>")
set_source_files_properties(${U8G2_SRC} PROPERTIES
    COMPILE_OPTIONS "$<${BRICK6_RELEASE_LIKE_CONFIG}:-O2>")
target_compile_options(STM32_Drivers PRIVATE "$<${BRICK6_RELEASE_LIKE_CONFIG}:-Os>")
# --------------------------------------------------------------------------
# Link options aligned with CubeIDE
# --------------------------------------------------------------------------
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -mcpu=cortex-m7
    -mfpu=fpv5-d16
    -mfloat-abi=hard
    -mthumb
    -T${CMAKE_CURRENT_SOURCE_DIR}/Board/LowCost/Generated/Linker/STM32H743IITX_FLASH.ld
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

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.bin
    COMMENT "Generating ${CMAKE_PROJECT_NAME}.bin"
)

# Remove wrong implicit lib if present
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Keep CubeMX link style consistent
target_link_libraries(${CMAKE_PROJECT_NAME} stm32cubemx)
