cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(GPUObjectRenderer VERSION 0.2.3)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Define la variable de entorno GEODE_SDK apuntando al SDK de Geode")
else()
    message(STATUS "Geode SDK: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

if (ANDROID)
    # EGL: eglGetProcAddress y eglGetCurrentContext. GLESv3: los símbolos GLES (glEnable, glBlendFunc, ...)
    # que se llaman directamente; las funciones de compute/SSBO se cargan en tiempo de ejecución.
    target_link_libraries(${PROJECT_NAME} EGL GLESv3)
elseif (WIN32)
    target_link_libraries(${PROJECT_NAME} opengl32)
endif()

setup_geode_mod(${PROJECT_NAME})
