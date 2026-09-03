cmake_policy(SET CMP0057 NEW)

if(NOT DEFINED MANIFEST)
    message(FATAL_ERROR "control_cm4_compile_check: MANIFEST is required")
endif()

include("${MANIFEST}")
file(MAKE_DIRECTORY "${CONTROL_CM4_WORK_DIR}")

set(include_args "")
foreach(include_dir IN LISTS CONTROL_CM4_INCLUDE_DIRS)
    list(APPEND include_args "-I${include_dir}")
endforeach()

set(definition_args
    # The repository currently vendors only the H743 device register header.
    # The compiler ISA/FPU below are nevertheless the real future CM4 ones;
    # H747 device headers arrive with the future platform image, not this check.
    -DSTM32H743xx
    -DARM_MATH_CM4
    -DUSE_HAL_DRIVER
    -DUSE_PWR_LDO_SUPPLY
    "-DBRICK6_STREAM_PRODUCT_PAGE_KIB=${CONTROL_CM4_PRODUCT_PAGE_KIB}"
    "-DBRICK6_STREAM_PRODUCT_MULTI_PRESOCLE_PAGES=${CONTROL_CM4_PRODUCT_MULTI_PRESOCLE_PAGES}"
    "-DBRICK6_STREAM_PRODUCT_MULTI_MOBILE_PAGES=${CONTROL_CM4_PRODUCT_MULTI_MOBILE_PAGES}"
    "-DBRICK6_STREAM_PRODUCT_VOICE_LOOP_CACHE_PAGES=${CONTROL_CM4_PRODUCT_VOICE_LOOP_CACHE_PAGES}"
    "-DBRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST=${CONTROL_CM4_PRODUCT_MULTI_CHANNEL_COST}"
    "-DBRICK6_STREAM_READ_CHUNK_KIB=${CONTROL_CM4_STREAM_READ_CHUNK_KIB}")

set(common_args
    -mcpu=cortex-m4
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
    -mthumb
    -ffunction-sections
    -fdata-sections
    -Wall
    -Werror=implicit-function-declaration
    --specs=nano.specs)

set(control_forbidden_dependencies
    "/Inc/Audio/"
    "/Src/Audio/"
    "/mutable_instruments/"
    "/Inspiration/")

set(storage_forbidden_dependencies
    # STORAGE may consume the shared Recorder data plane, but never AUDIO
    # implementation details, Looper/DSP/track business state or callbacks.
    "/Inc/Audio/"
    "/Src/Audio/"
    "/Inc/Sampler/sample_voice_reader.h"
    "/Src/Sampler/VoiceReader/"
    "/Inc/Track/"
    "/Src/Track/")

set(contract_forbidden_dependencies
    "/Inc/Audio/"
    "/Src/Audio/"
    "/Inc/App/"
    "/Inc/Storage/"
    "/Inc/UI/"
    "/Inc/Keyboard/"
    "/Inc/MIDI/"
    "/Inc/NoteFx/"
    "/Inc/Track/track_runtime.h"
    "/Inc/Track/track_state.h"
    "/Inc/Param/param_registry.h"
    "/Inc/Param/param_global_control.h"
    "/Inc/Sampler/sample_page_cache_audio.h")

set(compiled_count 0)
set(all_sources ${CONTROL_CM4_SOURCES} ${CONTROL_CM4_CONTRACT_SOURCES}
    ${CONTROL_CM4_SHARED_SOURCES})
