if(NOT DEFINED MANIFEST)
    message(FATAL_ERROR "audio_m7_compile_check: MANIFEST is required")
endif()

include("${MANIFEST}")
file(MAKE_DIRECTORY "${AUDIO_M7_WORK_DIR}")

set(include_args "")
foreach(include_dir IN LISTS AUDIO_M7_INCLUDE_DIRS)
    list(APPEND include_args "-I${include_dir}")
endforeach()

set(definition_args
    # The repository currently vendors only the H743 device register header.
    # The compiler ISA/FPU below are nevertheless the Cortex-M7 ones;
    # H747 device headers arrive with the platform image, not this check.
    -DSTM32H743xx
    -DARM_MATH_CM7
    -DUSE_HAL_DRIVER
    -DUSE_PWR_LDO_SUPPLY
    -DBRICK_TEST_BUILD=0
    "-D${AUDIO_M7_VARIANT_DEFINE}"
    "-DBRICK6_STREAM_PRODUCT_PAGE_KIB=${AUDIO_M7_PRODUCT_PAGE_KIB}"
    "-DBRICK6_STREAM_PRODUCT_MULTI_PRESOCLE_PAGES=${AUDIO_M7_PRODUCT_MULTI_PRESOCLE_PAGES}"
    "-DBRICK6_STREAM_PRODUCT_MULTI_MOBILE_PAGES=${AUDIO_M7_PRODUCT_MULTI_MOBILE_PAGES}"
    "-DBRICK6_STREAM_PRODUCT_VOICE_LOOP_CACHE_PAGES=${AUDIO_M7_PRODUCT_VOICE_LOOP_CACHE_PAGES}"
    "-DBRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST=${AUDIO_M7_PRODUCT_MULTI_CHANNEL_COST}"
    "-DBRICK6_STREAM_READ_CHUNK_KIB=${AUDIO_M7_STREAM_READ_CHUNK_KIB}")

set(common_args
    -mcpu=cortex-m7
    -mfpu=fpv5-d16
    -mfloat-abi=hard
    -mthumb
    -ffunction-sections
    -fdata-sections
    -Wall
    -Werror=implicit-function-declaration
    --specs=nano.specs
    -fsyntax-only)

set(audio_forbidden_dependencies
    "/Inspiration/"
    "/Inc/App/"
    "/Src/App/"
    "/Inc/Storage/"
    "/Src/Storage/"
    "/Inc/UI/"
    "/Src/UI/"
    "/Inc/Track/track_runtime.h"
    "/Inc/Track/entity_topology.h"
    "/Inc/Param/param_store.h"
    "/Inc/Param/param_registry_control.h"
    "/Inc/Mod/mod_matrix_control.h"
    "/Inc/Mod/mod_destination_catalog_control.h"
    "/Inc/Sampler/sample_global_pool.h"
    "/Inc/Sampler/multi_sample_pool.h"
    "/Inc/Sampler/sampler_ram_pool.h"
    "/Inc/Sampler/wavetable_pool.h"
    "_control.h")

set(compiled_count 0)
foreach(source IN LISTS AUDIO_M7_SOURCES)
    get_filename_component(extension "${source}" EXT)
    if(extension STREQUAL ".cpp" OR extension STREQUAL ".cc" OR extension STREQUAL ".cxx")
        set(compiler "${AUDIO_M7_CXX_COMPILER}")
        set(language_args -std=gnu++14 -fno-rtti -fno-exceptions -fno-threadsafe-statics)
    else()
        set(compiler "${AUDIO_M7_C_COMPILER}")
        set(language_args -std=gnu11)
    endif()

    string(SHA1 source_id "${source}")
    set(depfile "${AUDIO_M7_WORK_DIR}/${source_id}.d")
    execute_process(
        COMMAND "${compiler}"
            ${common_args}
            ${language_args}
            ${definition_args}
            ${include_args}
            -MMD -MF "${depfile}" -MT audio_m7_check
            "${source}"
        RESULT_VARIABLE compile_result
        OUTPUT_VARIABLE compile_stdout
        ERROR_VARIABLE compile_stderr)
    if(NOT compile_result EQUAL 0)
        message(FATAL_ERROR
            "Cortex-M7 compile failed for ${source}:\n${compile_stdout}${compile_stderr}")
    endif()

    file(READ "${depfile}" dependencies)
    string(REPLACE "\\" "/" dependencies "${dependencies}")
    foreach(forbidden IN LISTS audio_forbidden_dependencies)
        string(FIND "${dependencies}" "${forbidden}" forbidden_at)
        if(NOT forbidden_at EQUAL -1)
            message(FATAL_ERROR
                "DOMAIN_AUDIO dependency firewall: ${source} reaches ${forbidden}")
        endif()
    endforeach()
    math(EXPR compiled_count "${compiled_count} + 1")
endforeach()

message(STATUS
    "Cortex-M7 AUDIO compile-check passed (${compiled_count} translation units)")
