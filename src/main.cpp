#include <base/base.hpp>

#include <cmrc/cmrc.hpp>
#include "SDL3/SDL_version.h"
CMRC_DECLARE(game_assets);
cmrc::embedded_filesystem fs = cmrc::game_assets::get_filesystem();

#include <print>
#include <functional>
#include <webgpu/webgpu.hpp>
#include <iostream>

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

// <AI>
static inline std::string runtime_datetime() {
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

extern "C" Base::BaseClass* GetApplication();

extern "C" long long funny_number_generator();

int main() {
    // std::print("{}-bit machine.\n", sizeof(void*) * 8);
    // if constexpr (sizeof(void*) * 8 == 32) {
    //     std::print("Good luck running this on a 32 bit machine! It will probably crash (:\n");
    // }

    // <AI>
    // </AI>

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

    std::print("Initializing...\n");
    Base::BaseClass* app = GetApplication();
    app->init();

    std::print("Looping...\n");
    handle_loop([&app]() {
        app->loop();
        return app->running;
    });

    std::print("Quitting...\n");
    app->quit();

    return 0;
}
