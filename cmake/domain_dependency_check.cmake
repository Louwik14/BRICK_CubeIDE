cmake_policy(SET CMP0057 NEW)

if(NOT DEFINED MANIFEST)
    message(FATAL_ERROR "domain_dependency_check: MANIFEST is required")
endif()
include("${MANIFEST}")

execute_process(
    COMMAND "${DOMAIN_FIREWALL_BUILD_TOOL}" -C "${DOMAIN_FIREWALL_BUILD_DIR}" -t deps
    RESULT_VARIABLE deps_result
    OUTPUT_VARIABLE deps_output
    ERROR_VARIABLE deps_error)
if(NOT deps_result EQUAL 0)
    message(FATAL_ERROR "Unable to read M7 dependencies: ${deps_error}")
endif()

set(control_forbidden "/Inc/Audio/" "/Src/Audio/" "/mutable_instruments/" "/Inspiration/")
set(storage_forbidden
    "/Inc/Audio/" "/Src/Audio/" "/Inc/Sampler/sample_voice_reader.h"
    "/Src/Sampler/VoiceReader/" "/Inc/Track/track_runtime.h"
    "/Inc/Track/track_state.h" "/Inc/Track/entity_topology.h" "/Src/Track/")
set(audio_forbidden
    "/Inc/App/" "/Src/App/" "/Inc/Storage/" "/Src/Storage/"
    "/Inc/UI/" "/Src/UI/" "/Inc/Track/track_runtime.h"
    "/Inc/Track/entity_topology.h" "/Inc/Param/param_global_control.h"
    "/Inc/Param/param_registry_control.h" "/Inc/Mod/mod_matrix_control.h"
    "/Inc/Mod/mod_destination_catalog_control.h" "/Inc/SD/"
    "/App/Middlewares/Third_Party/FatFs/" "_control.h")
set(contract_forbidden
    "/Inc/Audio/" "/Src/Audio/" "/Inc/App/" "/Inc/Storage/"
    "/Inc/UI/" "/Inc/Keyboard/" "/Inc/MIDI/" "/Inc/NoteFx/"
    "/Inc/Track/track_runtime.h" "/Inc/Track/track_state.h"
    "/Inc/Param/param_registry.h" "/Inc/Param/param_global_control.h"
    "/Inc/Sampler/sample_page_cache_audio.h")

# These units are explicit domain adapters or product-lifecycle entry points.
# Their cross-domain includes are part of their ownership contract.
set(control_adapters
    "/Src/App/brick6_app_init.c" "/Src/App/power_shutdown.c"
    "/Src/Param/param_registry_backends.c"
    "/Src/Param/param_registry_tone_backends.c"
    "/Src/Track/audio_fx_control_state.c"
    "/Src/UI/pages/ui_page_midi_fx.c")
set(audio_adapters
    "/Src/Audio/audio_command_executor.c"
    "/Src/Audio/audio_note_engine_adapter.c"
    "/Src/Audio/audio_rec_overdub.c"
    "/Src/Audio/audio_recorder_capture_audio.c"
    "/Src/Mod/mod_destination_catalog.c")

function(source_object_key source output)
    file(RELATIVE_PATH relative "${DOMAIN_FIREWALL_SOURCE_DIR}" "${source}")
    string(REPLACE "\\" "/" relative "${relative}")
    set(${output} "CMakeFiles/${DOMAIN_FIREWALL_TARGET}.dir/${relative}.obj" PARENT_SCOPE)
endfunction()

set(records "")
foreach(owner CONTROL STORAGE AUDIO CONTRACTS)
    foreach(source IN LISTS DOMAIN_FIREWALL_${owner}_SOURCES)
        get_filename_component(extension "${source}" EXT)
        if(extension MATCHES "^\\.(c|cc|cpp|cxx)$")
            source_object_key("${source}" object)
            list(APPEND records "${object}|${owner}|${source}")
        endif()
    endforeach()
