#include <base/app.hpp>
#include <base/renderer.hpp>

#include <print>
#include <LDtkLoader/Project.hpp>
#include <SDL3/SDL.h>

#include <fs_utils.hpp>
#include "utils.hpp"

struct Game : Base::Application {
    void init() override {
        renderer->init();
        fps_counter->init();

        Base::Renderer::ColouredVertex tri[3];
        tri[0].rgba = 0x0000FF'FF;
        tri[0].x = 0;
        tri[0].y = 0;
        tri[1].rgba = 0x00FF00'FF;
        tri[1].x = 0;
        tri[1].y = 1;
        tri[2].rgba = 0xFF0000'FF;
        tri[2].x = 1;
        tri[2].y = 0;

        renderer->worldtris.coloured.push_back(tri[0]);
        renderer->worldtris.coloured.push_back(tri[1]);
        renderer->worldtris.coloured.push_back(tri[2]);
        tri[0].rgba = 0x0000FF'FF;
        tri[0].x = 1;
        tri[0].y = 1;
        tri[1].rgba = 0x00FF00'FF;
        tri[1].x = 1;
        tri[1].y = 0;
        tri[2].rgba = 0xFF0000'FF;
        tri[2].x = 0;
        tri[2].y = 1;
        renderer->worldtris.coloured.push_back(tri[0]);
        renderer->worldtris.coloured.push_back(tri[1]);
        renderer->worldtris.coloured.push_back(tri[2]);

        ldtk::Project main_world;
        {
            const std::span<const unsigned char> file = fs_helper::get_bytes_from_file<unsigned char>("assets/main.ldtk");
            main_world.loadFromMemory(file.data(), file.size());
            std::print("Loaded world\n");
        }

        for (auto& tileset : main_world.allTilesets()) {
            std::print("Tileset\n- {}\n", tileset.path);
            // The path cannot be outside the embedded filesystem (the one in the binary)
            if (tileset.path.starts_with("../"))
                std::print("- (INVALID PATH)\n");
            else {
                // This means that the path mentioned is inside the `fs` so it can be fetched
                std::string _path = "assets/" + tileset.path;
                //renderer->addAtlasFromData(_path, fs_helper::get_bytes_from_file<unsigned char>(_path));
                std::print("- (id={})", renderer->getAtlasId(_path));
            }
        }
    }

    struct {
        float up, down, left, right;
        void do_inputs(const SDL_Keycode keycode, float is_pressed) {
            switch (keycode) {
                case SDLK_UP:
                    up = is_pressed;
                    break;
                case SDLK_DOWN:
                    down = is_pressed;
                    break;
                case SDLK_LEFT:
                    left = is_pressed;
                    break;
                case SDLK_RIGHT:
                    right = is_pressed;
                    break;
            }
        }
    } inputs;

    void loop() override {
        fps_counter->loop();
        renderer->camera.update_zoom(fps_counter->deltaTime);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    this->running = false;
                    break;

                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_ESCAPE) running = false;
                    inputs.do_inputs(event.key.key, 1.0f);
                    if (!event.key.repeat) {
                        std::print("Key {} down!!1!\n", event.key.key);
                        for (auto& i : renderer->worldtris.coloured) i.rgba = 0xFF0000'FF;
                    }
                    break;

                case SDL_EVENT_KEY_UP:
                    inputs.do_inputs(event.key.key, 0.0f);
                    for (auto& i : renderer->worldtris.coloured) i.rgba = 0x00FF00'FF;
                    break;
            }
        }

        renderer->camera.x += (inputs.right - inputs.left) * fps_counter->deltaTime;
        renderer->camera.y += (inputs.up - inputs.down) * fps_counter->deltaTime;

        renderer->loop();

        check_running(renderer.get());
        check_running(fps_counter.get());
        if (!running) return;
    }

    void quit() override {
        fps_counter->quit();
        renderer->quit();
    }
};

extern "C" Base::BaseClass* GetApplication() { return new Game(); }
