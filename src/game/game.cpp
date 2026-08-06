#include <base/app.hpp>
#include <base/renderer.hpp>
#include <game/base/entity.hpp>

#include <map>
#include <memory>
#include <print>
#include <LDtkLoader/Project.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>

#include <fs_utils.hpp>
#include <utility>

using namespace std;
using namespace Game;
using namespace Base;

struct Point {
    int x, y;
};

using VertexArray = Renderer::VertexArray;

struct GameClass : Application {
    string hi = "";
    EntityList entities;
    EntityList::index_t camera_following_entity = EntityList::null_index;

    struct KeyboardControls {
        SDL_Scancode up = SDL_SCANCODE_W, down = SDL_SCANCODE_S, left = SDL_SCANCODE_A, right = SDL_SCANCODE_D;
    };
    struct KeyboardEntityController : EntityController {
        KeyboardControls controls;
        span<const bool> keyboard;
        virtual void update_controls(Entity& own, const EntityList&) const override {
            own.controls.up = keyboard[controls.up];
            own.controls.down = keyboard[controls.down];
            own.controls.left = keyboard[controls.left];
            own.controls.right = keyboard[controls.right];
        }
    };
    vector<EntityList::index_t> players{};

    using TexturedRectDescriptor = Renderer::TexturedRectDescriptor;
    using ColouredRectDescriptor = Renderer::ColouredRectDescriptor;

