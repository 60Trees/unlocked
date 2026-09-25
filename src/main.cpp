#include <base/base.hpp>
#include "utils.hpp"

#include <exception>
#include <iostream>
#include <print>

#ifdef __EMSCRIPTEN__
#    include <emscripten.h>
EM_JS(void, close_or_finish, (), {
    window.close();

    // If the browser refused to close us, provide an alternative.
    setTimeout(() =>
                    {
                        if (!document.hidden) {
                            document.body.innerHTML = "<h1>Game finished</h1><p>You can close this tab.</p>";
                        }
                    },
        100);
});
#else
static inline void close_or_finish() {}
#endif

#define _gameloopstep(_name, action)                                                                                      \
    try action catch (const std::exception& e) {                                                                          \
        std::cout << "[GAMELOOP] Exception " << typeid(e).name() << " during " << _name << ": " << e.what() << std::endl; \
    }

int main() {
    welcome_message();

    Base::BaseClass* app = nullptr;

    std::print("\n[GAMELOOP] Constructing...\n");
    _gameloopstep("constructing", { app = GetApplication(false); });

    std::print("\n[GAMELOOP] Initializing...\n");
    _gameloopstep("initializing", { app->init(); });

    std::print("\n[GAMELOOP] Looping...\n");

    handle_loop(
        [&app]() {
            _gameloopstep("looping", { app->loop(); });

            if (!app->running) std::print("Not running\n");

            return app->running;
        },
        [&app]() {
            _gameloopstep("quitting", {
                app->quit();
                close_or_finish();
            });
        });

    return 0;
}
