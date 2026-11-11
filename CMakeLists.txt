cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(Bismuth VERSION 1.0.0)

message(STATUS "Found Geode: $ENV{CMAKE_C_COMPILER}")
if (DEFINED ENV{CMAKE_C_COMPILER})
    set(CMAKE_C_COMPILER $ENV{CMAKE_C_COMPILER})
endif()
if (DEFINED ENV{CMAKE_CXX_COMPILER})
    set(CMAKE_CXX_COMPILER $ENV{CMAKE_CXX_COMPILER})
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

set(CMAKE_BUILD_TYPE RelWithDebInfo)

set(ENABLE_OPT 0 CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

include(FetchContent)

FetchContent_Declare(
	glm
	GIT_REPOSITORY	https://github.com/g-truc/glm.git
	GIT_TAG 	0af55ccecd98d4e5a8d1fad7de25ba429d60e863
)
FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG vulkan-sdk-1.4.335
)

if (WIN32)
    FetchContent_Declare(
        OpenCL-Headers
        GIT_REPOSITORY	https://github.com/KhronosGroup/OpenCL-Headers.git
        GIT_TAG 	    v2026.05.29
    )
    FetchContent_MakeAvailable(glm glslang OpenCL-Headers)
    target_link_libraries(${PROJECT_NAME} glm::glm glslang::glslang OpenCL::Headers)
else()
    FetchContent_MakeAvailable(glm glslang)
    target_link_libraries(${PROJECT_NAME} glm::glm glslang::glslang)
    if (ANDROID)
        target_link_libraries(${PROJECT_NAME} EGL GLESv3)
    endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE src resources/shaders/headers)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})
