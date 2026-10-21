geostudio_capability(runtime Runtime ${GN_RUNTIME_SOURCES})
target_link_libraries(geostudio-runtime PUBLIC geostudio-render geostudio-runtime-tiles)
