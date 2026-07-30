#include <SDL3/SDL.h>
#include <cmrc/cmrc.hpp>
#include <LDtkLoader/Project.hpp>
#include <app.hpp>
#include <print>

CMRC_DECLARE(game_assets);
cmrc::embedded_filesystem fs = cmrc::game_assets::get_filesystem();

#include <emscripten_wrapper.hpp>

int main() {
    Application* app = Application::get();
    app->init();

    std::print("{} v{}\n- {} commit# {}", PROJECT_NAME, PROJECT_VERSION, PROJECT_GIT_URL, PROJECT_GIT_COMMIT_HASH);

    emswrapper_loop([&app]() {
        bool doexit = false;
        try {
            app->loop();
            doexit = !app->running;
        } catch (Application::ErrorExit) {
            std::print("Error exit!");
            doexit = true;
        } catch (Application::Exit) {
            std::print("Exit thrown!");
            doexit = true;
        }
        if (doexit) {
            app->running = false;
        }
        return app->running;
    });

    std::print("Quitting...\n");

    app->quit();
    delete app;

    return 0;
}
