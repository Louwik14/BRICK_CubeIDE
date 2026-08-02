cmake_minimum_required(VERSION 3.20)
project(engine_lane_authority_tests C)

enable_testing()

foreach(ENGINE_LANE_VARIANT IN ITEMS premium lowcost)
    set(ENGINE_LANE_TARGET engine_lane_authority_test_${ENGINE_LANE_VARIANT})
    add_executable(${ENGINE_LANE_TARGET}
        ../engine_lane_authority_test.c
        ../../Src/Core/engine_lane_authority.c
        ../../Src/Core/track_topology.c
    )
    target_include_directories(${ENGINE_LANE_TARGET} PRIVATE ../../Inc)
    if(ENGINE_LANE_VARIANT STREQUAL lowcost)
        target_compile_definitions(${ENGINE_LANE_TARGET} PRIVATE BRICK6_VARIANT_LOWCOST=1)
    else()
        target_compile_definitions(${ENGINE_LANE_TARGET} PRIVATE BRICK6_VARIANT_PREMIUM=1)
    endif()
    if(CMAKE_CROSSCOMPILING)
        target_link_options(${ENGINE_LANE_TARGET} PRIVATE --specs=nosys.specs)
    endif()
    add_test(NAME ${ENGINE_LANE_TARGET} COMMAND ${ENGINE_LANE_TARGET})
endforeach()
