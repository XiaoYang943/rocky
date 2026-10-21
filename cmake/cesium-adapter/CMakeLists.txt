# Cesium Native 和其 GLM 配置只存在于 RuntimeTiles 的私有实现。
set(CMAKE_CXX_STANDARD 20)
set(BUILD_SHARED_LIBS OFF)
set(CESIUM_USE_EZVCPKG OFF CACHE BOOL "" FORCE)
set(CESIUM_TESTS_ENABLED OFF CACHE BOOL "" FORCE)
set(CESIUM_ENABLE_CLANG_TIDY OFF CACHE BOOL "" FORCE)
add_subdirectory("${GN_SOURCE_cesium-native}" cesium-native EXCLUDE_FROM_ALL)
target_compile_options(CesiumCurl PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/UNOMINMAX>)
find_package(Microsoft.GSL CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
set(VSGCS_USE_PROJ OFF)
set(VSGCS_FULL_DATA_DIR "${CMAKE_INSTALL_PREFIX}/share/GeoStudioNative/resources/tiles3d")
set(vsgCs_VERSION "0.2.0")
set(vsgCs_VERSION_MAJOR 0)
set(vsgCs_VERSION_MINOR 2)
set(vsgCs_VERSION_PATCH 0)
set(vsgCs_VERSION_TWEAK 0)
set(TILES3D_PRIVATE_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/include")
configure_file("${PROJECT_SOURCE_DIR}/src/render/tileset/cesium/Config.h.in" "${TILES3D_PRIVATE_INCLUDE}/geostudio/native/render/tileset/cesium/Config.h")
configure_file("${PROJECT_SOURCE_DIR}/src/core/base/cesium/Version.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/Version.cpp")
include(CheckIncludeFileCXX)
check_include_file_cxx("sys/utsname.h" HAVE_SYS_UTSNAME)
set(SOURCES
    "${PROJECT_SOURCE_DIR}/src/render/model/cesium/CesiumGltfBuilder.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/resources/cesium/CompilableImage.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/geospatial/cesium/CRS.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/tileset/overlays/CsDebugColorizeTilesOverlay.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/tileset/overlays/CsOverlay.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/tileset/overlays/CsTileMapServiceRasterOverlay.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/tileset/overlays/CsUrlTemplateRasterOverlay.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/tileset/overlays/CsWebMapServiceRasterOverlay.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/tileset/overlays/CsWebMapTileServiceRasterOverlay.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/geospatial/cesium/GeoNode.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/geospatial/cesium/GeospatialServices.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/model/cesium/GltfLoader.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/resources/cesium/GraphicsEnvironment.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/tileset/cesium/jsonUtils.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/model/cesium/ModelBuilder.cpp"
    "${PROJECT_SOURCE_DIR}/src/core/async/cesium/OpThreadTaskProcessor.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/model/cesium/pbr.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/tileset/cesium/RuntimeEnvironment.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/tileset/cesium/runtimeSupport.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/resources/cesium/ShaderFactory.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/model/cesium/Styling.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/tileset/cesium/TilesetNode.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/tileset/cesium/TracingCommandGraph.cpp"
    "${PROJECT_SOURCE_DIR}/src/drivers/http/cesium/UrlAssetAccessor.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/resources/cesium/vsgResourcePreparer.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/geospatial/cesium/WorldAnchor.cpp"
    "${PROJECT_SOURCE_DIR}/src/render/geospatial/cesium/WorldNode.cpp"
)
add_library(geostudio-cesium-adapter STATIC ${SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/Version.cpp")
set_target_properties(geostudio-cesium-adapter PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(geostudio-cesium-adapter PUBLIC cxx_std_20)
target_compile_definitions(geostudio-cesium-adapter PUBLIC VSGCS_LIBRARY_STATIC VSGCS_STATIC_DEFINE PRIVATE $<$<BOOL:${HAVE_SYS_UTSNAME}>:HAVE_SYS_UTSNAME>)
target_compile_options(geostudio-cesium-adapter PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)
if(WIN32)
    target_compile_definitions(geostudio-cesium-adapter PRIVATE NOGDI NOMINMAX)
endif()
target_include_directories(geostudio-cesium-adapter PUBLIC "${GEOSTUDIO_GENERATED_INCLUDE}" "${TILES3D_PRIVATE_INCLUDE}")
target_link_libraries(geostudio-cesium-adapter PUBLIC vsg::vsg Cesium3DTilesSelection CesiumRasterOverlays CesiumGltfContent CesiumGltfReader CesiumGltf CesiumCurl Microsoft.GSL::GSL spdlog::spdlog
    PRIVATE CURL::libcurl OpenSSL::SSL)
include(GenerateExportHeader)
generate_export_header(geostudio-cesium-adapter BASE_NAME VSGCS EXPORT_FILE_NAME "${TILES3D_PRIVATE_INCLUDE}/geostudio/native/core/base/cesium/Export.h")
add_library(geostudio-runtime-tiles SHARED "${PROJECT_SOURCE_DIR}/src/runtime/engine/TilesetAdapter.cpp")
set_target_properties(geostudio-runtime-tiles PROPERTIES EXPORT_NAME RuntimeTiles)
target_compile_features(geostudio-runtime-tiles PRIVATE cxx_std_20)
target_compile_definitions(geostudio-runtime-tiles PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(geostudio-runtime-tiles PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8> $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)
target_link_libraries(geostudio-runtime-tiles PUBLIC vsg::vsg nlohmann_json::nlohmann_json PRIVATE geostudio-cesium-adapter)
target_include_directories(geostudio-runtime-tiles PUBLIC
    $<BUILD_INTERFACE:${GEOSTUDIO_GENERATED_INCLUDE}>
    $<INSTALL_INTERFACE:include>)
generate_export_header(geostudio-runtime-tiles BASE_NAME GEOSTUDIO_NATIVE EXPORT_FILE_NAME "${GEOSTUDIO_GENERATED_INCLUDE}/geostudio/native/runtime/engine/Export.h")
install(TARGETS geostudio-runtime-tiles EXPORT GeoStudioNativeTargets RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)

configure_file("${GEOSTUDIO_GENERATED_INCLUDE}/geostudio/native/runtime/engine/Export.h" "${GEOSTUDIO_PUBLIC_INCLUDE}/geostudio/native/runtime/engine/Export.h" COPYONLY)
install(FILES "${GEOSTUDIO_PUBLIC_INCLUDE}/geostudio/native/runtime/engine/Export.h" DESTINATION include/geostudio/native/runtime/engine)
