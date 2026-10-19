set(APP_NAME rocky_demo)

find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

file(GLOB SOURCES *.cpp)
file(GLOB HEADERS *.h)

# enable folders for IDEs
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_executable(${APP_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${APP_NAME} GeoStudio::Globe imgui::imgui nlohmann_json::nlohmann_json)

install(TARGETS ${APP_NAME} RUNTIME DESTINATION bin)

if (USING_VCPKG AND ROCKY_INSTALL_VCPKG_DEPENDENCY_DLLS)
    file(GLOB VCPKG_DLLS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/*.dll")
    install(FILES ${VCPKG_DLLS} DESTINATION bin)
endif()

set_target_properties(${APP_NAME} PROPERTIES FOLDER "apps")

# MSVC may require /bigobj for large translation units.
if (MSVC)
    target_compile_options(${APP_NAME} PRIVATE /bigobj)
endif()
