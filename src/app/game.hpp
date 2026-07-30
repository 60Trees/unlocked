#include <app.hpp>
#include <LDtkLoader/Project.hpp>
#include <cmrc/cmrc.hpp>
#include <SDL3/SDL.h>

extern cmrc::embedded_filesystem fs;

struct Game : Application {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    void init() override;
    void loop() override;
    void quit() override;
};
