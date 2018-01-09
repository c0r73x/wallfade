cmake_minimum_required(VERSION 3.4)

project(wallfade)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(ImageMagick COMPONENTS MagickWand REQUIRED)
find_package(X11 REQUIRED)
find_package(OpenGL REQUIRED)

option(USEOPENMP "enable openmp threading" ON)
if(USEOPENMP)
    find_package(OpenMP)
    if (OPENMP_FOUND)
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    endif()
endif()

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DMAGICKCORE_HDRI_ENABLE=0 -DMAGICKCORE_QUANTUM_DEPTH=16")

include_directories(
    "."
    ${ImageMagick_INCLUDE_DIRS}
    ${X11_INCLUDE_DIR}
    ${X11_Xrandr_INCLUDE_PATH}
    ${X11_Xcomposite_INCLUDE_PATH}
    ${OPENGL_INCLUDE_DIR}
    )

message("ImageMagick:${ImageMagick_INCLUDE_DIRS} ${ImageMagick_FOUND}")
set(CMAKE_C_STANDARD 11)

string(SUBSTRING ${ImageMagick_VERSION_STRING} 0 1 ImageMagick_MajorVersion)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/wallfade.c.in"
    "${CMAKE_CURRENT_BINARY_DIR}/wallfade.c"
    )

set(COMMON_SRC
    "${CMAKE_CURRENT_BINARY_DIR}/wallfade.c"
    )

add_executable(${CMAKE_PROJECT_NAME} ${COMMON_SRC})

target_link_libraries(${CMAKE_PROJECT_NAME}
    ${ImageMagick_LIBRARIES}
    ${X11_LIBRARIES}
    ${X11_Xrandr_LIB}
    ${X11_Xcomposite_LIB}
    ${OPENGL_LIBRARIES}
    Threads::Threads
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