    // New pixel shader for the saturation post effect.
    // prevTex, prevSamp, params, and VSOut are already declared by buildPostPipeline() —
    // this only needs to define fs_main.
    static constexpr string_view saturationPS = R"(
    @fragment fn fs_main(in: VSOut) -> @location(0) vec4f {
        let colour = textureSample(prevTex, prevSamp, in.uv);
        let sat = params[0].x;
        let luma = dot(colour.rgb, vec3f(0.2126, 0.7152, 0.0722));
        let outRGB = mix(vec3f(luma), colour.rgb, sat);
        return vec4f(outRGB, colour.a);
    }
    )";

    // 1.0 = unchanged, >1.0 = more saturated, 0.0 = grayscale.
    // Padded to 16 bytes just for clarity; kParamMax (256B) gives plenty of room if you add more fields later.
    struct SaturationParams {
        float saturation;
        float _pad[3];
    } saturationParams{1.0f};

    shared_ptr<VertexArray> leveltris = make_shared<VertexArray>();
    shared_ptr<VertexArray> entitytris = make_shared<VertexArray>();

    struct EntityTrisIndex {
        shared_ptr<VertexArray>& entitytris;

        inline auto begin() { return _raw.begin(); }
        inline auto begin() const { return _raw.begin(); }
        inline auto end() { return _raw.end(); }
        inline auto end() const { return _raw.end(); }
        inline auto rbegin() { return _raw.rbegin(); }
        inline auto rbegin() const { return _raw.rbegin(); }
        inline auto rend() { return _raw.rend(); }
        inline auto rend() const { return _raw.rend(); }

        void add_entity(EntityList::index_t entity_id) {
            if (_raw.contains(entity_id)) return;
            entitytris->push_back({});
            _raw[entity_id] = entitytris->size() - 1;
        }
        void remove_entity(EntityList::index_t entity_id) {
            if (!_raw.contains(entity_id)) return;
            if (_raw[entity_id] != entitytris->size() - 1) {
                EntityList::index_t entity_to_swap = entitytris->size() - 1;
                std::swap(_raw[entity_to_swap], _raw[entitytris->size() - 1]);
                _raw[entity_to_swap] = entity_id;
            }
            _raw.erase(entitytris->size() - 1);
            entitytris->pop_back();
        }
        [[nodiscard]] inline bool contains(EntityList::index_t entity_id) const { return _raw.contains(entity_id); }
        [[nodiscard]] inline const auto& operator[](EntityList::index_t entity_id) const { return _raw.at(entity_id); }
        [[nodiscard]] inline auto& operator[](EntityList::index_t entity_id) { return _raw.at(entity_id); }

        map<EntityList::index_t, size_t> _raw{};
    } entitytrisindex{entitytris};

    void update_renderer_layers() {
        auto& q = renderer->render_queue;
        q.clear();

        q.append(entitytris);
        q.append(leveltris);
    }

    void init() override {
        renderer->init();
        fps_counter->init();

        ldtk::Project main_world;
        {
            const span<const unsigned char> file = fs_helper::get_bytes_from_file<unsigned char>("assets/main.ldtk");
            main_world.loadFromMemory(file.data(), file.size());
            print("Loaded world\n");
        }

        print("Testing: {}", fs_helper::get_sview_from_file("assets/test.txt"));

        for (auto& tileset : main_world.allTilesets()) {
            print("Tileset\n- {}\n", tileset.path);
            // The path cannot be outside the embedded filesystem (the one in the binary)
            if (tileset.path.starts_with("../"))
                print("- (INVALID PATH)\n");
            else {
                // This means that the path mentioned is inside the `fs` so it can be fetched
                string _path = "assets/" + tileset.path;
                renderer->addTextureFromBytes(_path, fs_helper::get_bytes_from_file<char>(_path));
                hi = _path;
                print("- (id={})\n", renderer->getTextureID(_path));
            }
        }

        const auto atlasid = renderer->getTextureID(hi);

        // leveltris->push_back({});
        // Renderer::make_textured_square({{0, 0, 1, 1}, {0, 128, 128, 0}, atlasid}, leveltris->at(0));
        for (auto& world : main_world.allWorlds()) {
            auto& level = world.allLevels()[0];
            const auto& size = level.size;
            for (auto& layer : level.allLayers()) {
                if (!layer.hasTileset()) continue;
                leveltris->push_back({});
                leveltris->back().material.pixel_shader = renderer->builtin_textured_pshader();
                leveltris->back().material.vertex_shader = renderer->builtin_worldspace_vshader();
                auto tileset = layer.getTileset();
                auto atlas_id = renderer->getTextureID("assets/" + tileset.path);
                // print("Atlas ID: {}\n", atlas_id);
                // auto atlas_id = atlasid;
                if (atlas_id == 0) print("Atlas ID for {} is invalid!\n", tileset.path);
                uint order = 0;
                // map<ldtk::IntPoint, pair<size_t, TexturedRectDescriptor>> tiles{};
                struct IntPoint {
                    int x, y;
                    IntPoint(const ldtk::IntPoint& o) : x(o.x), y(o.y) {}
                    IntPoint(IntPoint&&) = default;
                    IntPoint(const IntPoint& o) : x(o.x), y(o.y) {}
                    auto operator<=>(const IntPoint&) const = default;
                    auto operator<=>(const ldtk::IntPoint& o) const { return this->operator<=>(IntPoint{o}); }
                };
                map<IntPoint, pair<size_t, TexturedRectDescriptor>> tiles_to_do;

                for (auto& tile : layer.allTiles()) {
                    auto tilepos = tile.getPosition();
                    auto tilesize = layer.getCellSize();
                    float scaloid = 1.0f;
                    // if (tiles_to_do.contains(tilepos) && tiles_to_do[tilepos].first < order) continue;
                    auto texturerect = tile.getTextureRect();

                    TexturedRectDescriptor rect;
                    rect.pos = {(float)tilepos.x, (float)-tilepos.y * scaloid, ((float)tilepos.x + tilesize) * scaloid,
                        ((float)-tilepos.y + tilesize) * scaloid};
                    rect.atlas_index = atlas_id;
                    // rect.layer = (int8_t)order;

                    // u0
                    rect.uv.l = texturerect.x;
                    // u1
                    rect.uv.t = texturerect.y + texturerect.height;
                    // v0
                    rect.uv.r = texturerect.x + texturerect.width;  // your existing vertical flip
                    // v1
                    rect.uv.b = texturerect.y;

                    if (tile.flipX) swap(rect.uv.l, rect.uv.r);
                    if (tile.flipY) swap(rect.uv.t, rect.uv.b);

                    // if (tiles.contains(tilepos) && ((tiles[tilepos].first) > order - 1)) continue;

                    Renderer::make_textured_square(rect, leveltris->back().vertices);

                    // tiles_to_do[tilepos] = {order, rect};

                    order++;
                }

                // for (auto& x : tiles_to_do) make_square(x.second.second);
            }
        }

        print("Atlas ID={}\n", atlasid);

        /*
        Renderer::ColouredVertex tri[3];
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

        // print("Real square:\n");

        update_renderer_layers();

        renderer->compile_default_shaders();
        // renderer->compile_used_shaders();

        players.push_back(entities.spawn_entity("player"));
        entities[players[0]].controller = make_unique<KeyboardEntityController>();
        camera_following_entity = players[0];

        size_t tris_i = 0;
        for (const auto& [entity_id, entity] : entities) {
            entitytrisindex.add_entity(entity_id);
            entity->render(*renderer, entitytris->at(entitytrisindex[entity_id]));
        }
    }

    struct {
        float up, down, left, right;
        void do_inputs(const SDL_Keycode keycode, float speed) {
            switch (keycode) {
                case SDLK_UP:
                    up = speed;
                    break;
                case SDLK_DOWN:
                    down = speed;
                    break;
                case SDLK_LEFT:
                    left = speed;
                    break;
                case SDLK_RIGHT:
                    right = speed;
                    break;
            }
        }
    } inputs;

    void loop() override {
        fps_counter->loop();
        renderer->camera.update_zoom(fps_counter->deltaTime);
        renderer->loop();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    this->running = false;
                    break;

                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_ESCAPE) running = false;
                    inputs.do_inputs(event.key.key, 16.0f);
                    break;

                case SDL_EVENT_KEY_UP:
                    inputs.do_inputs(event.key.key, 0.0f);
                    break;
            }
        }

        for (const auto i : players) {
            auto& player = entities[i];
            if (auto* controller = dynamic_cast<KeyboardEntityController*>(player.controller.get())) {
                int size;
                const bool* data = SDL_GetKeyboardState(&size);
                controller->keyboard = span<const bool>{data, (size_t)size};
            }

            if (entities.exists(camera_following_entity))
                renderer->camera.follow_point(entities[camera_following_entity].data.hitbox_center(), fps_counter->deltaTime);

            // renderer->camera.x += (inputs.right - inputs.left) * fps_counter->deltaTime;
            // renderer->camera.y += (inputs.up - inputs.down) * fps_counter->deltaTime;
        }

        bool should_clean_entities = false;
        for (auto& [entity_id, entity] : entities) {
            if (!entity || entity->wants_to_despawn) {
                should_clean_entities = true;
                entitytrisindex.remove_entity(entity_id);
                continue;
            }
            entity->controller->update_controls(*entity, entities);
            entity->tick_all(fps_counter->deltaTime);
            if (entity->wants_to_despawn) should_clean_entities = true;

            if (!entitytrisindex.contains(entity_id)) entitytrisindex.add_entity(entity_id);
            entity->render(*renderer, entitytris->at(entitytrisindex[entity_id]));
        }
        if (should_clean_entities) entities.clean_entities();

        check_running(renderer.get());
        check_running(fps_counter.get());
        if (!running) return;
    }

    void quit() override {
        fps_counter->quit();
        renderer->quit();
    }
};

extern "C" BaseClass* GetApplication() { return new GameClass(); }
