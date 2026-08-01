cmake_minimum_required(VERSION 3.24)

set(PROJECT_NAME_VAR "Run")

project(${PROJECT_NAME_VAR}
    VERSION 1.0.0
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/lib")

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

if(EMSCRIPTEN)
    set_target_properties(${PROJECT_NAME_VAR}
        PROPERTIES
        SUFFIX ".html"
    )

    target_link_options(${PROJECT_NAME_VAR}
        PRIVATE
            "-sUSE_SDL=3"
            "-sUSE_WEBGPU=1"
            "-sASSERTIONS=1"
            "-sALLOW_MEMORY_GROWTH"
            "-sEXIT_RUNTIME=0"
    )

    target_link_options(${PROJECT_NAME_VAR}
        PRIVATE
            --shell-file
            ${CMAKE_SOURCE_DIR}/web/shell.html
    )
else()
    set(CMAKE_REQUIRED_DEFINITIONS "-D_DEFAULT_SOURCE" CACHE STRING "" FORCE)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.8
        CMAKE_ARGS
            -DHAVE_GETRESUID=1
            -DHAVE_GETRESGID=1
    )

    FetchContent_Declare(
        webgpu
        GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution
        GIT_TAG        v0.3.0-gamma
    )
    set(WEBGPU_BACKEND "WGPU" CACHE STRING "" FORCE)
    set(WEBGPU_BUILD_FROM_SOURCE OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(webgpu)
endif()

# gcem has to be manually imported because it is a special little snowflake
FetchContent_Declare(
    gcem
    GIT_REPOSITORY https://github.com/kthohr/gcem.git
    GIT_TAG master # or replace with a specific release tag/commit
)
FetchContent_GetProperties(gcem)
if(NOT gcem_POPULATED)
    FetchContent_Populate(gcem)
endif()

if(NOT EMSCRIPTEN)
    set(HAVE_GETRESUID 1 CACHE INTERNAL "")
    set(HAVE_GETRESGID 1 CACHE INTERNAL "")
    FetchContent_MakeAvailable(SDL3)
endif()

FetchContent_MakeAvailable(
    cmrc
    LDtkLoader
    sdl3webgpu
    stb
)
file(GLOB_RECURSE PROJECT_SOURCES
    CONFIGURE_DEPENDS
    src/*.cpp
    src/*.c
)

# <AI>
# Get Git remote URL
execute_process(
    COMMAND git config --get remote.origin.url
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_URL
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Get commit hash
execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Fallbacks
if(NOT GIT_URL)
    set(GIT_URL "unknown://url")
endif()

if(NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH "unknownhash")
endif()
# </AI>

add_executable(${PROJECT_NAME_VAR}
    ${PROJECT_SOURCES}
)

file(GLOB_RECURSE ASSET_FILES
    CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_SOURCE_DIR}"
    assets/*
)

cmrc_add_resource_library(game_assets
    ALIAS assets
    ${ASSET_FILES}
)
target_link_libraries(${PROJECT_NAME_VAR}
    PRIVATE
        SDL3::SDL3
        game_assets
        LDtkLoader
        webgpu
        sdl3webgpu
)

if(NOT EMSCRIPTEN)
    target_copy_webgpu_binaries(${PROJECT_NAME_VAR})
endif()


target_include_directories(${PROJECT_NAME_VAR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/
        ${gcem_SOURCE_DIR}/include/
        ${stb_SOURCE_DIR}/
)

target_compile_definitions(${PROJECT_NAME_VAR}
    PRIVATE
        PROJECT_GIT_URL="${GIT_URL}"
        PROJECT_GIT_COMMIT_HASH="${GIT_COMMIT_HASH}"
        PROJECT_NAME="${PROJECT_NAME_VAR}"
        PROJECT_VERSION="${PROJECT_VERSION}"
)

