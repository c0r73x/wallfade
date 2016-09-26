cmake_minimum_required(VERSION 3.4)

project(wallfade)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(ImageMagick COMPONENTS MagickWand REQUIRED)
find_package(X11 REQUIRED)
find_package(OpenGL REQUIRED)

set(COMMON_SRC
    "wallfade.c"
    )

include_directories(
    "."
    ${ImageMagick_INCLUDE_DIRS}
    ${X11_INCLUDE_DIR}
    ${X11_Xrandr_INCLUDE_PATH}
    ${OPENGL_INCLUDE_DIR}
    )

message("magic:${ImageMagick_INCLUDE_DIRS} ${ImageMagick_FOUND}")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 11)

add_executable(${CMAKE_PROJECT_NAME} ${COMMON_SRC})

target_link_libraries(${CMAKE_PROJECT_NAME}
    ${ImageMagick_LIBRARIES}
    ${X11_LIBRARIES}
    ${X11_Xrandr_LIB}
    ${OPENGL_LIBRARIES}
    bsd
    )
install(TARGETS ${CMAKE_PROJECT_NAME} RUNTIME DESTINATION bin)

# uninstall target
configure_file(
    "${CMAKE_MODULE_PATH}/cmake_uninstall.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)

find_program(iwyu_path NAMES include-what-you-use iwyu)
if(iwyu_path)
    set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY C_INCLUDE_WHAT_YOU_USE ${iwyu_path})
    set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY CXX_INCLUDE_WHAT_YOU_USE ${iwyu_path})
endif()
