#include <SDL3/SDL.h>

#ifdef __EMSCRIPTEN__
#    include <emscripten.h>
#endif

static SDL_Window* window = nullptr;
static SDL_Renderer* renderer = nullptr;

static bool running = true;

static void Frame() {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) running = false;
                break;
        }
    }

    if (!running) {
#ifdef __EMSCRIPTEN__
        emscripten_cancel_main_loop();
#else
        return;
#endif
    }

    SDL_SetRenderDrawColor(renderer, 40, 40, 55, 255);
    SDL_RenderClear(renderer);

    SDL_FRect rect{100.f, 100.f, 200.f, 150.f};

    SDL_SetRenderDrawColor(renderer, 100, 200, 255, 255);
    SDL_RenderFillRect(renderer, &rect);

    SDL_RenderPresent(renderer);
}

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("SDL3 + Emscripten", 1280, 720, SDL_WINDOW_RESIZABLE);

    if (!window) {
        SDL_Log("%s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(window, nullptr);

    if (!renderer) {
        SDL_Log("%s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(Frame, 0, true);
#else
    while (running) Frame();
#endif

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
