#include <base/base.hpp>
#include "utils.hpp"

#include <print>

int main() {
    welcome_message();

    std::print("\n[GAMELOOP] Initializing...\n");

    Base::BaseClass* app = GetApplication(false);
    app->init();

    std::print("\n[GAMELOOP] Looping...\n");

    handle_loop([&app]() {
        app->loop();
        return app->running;
    });

    std::print("\n[GAMELOOP] Quitting...\n");
    app->quit();

    return 0;
}
