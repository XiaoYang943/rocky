geostudio_capability(render Render ${GN_RENDER_SOURCES})
target_link_libraries(geostudio-render PUBLIC geostudio-scene geostudio-streaming geostudio-drivers geostudio-import)