set(object_manifest "")
foreach(source IN LISTS all_sources)
    get_filename_component(extension "${source}" EXT)
    if(extension STREQUAL ".cpp" OR extension STREQUAL ".cc" OR extension STREQUAL ".cxx")
        set(compiler "${CONTROL_CM4_CXX_COMPILER}")
        set(language_args -std=gnu++14 -fno-rtti -fno-exceptions -fno-threadsafe-statics)
    else()
        set(compiler "${CONTROL_CM4_C_COMPILER}")
        set(language_args -std=gnu11)
    endif()

    string(SHA1 source_id "${source}")
    set(depfile "${CONTROL_CM4_WORK_DIR}/${source_id}.d")
    set(object "${CONTROL_CM4_WORK_DIR}/${source_id}.o")
    execute_process(
        COMMAND "${compiler}"
            ${common_args}
            ${language_args}
            ${definition_args}
            ${include_args}
            -MMD -MF "${depfile}" -MT control_cm4_check
            -c -o "${object}"
            "${source}"
        RESULT_VARIABLE compile_result
        OUTPUT_VARIABLE compile_stdout
        ERROR_VARIABLE compile_stderr)
    if(NOT compile_result EQUAL 0)
        message(FATAL_ERROR
            "Cortex-M4 compile failed for ${source}:\n${compile_stdout}${compile_stderr}")
    endif()
    if(source IN_LIST CONTROL_CM4_SHARED_SOURCES)
        set(owner SHARED_BACKING)
    elseif(source IN_LIST CONTROL_CM4_CONTRACT_SOURCES)
        set(owner CONTRACTS)
    elseif(source IN_LIST CONTROL_CM4_STORAGE_SOURCES)
        set(owner STORAGE)
    else()
        set(owner CONTROL)
    endif()
    string(APPEND object_manifest "${owner}|${object}|${source}\n")

    file(READ "${depfile}" dependencies)
    string(REPLACE "\\" "/" dependencies "${dependencies}")
    foreach(forbidden IN LISTS control_forbidden_dependencies)
        string(FIND "${dependencies}" "${forbidden}" forbidden_at)
        if(NOT forbidden_at EQUAL -1)
            message(FATAL_ERROR
                "DOMAIN_CONTROL dependency firewall: ${source} reaches ${forbidden}")
        endif()
    endforeach()
    if(source IN_LIST CONTROL_CM4_STORAGE_SOURCES)
        foreach(forbidden IN LISTS storage_forbidden_dependencies)
            string(FIND "${dependencies}" "${forbidden}" forbidden_at)
            if(NOT forbidden_at EQUAL -1)
                message(FATAL_ERROR
                    "DOMAIN_STORAGE dependency firewall: ${source} reaches ${forbidden}")
            endif()
        endforeach()
    endif()
    math(EXPR compiled_count "${compiled_count} + 1")
endforeach()
file(WRITE "${CONTROL_CM4_WORK_DIR}/objects.manifest" "${object_manifest}")

foreach(contract_header IN LISTS CONTROL_CM4_CONTRACT_HEADERS)
    string(SHA1 contract_id "${contract_header}")
    set(depfile "${CONTROL_CM4_WORK_DIR}/contract_${contract_id}.d")
    execute_process(
        COMMAND "${CONTROL_CM4_C_COMPILER}"
            ${common_args}
            -fsyntax-only
            -std=gnu11
            ${definition_args}
            ${include_args}
            -x c -MMD -MF "${depfile}" -MT control_cm4_contract_check
            "${contract_header}"
        RESULT_VARIABLE compile_result
        OUTPUT_VARIABLE compile_stdout
        ERROR_VARIABLE compile_stderr)
    if(NOT compile_result EQUAL 0)
        message(FATAL_ERROR
            "Cortex-M4 contract compile failed for ${contract_header}:\n${compile_stdout}${compile_stderr}")
    endif()

    file(READ "${depfile}" dependencies)
    string(REPLACE "\\" "/" dependencies "${dependencies}")
    foreach(forbidden IN LISTS contract_forbidden_dependencies)
        string(FIND "${dependencies}" "${forbidden}" forbidden_at)
        if(NOT forbidden_at EQUAL -1)
            message(FATAL_ERROR
                "DOMAIN_CONTRACTS dependency firewall: ${contract_header} reaches ${forbidden}")
        endif()
    endforeach()
endforeach()

message(STATUS
    "Cortex-M4 CONTROL/STORAGE compile-check passed (${compiled_count} translation units)")
