#include <base/app.hpp>
#include <base/renderer.hpp>

#include <map>
#include <print>
#include <LDtkLoader/Project.hpp>
#include <SDL3/SDL.h>

#include <fs_utils.hpp>
#include <utility>

struct Point {
    int x, y;
};

struct Game : Base::Application {
    std::string hi = "";

    struct TexturedRectDescriptor {
        struct Rect {
            /// left, top, right, bottom
            float l, t, r, b;
        };
        Rect pos;
        Rect uv;
        uint8_t atlas_index;
        int8_t layer = 0;
        int8_t parralax = 0;
    };

    void make_square(TexturedRectDescriptor rect) {
        Base::Renderer::TexturedVertex tris[6];

        for (auto& tri : tris) {
            tri.textureid = rect.atlas_index;
            tri.layer = rect.layer;
            tri.depth = rect.parralax;
        }

        // Triangle 1: top-left, bottom-left, bottom-right
        tris[0].x = rect.pos.l;
        tris[0].y = rect.pos.t;
        tris[0].uvx = rect.uv.l;
        tris[0].uvy = rect.uv.t;

        tris[1].x = rect.pos.l;
        tris[1].y = rect.pos.b;
        tris[1].uvx = rect.uv.l;
        tris[1].uvy = rect.uv.b;

        tris[2].x = rect.pos.r;
        tris[2].y = rect.pos.b;
        tris[2].uvx = rect.uv.r;
        tris[2].uvy = rect.uv.b;

        // Triangle 2: top-left, bottom-right, top-right
        tris[3] = tris[0];

        tris[4] = tris[2];

        tris[5].x = rect.pos.r;
        tris[5].y = rect.pos.t;
        tris[5].uvx = rect.uv.r;
        tris[5].uvy = rect.uv.t;

        if (rect.atlas_index == 255)
            std::print("atlasIndex is invalid!\n");
        else
            // Push into renderer
            for (auto& t : tris) renderer->worldtris.textured.push_back(t);
    }

    void init() override {
        renderer->init();
        fps_counter->init();

        ldtk::Project main_world;
        {
            const std::span<const unsigned char> file = fs_helper::get_bytes_from_file<unsigned char>("assets/main.ldtk");
            main_world.loadFromMemory(file.data(), file.size());
            std::print("Loaded world\n");
        }

        std::print("Testing: {}", fs_helper::get_sview_from_file("assets/test.txt"));

        for (auto& tileset : main_world.allTilesets()) {
            std::print("Tileset\n- {}\n", tileset.path);
            // The path cannot be outside the embedded filesystem (the one in the binary)
            if (tileset.path.starts_with("../"))
                std::print("- (INVALID PATH)\n");
            else {
                // This means that the path mentioned is inside the `fs` so it can be fetched
                std::string _path = "assets/" + tileset.path;
                renderer->addAtlasFromData(_path, fs_helper::get_bytes_from_file<unsigned char>(_path));
                hi = _path;
                std::print("- (id={})\n", renderer->getAtlasId(_path));
            }
        }

        const auto atlasid = renderer->getAtlasId(hi);

        for (auto& world : main_world.allWorlds()) {
            auto& level = world.allLevels()[0];
            const auto& size = level.size;
            for (auto& layer : level.allLayers()) {
                if (!layer.hasTileset()) continue;
                auto tileset = layer.getTileset();
                auto atlas_id = renderer->getAtlasId("assets/" + tileset.path);
                // std::print("Atlas ID: {}\n", atlas_id);
                // auto atlas_id = atlasid;
                if (atlas_id == 255) std::print("Atlas ID for {} is invalid!\n", tileset.path);
                uint order = 0;
                // std::map<ldtk::IntPoint, std::pair<size_t, TexturedRectDescriptor>> tiles{};
                struct IntPoint {
                    int x, y;
                    IntPoint(const ldtk::IntPoint& o) : x(o.x), y(o.y) {}
                    IntPoint(IntPoint&&) = default;
                    IntPoint(const IntPoint& o) : x(o.x), y(o.y) {}
                    auto operator<=>(const IntPoint&) const = default;
                    auto operator<=>(const ldtk::IntPoint& o) const { return this->operator<=>(IntPoint{o}); }
                };
                std::map<IntPoint, std::pair<size_t, TexturedRectDescriptor>> tiles_to_do;
                for (auto& tile : layer.allTiles()) {
                    auto tilepos = tile.getGridPosition();
                    //if (tiles_to_do.contains(tilepos) && tiles_to_do[tilepos].first < order) continue;
                    auto texturerect = tile.getTextureRect();

                    float u0 = texturerect.x;
                    float u1 = texturerect.x + texturerect.width;
                    float v0 = texturerect.y + texturerect.height;  // your existing vertical flip
                    float v1 = texturerect.y;

                    if (tile.flipX) std::swap(u0, u1);
                    if (tile.flipY) std::swap(v0, v1);

                    // if (tiles.contains(tilepos) && ((tiles[tilepos].first) > order - 1)) continue;

                    const TexturedRectDescriptor rect = {
                        {(float)tilepos.x, (float)-tilepos.y, (float)tilepos.x + 1, (float)-tilepos.y + 1},
                        {u0, v0, u1, v1},
                        atlas_id,
                        static_cast<int8_t>(order)
                    };
                    make_square(rect);

                    //tiles_to_do[tilepos] = {order, rect};

                    order++;
                }

                //for (auto& x : tiles_to_do) make_square(x.second.second);
            }
        }

        std::print("Atlas ID={}", atlasid);

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

        // std::print("Real square:\n");
        // make_square(0, 0, 1, 1, 0, 16, 16, 0, atlasid);
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
