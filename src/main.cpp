#include <base/base.hpp>

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(game_assets);
cmrc::embedded_filesystem fs = cmrc::game_assets::get_filesystem();

#include <print>
#include <functional>

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

extern "C" Base::BaseClass* GetApplication();

extern "C" long long funny_number_generator();
int main() {
    auto funny_number = funny_number_generator();
    std::print("Today's funny number is: {}.", funny_number);
    if (funny_number == 0)
        std::print(" ):\n");
    else
        std::print(" (:\n");

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
