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
void handle_loop(std::function<bool()> loop) {
    while (loop());
}
#endif

extern "C" Base::BaseClass* GetApplication();

int main() {
    std::print("{} v{}\n- {} commit# {}", PROJECT_NAME, PROJECT_VERSION, PROJECT_GIT_URL, PROJECT_GIT_COMMIT_HASH);

    std::print("Initializing...\n");
    Base::BaseClass* app = GetApplication();
    app->init();

    handle_loop([&app]() {
        app->loop();
        return app->running;
    });

    std::print("Quitting...\n");
    app->quit();

    return 0;
}
