#include "utils.hpp"

#include <iostream>
#include <functional>
#include <chrono>

#include <webgpu/webgpu.hpp>
#include "SDL3/SDL_version.h"

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(game_assets);
cmrc::embedded_filesystem fs = cmrc::game_assets::get_filesystem();

long long funny_number_generator() {
    // hiding it in a struct stops the compiler from complaining
    struct {
        long long i_wonder_what_this_number_is;
    } hi;
    // very funny undefined behaviour yes funny
    return hi.i_wonder_what_this_number_is;
}

// <AI>
std::string runtime_datetime() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm local{};

#ifdef _WIN32
    localtime_s(&local, &t);
#else
    local = *std::localtime(&t);
#endif

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%b %e %Y at %H:%M:%S", &local);

    return buffer;
}
// </AI>
//
#ifdef __EMSCRIPTEN__
#    include <emscripten.h>
void handle_loop(std::function<bool()> loop) {
    static std::function<bool()> _loop = [] { return false; };
    _loop = loop;
    emscripten_set_main_loop(
        [] {
            if (!_loop()) emscripten_cancel_main_loop();
        },
        0, true);
}
#else
void handle_loop(std::function<bool()> loop) { while (loop()); }
#endif


void welcome_message() {
    std::cout << PROJECT_NAME << " v" << PROJECT_VERSION << "\n| " << sizeof(void*) * 8 << "-bit computer"
              << "\n| Uninitialized memory test: " << funny_number_generator() << "\n| Git URL: " << PROJECT_GIT_URL << " with commit #"
              << PROJECT_GIT_COMMIT_HASH << "\n| "
#ifdef __EMSCRIPTEN__
              << "Using EMSCRIPTEN\n| "
#endif
#ifdef __clang___
              << "Compiler: Clang " << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__

#elif defined(__GNUC__)
              << "Compiler: GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__

#elif defined(_MSC_VER)
              << "Compiler: MSVC " << _MSC_VER
#else
              << "Unknown compiler"
#endif
#ifdef _LIBCPP_VERSION
              << " (using libc++)"
#elif defined(__GLIBCXX__)
              << " (using libstdc++)"
#else
              << " (unknown STL)"
#endif
              << "\n| Compiled on " << __DATE__ << " at " << __TIME__ << "\n| Run on " << runtime_datetime()
              << "\n| WGPU version: " << wgpuGetVersion() << "\n| SDL version: " << SDL_GetVersion() << std::endl;
}
