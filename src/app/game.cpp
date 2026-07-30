#include "game.hpp"
#include <print>
#include <SDL3/SDL.h>

void Game::init() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        throw ErrorExit();
    }

    {
        this->window = SDL_CreateWindow("SDL3 + Emscripten", 1280, 720, SDL_WINDOW_RESIZABLE);

        if (!window) {
            SDL_Log("%s", SDL_GetError());
            SDL_Quit();
            throw ErrorExit();
        }
    }

    {
        this->renderer = SDL_CreateRenderer(window, nullptr);

        if (!renderer) {
            SDL_Log("%s", SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_Quit();
            throw ErrorExit();
        }
    }

    ldtk::Project main_world;
    {
        auto file = fs.open("assets/main.ldtk");
        const auto* data = reinterpret_cast<const unsigned char*>(file.begin());
        size_t size = file.size();

        main_world.loadFromMemory(data, size);
        std::print("Loaded world\n");
    }

    for (auto& tileset : main_world.allTilesets()) {
        std::print("Tileset {}: path={}\n", tileset.name, tileset.path);
    }
}
void Game::loop() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                this->running = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) running = false;
                break;
        }
    }

    if (!running) return;

    SDL_SetRenderDrawColor(renderer, 40, 40, 55, 255);
    SDL_RenderClear(renderer);

    SDL_FRect rect{100.f, 100.f, 200.f, 150.f};

    SDL_SetRenderDrawColor(renderer, 100, 200, 255, 255);
    SDL_RenderFillRect(renderer, &rect);

    SDL_RenderPresent(renderer);
}

void Game::quit() {
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

Application* Application::get() {
    static Application* app = nullptr;
    if (!app) app = new Game();
    return app;
}