endforeach()

string(REPLACE "\r\n" "\n" deps_output "${deps_output}")
string(REPLACE "\n" ";" deps_lines "${deps_output}")
set(current_owner "")
set(current_source "")
set(checked_units "")
set(violations "")

foreach(line IN LISTS deps_lines)
    if(line MATCHES "^([^ ]+\\.obj): #deps")
        set(current_object "${CMAKE_MATCH_1}")
        string(REPLACE "\\" "/" current_object "${current_object}")
        set(current_owner "")
        set(current_source "")
        foreach(record IN LISTS records)
            string(REPLACE "|" ";" fields "${record}")
            list(GET fields 0 object)
            if(object STREQUAL current_object)
                list(GET fields 1 current_owner)
                list(GET fields 2 current_source)
                list(APPEND checked_units "${current_source}")
                break()
            endif()
        endforeach()
    elseif(current_owner AND line MATCHES "^    (.+)$")
        set(dependency "${CMAKE_MATCH_1}")
        string(REPLACE "\\" "/" dependency "${dependency}")
        string(REPLACE "\\" "/" source_normalized "${current_source}")

        # Publication headers are public contracts, not AUDIO internals.
        if(dependency MATCHES "/Inc/Audio/Publications/" OR
           dependency MATCHES "/Inc/Storage/sd_preview_ring_contract.h$")
            continue()
        endif()
        set(is_adapter FALSE)
        if(current_owner STREQUAL "CONTROL")
            set(adapter_list control_adapters)
        elseif(current_owner STREQUAL "AUDIO")
            set(adapter_list audio_adapters)
        else()
            set(adapter_list "")
        endif()
        foreach(adapter IN LISTS ${adapter_list})
            string(FIND "${source_normalized}" "${adapter}" adapter_at)
            if(NOT adapter_at EQUAL -1)
                set(is_adapter TRUE)
                break()
            endif()
        endforeach()
        if(is_adapter)
            continue()
        endif()

        string(TOLOWER "${current_owner}" current_owner_lower)
        set(forbidden_list "${current_owner_lower}_forbidden")
        foreach(forbidden IN LISTS ${forbidden_list})
            string(FIND "${dependency}" "${forbidden}" forbidden_at)
            if(NOT forbidden_at EQUAL -1)
                set(edge "${current_owner}: ${source_normalized} -> ${dependency}")
                list(APPEND violations "${edge}")
            endif()
        endforeach()
    endif()
endforeach()

list(REMOVE_DUPLICATES checked_units)
list(REMOVE_DUPLICATES violations)

# Contract headers are not translation units, so inspect their direct includes.
# Transitive dependencies are still covered by the compiled contract units.
set(contract_forbidden_includes
    "Audio/" "App/" "Storage/" "UI/" "Keyboard/" "MIDI/" "NoteFx/"
    "Track/track_runtime.h" "Track/track_state.h" "Param/param_registry.h"
    "Param/param_global_control.h" "Sampler/sample_page_cache_audio.h")
foreach(header IN LISTS DOMAIN_FIREWALL_CONTRACT_HEADERS)
    file(STRINGS "${header}" include_lines REGEX "^[ \t]*#[ \t]*include")
    foreach(include_line IN LISTS include_lines)
        if(include_line MATCHES "Audio/Publications/")
            continue()
        endif()
        foreach(forbidden IN LISTS contract_forbidden_includes)
            string(FIND "${include_line}" "${forbidden}" forbidden_at)
            if(NOT forbidden_at EQUAL -1)
                list(APPEND violations "CONTRACTS: ${header} -> ${forbidden}")
            endif()
        endforeach()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES violations)

list(LENGTH checked_units checked_count)
if(violations)
    list(JOIN violations "\n  " violation_text)
    message(FATAL_ERROR "Domain dependency violations:\n  ${violation_text}")
endif()
message(STATUS "M7 domain dependency firewall passed (${checked_count} translation units)")
