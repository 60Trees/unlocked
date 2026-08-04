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

    typedef std::vector<Base::Renderer::Vertex> RenderLayer;

    // worldspace, textured
    std::vector<RenderLayer> leveltris;
    // worldspace, coloured
    RenderLayer shadowtris;

    void update_renderer_layers() {
        renderer->render_queue.clear();
        auto& q = renderer->render_queue;

        const auto worldspace = renderer->builtin_worldspace_vshader();
        const auto uispace = renderer->builtin_uispace_vshader();
        const auto textured = renderer->builtin_textured_pshader();
        const auto coloured = renderer->builtin_coloured_pshader();

        // q.push_back({shadowtris, {worldspace, coloured}});

        for (auto& layer : leveltris) q.push_back({layer, {worldspace, textured}});
        q.push_back({shadowtris, {worldspace, coloured, Base::Renderer::Material::Multiply}});
    }

    struct ColouredRectDescriptor {
        template <typename T>
        struct Rect {
            /// left, top, right, bottom
            T l, t, r, b;
        };
        template <typename T>
        struct RectCorners {
            T tl, tr, bl, br;
        };
        Rect<float> pos;
        RectCorners<uint32_t> colours;
        int8_t layer = 0;
    };
    struct TexturedRectDescriptor {
        template <typename T>
        struct Rect {
            /// left, top, right, bottom
            T l, t, r, b;
        };
        Rect<float> pos;
        Rect<uint16_t> uv;
        ushort atlas_index;
        int8_t layer = 0;
    };

    void make_coloured_square(ColouredRectDescriptor rect, std::vector<Base::Renderer::Vertex>& verts) {
        std::array<Base::Renderer::Vertex, 6> tris;

        for (auto& tri : tris) {
            tri.pos.world.depth = rect.layer;
        }

        tris[0].pos.world.x = rect.pos.l;
        tris[0].pos.world.y = rect.pos.t;
        tris[0].shaderdata.rgba_combined = rect.colours.tl;

        tris[1].pos.world.x = rect.pos.l;
        tris[1].pos.world.y = rect.pos.b;
        tris[1].shaderdata.rgba_combined = rect.colours.bl;

        tris[2].pos.world.x = rect.pos.r;
        tris[2].pos.world.y = rect.pos.b;
        tris[2].shaderdata.rgba_combined = rect.colours.br;

        tris[3] = tris[0];

        tris[4] = tris[2];

        tris[5].pos.world.x = rect.pos.r;
        tris[5].pos.world.y = rect.pos.t;
        tris[5].shaderdata.rgba_combined = rect.colours.br;

        for (auto& t : tris) verts.push_back(std::move(t));
    }
    void make_textured_square(TexturedRectDescriptor rect, std::vector<Base::Renderer::Vertex>& verts) {
        if (rect.atlas_index == 0) {
            std::print("atlasIndex is invalid!\n");
            return;
        }

        std::array<Base::Renderer::Vertex, 6> tris;

        for (auto& tri : tris) {
            tri.shaderdata.texture.texture_id = rect.atlas_index;
            tri.pos.world.depth = rect.layer;
        }

        tris[0].pos.world.x = rect.pos.l;
        tris[0].pos.world.y = rect.pos.t;
        tris[0].shaderdata.texture.u = rect.uv.l;
        tris[0].shaderdata.texture.v = rect.uv.t;

        tris[1].pos.world.x = rect.pos.l;
        tris[1].pos.world.y = rect.pos.b;
        tris[1].shaderdata.texture.u = rect.uv.l;
        tris[1].shaderdata.texture.v = rect.uv.b;

        tris[2].pos.world.x = rect.pos.r;
        tris[2].pos.world.y = rect.pos.b;
        tris[2].shaderdata.texture.u = rect.uv.r;
        tris[2].shaderdata.texture.v = rect.uv.b;

        tris[3] = tris[0];

        tris[4] = tris[2];

        tris[5].pos.world.x = rect.pos.r;
        tris[5].pos.world.y = rect.pos.t;
        tris[5].shaderdata.texture.u = rect.uv.r;
        tris[5].shaderdata.texture.v = rect.uv.t;

        for (auto& t : tris) verts.push_back(std::move(t));
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
                renderer->addTextureFromBytes(_path, fs_helper::get_bytes_from_file<char>(_path));
                hi = _path;
                std::print("- (id={})\n", renderer->getTextureID(_path));
            }
        }

        const auto atlasid = renderer->getTextureID(hi);

        leveltris.push_back({});
        make_textured_square({{0, 0, 1, 1}, {0, 128, 128, 0}, atlasid}, leveltris[0]);
        for (auto& world : main_world.allWorlds()) {
            auto& level = world.allLevels()[0];
            const auto& size = level.size;
            for (auto& layer : level.allLayers()) {
                if (!layer.hasTileset()) continue;
                leveltris.push_back({});
                auto tileset = layer.getTileset();
                auto atlas_id = renderer->getTextureID("assets/" + tileset.path);
                // std::print("Atlas ID: {}\n", atlas_id);
                // auto atlas_id = atlasid;
                if (atlas_id == 0) std::print("Atlas ID for {} is invalid!\n", tileset.path);
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
                    // if (tiles_to_do.contains(tilepos) && tiles_to_do[tilepos].first < order) continue;
                    auto texturerect = tile.getTextureRect();

                    TexturedRectDescriptor rect;
                    rect.pos = {(float)tilepos.x, (float)-tilepos.y, (float)tilepos.x + 1, (float)-tilepos.y + 1};
                    rect.atlas_index = atlas_id;
                    // rect.layer = (int8_t)order;

                    // u0
                    rect.uv.l = texturerect.x;
                    // u1
                    rect.uv.t = texturerect.y + texturerect.width;
                    // v0
                    rect.uv.r = texturerect.x + texturerect.height;  // your existing vertical flip
                    // v1
                    rect.uv.b = texturerect.y;

                    if (tile.flipX) std::swap(rect.uv.l, rect.uv.r);
                    if (tile.flipY) std::swap(rect.uv.t, rect.uv.b);

                    // if (tiles.contains(tilepos) && ((tiles[tilepos].first) > order - 1)) continue;

                    make_textured_square(rect, leveltris.back());

                    // tiles_to_do[tilepos] = {order, rect};

                    order++;
                }

                // for (auto& x : tiles_to_do) make_square(x.second.second);
            }
        }

        std::print("Atlas ID={}\n", atlasid);

        /*
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
        //*/

        // std::print("Real square:\n");

        update_renderer_layers();

        renderer->compile_all_shaders();
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
                    break;

                case SDL_EVENT_KEY_UP:
                    inputs.do_inputs(event.key.key, 0.0f);
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
