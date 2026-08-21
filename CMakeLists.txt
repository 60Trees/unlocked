cmake_minimum_required(VERSION 3.24)

# <AI> cmake is annoying + tedious

set(PROJECT_NAME_VAR "Run")
message("Configuring ${PROJECT_NAME_VAR}...")

project(${PROJECT_NAME_VAR}
    VERSION 1.0.0
    LANGUAGES C CXX
)

file(GLOB_RECURSE PROJECT_SOURCES
    CONFIGURE_DEPENDS
    src/*.cpp
    src/*.c
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME_VAR}
    ${PROJECT_SOURCES}
)

include(FetchContent)

set(FETCHCONTENT_BASE_DIR
    "${CMAKE_SOURCE_DIR}/lib"
)

#FetchContent_Declare(
#    yyjson
#    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
#    GIT_TAG        0.12.0
#)
#
#FetchContent_MakeAvailable(yyjson)
#FetchContent_Declare(
#    ldtkimport
#    GIT_REPOSITORY https://github.com/AnomalousUnderdog/ldtkimport.git
#    GIT_TAG        development
#)
#
#FetchContent_GetProperties(ldtkimport)
#if(NOT ldtkimport_POPULATED)
#    FetchContent_Populate(ldtkimport)
#endif()
#
#add_library(ldtkimport STATIC
#    ${ldtkimport_SOURCE_DIR}/source/LdtkDefFile.cpp
#    ${ldtkimport_SOURCE_DIR}/source/Rule.cpp
#    # add the rest of the .cpp files here
#)
#
#target_include_directories(ldtkimport
#    PUBLIC
#        ${ldtkimport_SOURCE_DIR}/include
#)

#target_link_libraries(ldtkimport
#    PUBLIC
#        yyjson
#)
FetchContent_Declare(
    cmrc
    GIT_REPOSITORY https://github.com/vector-of-bool/cmrc.git
    GIT_TAG master
)
FetchContent_Declare(
    LDtkLoader
    GIT_REPOSITORY https://github.com/Madour/LDtkLoader.git
    GIT_TAG master
)
FetchContent_Declare(
    sdl3webgpu
    GIT_REPOSITORY https://github.com/eliemichel/sdl3webgpu.git
    GIT_TAG main
)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)
FetchContent_Declare(
    webgpu
    GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution
    GIT_TAG v0.3.0-gamma
)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.8
    CMAKE_ARGS
        -DHAVE_GETRESUID=1
        -DHAVE_GETRESGID=1
)
FetchContent_Declare(
    gcem
    GIT_REPOSITORY https://github.com/kthohr/gcem.git
    GIT_TAG master
)

FetchContent_GetProperties(gcem)
if(NOT gcem_POPULATED)
    FetchContent_Populate(gcem)
endif()

# ---------------------------------------------------------------------------
# WebGPU
#
# Native:
#     WebGPU-distribution -> wgpu-native
#
# Emscripten:
#     WebGPU-distribution -> emdawnwebgpu
# ---------------------------------------------------------------------------

if(EMSCRIPTEN)
    message(STATUS "Building for Emscripten")

    set(WEBGPU_BACKEND
        "EMDAWNWEBGPU"
        CACHE STRING "WebGPU backend"
        FORCE
    )

    set_target_properties(${PROJECT_NAME_VAR}
        PROPERTIES
            SUFFIX ".html"
    )

    target_link_options(${PROJECT_NAME_VAR}
        PRIVATE
            "-sUSE_SDL=3"
            "-sASSERTIONS=1"
            "-sALLOW_MEMORY_GROWTH"
            "-sEXIT_RUNTIME=0"
            "-sASYNCIFY=1"
            "--shell-file"
            "${CMAKE_SOURCE_DIR}/web/shell.html"
    )
else()

    message(STATUS "Building native")

    set(WEBGPU_BACKEND
        "WGPU"
        CACHE STRING "WebGPU backend"
        FORCE
    )

    set(WEBGPU_BUILD_FROM_SOURCE
        OFF
        CACHE BOOL "Build wgpu-native from source"
        FORCE
    )
endif()
FetchContent_MakeAvailable(webgpu)

# SDL3
set(HAVE_GETRESUID 1 CACHE INTERNAL "")
set(HAVE_GETRESGID 1 CACHE INTERNAL "")
FetchContent_MakeAvailable(SDL3)

# Other
FetchContent_MakeAvailable(
    cmrc
    LDtkLoader
    sdl3webgpu
    stb
    glm
)

if(EMSCRIPTEN)
    # Tell SDL3WebGPU to use the Emscripten/Dawn WebGPU API.
    target_compile_definitions(
        sdl3webgpu
        PRIVATE
            WEBGPU_BACKEND_EMDAWNWEBGPU
    )

    # Emscripten provides the WebGPU implementation through its port.
    target_compile_options(
        sdl3webgpu
        PRIVATE
            "--use-port=emdawnwebgpu"
    )
endif()

# ---------------------------------------------------------------------------
# Git information
# ---------------------------------------------------------------------------

execute_process(
    COMMAND git config --get remote.origin.url
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_URL
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT GIT_URL)
    set(GIT_URL "unknown://url")
endif()

if(NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH "unknownhash")
endif()

# ---------------------------------------------------------------------------
# Assets
# ---------------------------------------------------------------------------

file(GLOB_RECURSE ASSET_FILES
    CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_SOURCE_DIR}"
    assets/*
)

cmrc_add_resource_library(
    game_assets
    ALIAS assets
    ${ASSET_FILES}
)

# imgui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.8
)
FetchContent_MakeAvailable(imgui)
add_library(imgui
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC
    SDL3::SDL3
    webgpu
)
if(NOT EMSCRIPTEN)
    target_compile_definitions(imgui PUBLIC
        IMGUI_IMPL_WEBGPU_BACKEND_WGPU
    )
endif()

# ---------------------------------------------------------------------------
# Linking
# ---------------------------------------------------------------------------

target_link_libraries(${PROJECT_NAME_VAR}
    PRIVATE
        SDL3::SDL3
        game_assets
        LDtkLoader
        webgpu
        sdl3webgpu
        glm
        imgui
        #ldtkimport
)

if(NOT EMSCRIPTEN)
    target_copy_webgpu_binaries(${PROJECT_NAME_VAR})
endif()

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------

target_include_directories(${PROJECT_NAME_VAR}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/"
        "${gcem_SOURCE_DIR}/include/"
        "${stb_SOURCE_DIR}/"
        "${CMAKE_CURRENT_SOURCE_DIR}/lib_helper/"
)

# ---------------------------------------------------------------------------
# Project definitions
# ---------------------------------------------------------------------------

target_compile_definitions(${PROJECT_NAME_VAR}
    PRIVATE
        PROJECT_GIT_URL="${GIT_URL}"
        PROJECT_GIT_COMMIT_HASH="${GIT_COMMIT_HASH}"
        PROJECT_NAME="${PROJECT_NAME_VAR}"
        PROJECT_VERSION="${PROJECT_VERSION}"
)
