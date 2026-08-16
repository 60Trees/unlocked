#include <base/app.hpp>
#include <base/renderer.hpp>
#include <cmath>
#include <game/base/entity.hpp>
#include <game/base/entity_list.hpp>

#include <map>
#include <memory>
#include <print>
#include <LDtkLoader/Project.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>

#include <fs_utils.hpp>
#include <utility>
#include "game/base/world_handler.hpp"

#ifdef DEBUG_SCREEN
#include <imgui_impl_sdl3.h>
#endif

using namespace std;
using namespace Game;
using namespace Base;

struct Point {
    int x, y;
};

extern "C" double dt_multiplier();

using VertexArray = Renderer::VertexArray;

struct GameClass : Application {
    EntityList entities;
    EntityList::index_t camera_following_entity = EntityList::null_index;

    struct KeyboardControls {
        SDL_Scancode up = SDL_SCANCODE_W, down = SDL_SCANCODE_S, left = SDL_SCANCODE_A, right = SDL_SCANCODE_D, jump = SDL_SCANCODE_SPACE;
    };
    struct KeyboardEntityController : EntityController {
        KeyboardControls controls;
        span<const bool> keyboard;
        virtual void update_controls(Entity& own, const EntityList&, double deltaTime) const override {
            int8_t moving_dir_int = 0;
            const auto forced_dir = own.current_movement->forced_direction;
            if (keyboard[controls.left]) moving_dir_int -= 1;
            if (keyboard[controls.right]) moving_dir_int += 1;
            _disabled if (forced_dir) {
                if (forced_dir == LEFT) moving_dir_int = -1;
                if (forced_dir == RIGHT) moving_dir_int = 1;
            }
            Direction moving_dir = moving_dir_int == 0 ? own.current_movement->direction : moving_dir_int > 0;
            own.current_movement->direction = moving_dir;

            own.controls.up.update(deltaTime, keyboard[controls.up]);
            own.controls.down.update(deltaTime, keyboard[controls.down]);
            own.controls.left.update(deltaTime, !moving_dir && moving_dir_int != 0);
            own.controls.right.update(deltaTime, moving_dir && moving_dir_int != 0);
            // TOOD: Fix jump animation
            // own.controls.jump.update(deltaTime, keyboard[controls.jump]);
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
    shared_ptr<VertexArray> background = make_shared<VertexArray>();

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

    static constexpr std::string_view kDitherGradientPS = R"(
    struct Transform { offset: vec2f, scale: vec2f };
    @group(0) @binding(0) var<uniform> transform: Transform;
    @group(2) @binding(0) var<uniform> params: array<vec4f, 16>;

    const bayer4x4 = array<f32, 16>(
        0.0,  8.0,  2.0, 10.0,
        12.0,  4.0, 14.0,  6.0,
        3.0, 11.0,  1.0,  9.0,
        15.0,  7.0, 13.0,  5.0
    );

    @fragment fn fs_main(in: VSOut) -> @location(0) vec4f {
        let colourA   = params[0];        // top colour
        let colourB   = params[1];        // bottom colour
        let pixelSize = max(params[2].x, 1.0);

        let resolution = vec2f(2.0) / transform.scale;
        let blockPos   = floor(in.pos.xy / pixelSize) * pixelSize;
        let t          = clamp(blockPos.y / resolution.y, 0.0, 1.0);

        let bx = u32(blockPos.x / pixelSize) % 4u;
        let by = u32(blockPos.y / pixelSize) % 4u;
        let threshold = bayer4x4[by * 4u + bx] / 16.0;

        return mix(colourA, colourB, step(threshold, t));
    }
    )";

    struct DitherBgParams {
        float colourA[4];
        float colourB[4];
        float pixelSize;
        float _pad[3];
    };
    DitherBgParams ditherBgParams{};  // member of GameClass — must outlive the frame it's uploaded on

    static constexpr uint32_t pack_rgba_from_rrggbb(uint32_t rrggbb) {
        rrggbb &= 0xFFFFFF;  // ignore/require: only RRGGBB, top byte is never colour data
        uint8_t r = (rrggbb >> 16) & 0xFF;
        uint8_t g = (rrggbb >> 8) & 0xFF;
        uint8_t b = rrggbb & 0xFF;
        uint8_t a = 0xFF;
        return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
    }

    void set_dithered_gradient_background(uint32_t top, uint32_t bottom, float pixelSize = 4.0f) {
        auto unpack = [](uint32_t c, float* out) {
            out[0] = ((c >> 0) & 0xFF) / 255.0f;
            out[1] = ((c >> 8) & 0xFF) / 255.0f;
            out[2] = ((c >> 16) & 0xFF) / 255.0f;
            out[3] = ((c >> 24) & 0xFF) / 255.0f;
        };
        unpack(pack_rgba_from_rrggbb(top), ditherBgParams.colourA);
        unpack(pack_rgba_from_rrggbb(bottom), ditherBgParams.colourB);
        ditherBgParams.pixelSize = pixelSize;

        background->clear();
        background->push_back({});
        auto& layer = background->back();
        layer.material.vertex_shader = renderer->builtin_uispace_vshader();
        layer.material.pixel_shader = kDitherGradientPS;
        layer.material.screenspace = true;
        layer.material.blend_mode = Base::Renderer::Opaque;
        layer.material.params = std::as_bytes(std::span(&ditherBgParams, 1));
        make_screen_gradient_quad(0, 0, 0, 0, layer.vertices);  // vertex colour is unused by this shader
    }

