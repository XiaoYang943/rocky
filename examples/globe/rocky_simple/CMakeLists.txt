set(APP_NAME rocky_simple)

file(GLOB SOURCES *.cpp)

add_executable(${APP_NAME} ${SOURCES})

set(LIBS GeoStudio::Globe GeoStudio::IO)



target_link_libraries(${APP_NAME} ${LIBS})


install(TARGETS ${APP_NAME} RUNTIME DESTINATION bin)

set_target_properties(${APP_NAME} PROPERTIES FOLDER "apps")
