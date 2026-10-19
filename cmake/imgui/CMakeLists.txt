cmake_minimum_required(VERSION 3.21)
project(ng_imgui VERSION 1.92.5 LANGUAGES CXX)
find_package(Vulkan REQUIRED)
add_library(imgui STATIC
  "${IMGUI_SOURCE}/imgui.cpp" "${IMGUI_SOURCE}/imgui_draw.cpp"
  "${IMGUI_SOURCE}/imgui_tables.cpp" "${IMGUI_SOURCE}/imgui_widgets.cpp"
  "${IMGUI_SOURCE}/imgui_demo.cpp" "${IMGUI_SOURCE}/backends/imgui_impl_vulkan.cpp")
target_link_libraries(imgui PUBLIC Vulkan::Vulkan)
target_include_directories(imgui PUBLIC $<BUILD_INTERFACE:${IMGUI_SOURCE}> $<BUILD_INTERFACE:${IMGUI_SOURCE}/backends> $<INSTALL_INTERFACE:include/imgui> $<INSTALL_INTERFACE:include/imgui/backends>)
install(TARGETS imgui EXPORT imguiTargets ARCHIVE DESTINATION lib)
install(FILES "${IMGUI_SOURCE}/imgui.h" "${IMGUI_SOURCE}/imconfig.h" "${IMGUI_SOURCE}/imgui_internal.h" "${IMGUI_SOURCE}/imstb_rectpack.h" "${IMGUI_SOURCE}/imstb_textedit.h" "${IMGUI_SOURCE}/imstb_truetype.h" DESTINATION include/imgui)
install(FILES "${IMGUI_SOURCE}/backends/imgui_impl_vulkan.h" DESTINATION include/imgui/backends)
install(EXPORT imguiTargets NAMESPACE imgui:: DESTINATION lib/cmake/imgui)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/imguiConfig.cmake" DESTINATION lib/cmake/imgui)
