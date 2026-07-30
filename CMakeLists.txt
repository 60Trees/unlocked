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
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG main
)
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
FetchContent_MakeAvailable(
    SDL3
    cmrc
    LDtkLoader
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
)
target_include_directories(${PROJECT_NAME_VAR}
    PRIVATE
        src
)

target_compile_definitions(${PROJECT_NAME_VAR}
    PRIVATE
        PROJECT_GIT_URL="${GIT_URL}"
        PROJECT_GIT_COMMIT_HASH="${GIT_COMMIT_HASH}"
        PROJECT_NAME="${PROJECT_NAME_VAR}"
        PROJECT_VERSION="${PROJECT_VERSION}"
)

if(EMSCRIPTEN)
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
    )

    target_link_options(${PROJECT_NAME_VAR}
        PRIVATE
            --shell-file
            ${CMAKE_SOURCE_DIR}/web/shell.html
    )
endif()

