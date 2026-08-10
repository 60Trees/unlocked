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
#include "game/base/world_handler.hpp"

using namespace std;
using namespace Game;
using namespace Base;

struct Point {
    int x, y;
};

using VertexArray = Renderer::VertexArray;

struct GameClass : Application {
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

    Renderer::RenderQueue renderqueue{};

    void update_renderer_layers() {
        renderqueue.clear();

        renderqueue.append(leveltris);
        renderqueue.append(entitytris);
    }

    vector<string> worldnames{};
    map<string, vector<string>> levelnames{};

    unique_ptr<Game::WorldHandler> world_handler{GetWorldHandler()};

    void init() override {
        print("Testing: {}", fs_helper::get_sview_from_file("assets/test.txt"));

        renderer->renderqueue = [&] -> Renderer::RenderQueue& { return renderqueue; };
        renderer->init();
        fps_counter->init();

        {
            const span<const unsigned char> file = fs_helper::get_bytes_from_file<unsigned char>("assets/main.ldtk");
            world_handler->main_world.loadFromMemory(file.data(), file.size());
            print("Loaded world\n");
        }

        world_handler->uploadAllTilesets(*renderer);

        world_handler->placed_levels.push_back({world_handler->getlevel(0,0)});
        world_handler->placed_levels[0].render(*leveltris);

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
                    if (event.key.key == SDLK_SPACE) entities[players[0]].data.vel *= 10;
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
                controller->keyboard = std::span<const bool>(data, (size_t)size);
            }

            if (entities.exists(camera_following_entity))
                renderer->camera.follow_point(entities[camera_following_entity].data.hitbox_center(), fps_counter->deltaTime);
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

GETTER_IMPL(Base::BaseClass, GetApplication, GameClass);
