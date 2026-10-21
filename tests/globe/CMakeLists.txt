set(APP_NAME geostudio-globe-tests)

set(SOURCES tests.cpp tests_projection_lifecycle.cpp tests_tessellation.cpp tests_slug_fallback.cpp
    tests_terrain_anchor.cpp tests_overlay_depth.cpp tests_overlay_lifecycle.cpp tests_texture.cpp tests_units.cpp tests_antimeridian.cpp catch.hpp)

add_executable(${APP_NAME} ${SOURCES})

target_link_libraries(${APP_NAME} GeoStudio::Globe nlohmann_json::nlohmann_json)

if(WIN32)
    target_link_libraries(${APP_NAME} ws2_32)
endif()

# Catch and the ECS templates can exceed the COFF section limit in MSVC debug-info builds.
target_compile_options(${APP_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)

# Tests use json.h, which relies on nlohmann_json, which is not a public dependency of rocky
if (BUILD_WITH_JSON)
    find_package(nlohmann_json CONFIG)
    if (nlohmann_json_FOUND)
        target_link_libraries(${APP_NAME} nlohmann_json::nlohmann_json)
    endif()
endif()


add_test(NAME geostudio-globe-tests COMMAND ${APP_NAME} "~[environment]~[requires-grid]~[.]~[issue229-repro]")
set_tests_properties(geostudio-globe-tests PROPERTIES LABELS "unit;offline" TIMEOUT 180)
option(GEOSTUDIO_ENVIRONMENT_TESTS "Run network and PROJ grid tests" OFF)
if(GEOSTUDIO_ENVIRONMENT_TESTS)
    add_test(NAME environment-contracts COMMAND ${APP_NAME} "[environment],[requires-grid]")
endif()

set_target_properties(${APP_NAME} PROPERTIES FOLDER "tests")
