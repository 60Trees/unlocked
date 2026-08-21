#include <base/base.hpp>
#include "utils.hpp"

#include <print>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(void, close_or_finish, (), {
    window.close();

    // If the browser refused to close us, provide an alternative.
    setTimeout(() => {
        if (!document.hidden) {
            document.body.innerHTML =
                "<h1>Game finished</h1><p>You can close this tab.</p>";
        }
    }, 100);
});
#else
static inline void close_or_finish() {}
#endif

int main() {
    welcome_message();

    std::print("\n[GAMELOOP] Initializing...\n");

    Base::BaseClass* app = GetApplication(false);
    app->init();

    std::print("\n[GAMELOOP] Looping...\n");

    handle_loop(
        [&app]() {
            app->loop();

            if (!app->running)
                std::print("Not running\n");

            return app->running;
        },
        [&app]() {
            app->quit();
            close_or_finish();
        }
    );

    return 0;
}