    void set_gradient_background(uint32_t top, uint32_t bottom) {
        background->clear();
        background->push_back({});
        auto& layer = background->back();
        layer.material.vertex_shader = renderer->builtin_uispace_vshader();
        layer.material.pixel_shader = renderer->builtin_coloured_pshader();
        layer.material.screenspace = true;
        layer.material.blend_mode = Base::Renderer::Opaque;
        const auto top_packed = pack_rgba_from_rrggbb(top);
        const auto bottom_packed = pack_rgba_from_rrggbb(bottom);
        make_screen_gradient_quad(top_packed, top_packed, bottom_packed, bottom_packed, layer.vertices);
    }

    void set_solid_background(uint8_t r, uint8_t g, uint8_t b) {
        uint32_t packed = uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (0xFFu << 24);
        background->clear();
        background->push_back({});
        auto& layer = background->back();
        layer.material.vertex_shader = renderer->builtin_uispace_vshader();
        layer.material.pixel_shader = renderer->builtin_coloured_pshader();
        layer.material.screenspace = true;
        layer.material.blend_mode = Base::Renderer::Opaque;
        make_screen_gradient_quad(packed, packed, packed, packed, layer.vertices);
    }

    using Vertex = Base::Renderer::Vertex;
    using ScreenSpace = Base::Renderer::ScreenSpace;

    static void make_screen_gradient_quad(uint32_t tl, uint32_t tr, uint32_t bl, uint32_t br, std::vector<Vertex>& verts) {
        auto corner = [](ScreenSpace::AnchorPoint ap, uint32_t colour) {
            Vertex v{};
            v.pos.screen = ScreenSpace{0, 0, ap};
            v.shaderdata.rgba_combined = colour;
            return v;
        };
        Vertex a = corner(ScreenSpace::TOP_LEFT, tl);
        Vertex b = corner(ScreenSpace::BOTTOM_LEFT, bl);
        Vertex c = corner(ScreenSpace::BOTTOM_RIGHT, br);
        Vertex d = corner(ScreenSpace::TOP_RIGHT, tr);
        verts.insert(verts.end(), {a, b, c, a, c, d});
    }

    static constexpr uint32_t pack_rgba32(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
    }

    void update_renderer_layers() {
        renderqueue.clear();

        renderqueue.append(background);
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

        {
            const auto c = world_handler->main_world.getBgColor();
            set_solid_background(c.r, c.g, c.b);
        }

        world_handler->uploadAllTilesets(*renderer);

        world_handler->placed_levels.push_back({world_handler->getlevel(0, 0)});
        world_handler->placed_levels[0].render(*leveltris);

        update_renderer_layers();

        renderer->compile_default_shaders();

        renderer->addTextureFromBytes("assets/player.png", fs_helper::get_bytes_from_file<char>("assets/player.png"));

        // renderer->compile_used_shaders();

        players.push_back(entities.spawn_entity("player"));
        entities[players[0]].controller = make_unique<KeyboardEntityController>();
        camera_following_entity = players[0];

        size_t tris_i = 0;
        for (const auto& [entity_id, entity] : entities) {
            entitytrisindex.add_entity(entity_id);
            entity->render(*renderer, entitytris->at(entitytrisindex[entity_id]), 1.0 / 60.0);
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
        static bool slow_motion = false;
        const double dt = fps_counter->deltaTime * (slow_motion ? dt_multiplier() : 1);
        ASSUME(dt != NAN);
        ASSUME(dt > 0);
        renderer->camera.update_zoom(dt);
        renderer->loop();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    this->running = false;
                    break;

                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_ESCAPE) running = false;
                    if (event.key.key == SDLK_R) entities[players[0]].data.pos = {0,100};
                    if (event.key.key == SDLK_SPACE) entities[players[0]].data.vel *= 10;
                    if (event.key.key == SDLK_F) slow_motion = !slow_motion;
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
                renderer->camera.follow_point(entities[camera_following_entity].data.hitbox_center(), dt);
        }

        bool should_clean_entities = false;
        for (auto& [entity_id, entity] : entities) {
            if (!entity || entity->wants_to_despawn) {
                should_clean_entities = true;
                entitytrisindex.remove_entity(entity_id);
                continue;
            }
            entity->controller->update_controls(*entity, entities, dt);
            entity->tick_all(dt);
            if (entity->wants_to_despawn) should_clean_entities = true;

            if (!entitytrisindex.contains(entity_id)) entitytrisindex.add_entity(entity_id);
            entity->render(*renderer, entitytris->at(entitytrisindex[entity_id]), dt);
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
