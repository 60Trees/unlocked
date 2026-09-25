/*
 * @file src/game/game.cpp
 * @author 60Trees_ (github.com/60Trees)
 */

// #define what_is_going_on 1000

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
#include <glm/common.hpp>
#include <utility>
#include "LDtkLoader/DataTypes.hpp"
#include "SDL3/SDL_keycode.h"
#include "game/base/world_handler.hpp"
#include "utils.hpp"

#include "game/systems/light_shafts.hpp"
#include "game/systems/bloom.hpp"
#include "game/systems/edge_glow.hpp"

#include <imgui_impl_sdl3.h>

using namespace std;
using namespace Game;
using namespace Base;

struct Point {
    int x, y;
};

extern "C" double dt_multiplier();

using VertexArray = Renderer::VertexArray;
// Same test as Entity::colliding_with, but for two raw Hitboxes -- the Finish/NoFinish/
// ShareFinish bounds are never spawned as real Entities, so there's no Entity to compare.
static bool hitboxes_overlap(const Hitbox& a, const Hitbox& b) {
    return a.left() < b.right() && a.right() > b.left() && a.bottom() < b.top() && a.top() > b.bottom();
}

static Hitbox::vec2_t bottom_mid(const Hitbox& h) { return {(h.left() + h.right()) / 2.0, h.bottom()}; }

// Translates `h` (preserving size/anchor) so its bottom-mid point lands on `target`.
static void snap_bottom_mid_to(Hitbox& h, Hitbox::vec2_t target) { h.pos += target - bottom_mid(h); }

#ifdef what_is_going_on
struct RandomController : EntityController {
    uint seed = 0;
    uint tick_count = 0;
    Duration time_left = 0.0;
    enum { WAIT, MOVE_LEFT, MOVE_RIGHT, JUMP, SHOOT } Action;
    RandomController(const RandomController& oth)
        : EntityController(oth), seed(oth.seed), tick_count(oth.tick_count), time_left(oth.time_left) {}
    RandomController() = default;
    std::unique_ptr<EntityController> clone() override { return std::make_unique<RandomController>(*this); }
    virtual void update_controls(Entity& own, const Application& app) override {
        const auto deltaTime = app.get<FpsCounter>().deltaTime;
        const auto& r = app.get<Renderer>();

        time_left -= deltaTime;
        if (time_left < 0) {
            time_left = Random::real(1.0, 3.0, 2);
            seed++;
            Action = static_cast<typeof(Action)>(visualRandom(&own + seed, 0, 5));
            seed++;
        }

        {
            int8_t moving_dir_int = Action == MOVE_LEFT ? -1 : (Action == MOVE_RIGHT ? 1 : 0);
            const auto forced_dir = own.movement->forced_direction;
            {
                if (forced_dir == LEFT) moving_dir_int = -1;
                if (forced_dir == RIGHT) moving_dir_int = 1;
            }
            Direction moving_dir = moving_dir_int == 0 ? own.movement->direction : moving_dir_int > 0;
            own.movement->direction = moving_dir;
            own.controls.left.update(app, !moving_dir && moving_dir_int != 0);
            own.controls.right.update(app, moving_dir && moving_dir_int != 0);
        }

        if (Action == SHOOT) {
            own.controls.focusDegrees = mth::fmod(visualRandom(&own + seed, 0, 360) + 360, 360);
            seed++;
        }
        // own.controls.boost.update(app, Action == SHOOT);
        own.controls.boost.update(app, tick_count % 3 == 0);
        time_left = 0;

        own.controls.jump.update(app, Action == JUMP);

        tick_count++;
    }
};
#endif

// my laptop keyboard has leaf bits in it
// its arrow keys are broken ):
// #define ARROW_KEYS_BROKEN

struct KeyboardEntityController : EntityController {
    struct KeyboardControls {
        SDL_Scancode left = SDL_SCANCODE_A, right = SDL_SCANCODE_D, jump = SDL_SCANCODE_W, boost = SDL_SCANCODE_SPACE;

#ifdef ARROW_KEYS_BROKEN
        // this is for numpad
        SDL_Scancode aim_up = SDL_SCANCODE_KP_5, aim_down = SDL_SCANCODE_KP_2, aim_left = SDL_SCANCODE_KP_1, aim_right = SDL_SCANCODE_KP_3;
#else
        SDL_Scancode aim_up = SDL_SCANCODE_UP, aim_down = SDL_SCANCODE_DOWN, aim_left = SDL_SCANCODE_LEFT, aim_right = SDL_SCANCODE_RIGHT;
#endif
    };

    KeyboardControls controls;
    span<const bool> keyboard;

    KeyboardEntityController(const KeyboardEntityController& oth) : EntityController(oth), controls(oth.controls) {}
    KeyboardEntityController() = default;
    std::unique_ptr<EntityController> clone() override { return std::make_unique<KeyboardEntityController>(*this); }

    /// NAN = not being held, INFINITY = being held but opressed
    float get_aim_direction() {
        int8_t vertical = 0, horizontal = 0;
        if (keyboard[controls.aim_up]) vertical += 1;
        if (keyboard[controls.aim_down]) vertical -= 1;
        if (keyboard[controls.aim_left]) horizontal -= 1;
        if (keyboard[controls.aim_right]) horizontal += 1;

        if (vertical == 0 && horizontal == 0) {
            force_stop_aiming = false;
            return NAN;
        }
        if (force_stop_aiming) return INFINITY;

        if (vertical > 0) return (horizontal * 45 + 360) % 360;
        if (vertical < 0) return (-horizontal * 45 + 360 + 180) % 360;
        return (horizontal * 90 + 360) % 360;
    }
    bool slowmo;
    bool force_stop_aiming = false;

    bool should_slow_motion() override { return slowmo; }

    virtual void update_controls(Entity& own, const Application& app) override {
        const auto deltaTime = app.get<FpsCounter>().deltaTime;
        const auto& r = app.get<Renderer>();

        {
            int8_t moving_dir_int = 0;
            const auto forced_dir = own.movement->forced_direction;
            if (keyboard[controls.left]) moving_dir_int -= 1;
            if (keyboard[controls.right]) moving_dir_int += 1;
            if (forced_dir) {
                if (forced_dir == LEFT) moving_dir_int = -1;
                if (forced_dir == RIGHT) moving_dir_int = 1;
            }
            Direction moving_dir = moving_dir_int == 0 ? own.movement->direction : moving_dir_int > 0;
            own.movement->direction = moving_dir;
            own.controls.left.update(app, !moving_dir && moving_dir_int != 0);
            own.controls.right.update(app, moving_dir && moving_dir_int != 0);
        }

        const auto aim_dir = get_aim_direction();
        const bool is_aiming = !std::isnan(aim_dir) && !std::isinf(aim_dir);

        own.controls.boost.update(app, [&] {
            // if (keyboard[controls.boost]) force_stop_aiming = true;
            return keyboard[controls.boost];
        }());

        if (!is_aiming)
            slowmo = false;
        else {
            own.controls.focusDegrees = aim_dir;
            slowmo = true;
        }

        own.controls.jump.update(app, keyboard[controls.jump]);
    }
};
#include <format>
#include <string>
#include <cmath>

static std::string format_timer(double seconds) {
    const auto total_seconds = static_cast<long long>(seconds);

    const long long days = total_seconds / 86400;
    const long long hours = (total_seconds % 86400) / 3600;
    const long long minutes = (total_seconds % 3600) / 60;
    const long long secs = total_seconds % 60;

    // Get fractional part.
    double fraction = seconds - static_cast<double>(total_seconds);

    // Convert to decimal digits.
    std::string fraction_str = std::format("{:.9f}", fraction);

    // Remove "0."
    fraction_str.erase(0, 2);

    // Remove trailing zeroes.
    while (!fraction_str.empty() && fraction_str.back() == '0') fraction_str.pop_back();

    std::string result = "\\red";

    if (days > 0) result += std::format("{}:", days);

    if (hours > 0 || days > 0) result += std::format("{:02}:", hours);

    if (minutes > 0 || hours > 0 || days > 0) result += std::format("{:02}:", minutes);

    result += std::format("{:02}", secs);

    if (!fraction_str.empty()) result += ":\\normal" + fraction_str;

    return result;
}

struct GameClass : Application {
    GameClass() { this->renderer->parent = this; }
    LightShaftSystem light_shafts;
    EdgeGlowSystem edge_glow;
    BloomSystem bloom;

    Game::EntityList& entities = [&] -> Game::EntityList& {
        this->ensure_class_added<Game::EntityList>([] { return new Game::EntityList(); });
        return this->get<Game::EntityList>();
    }();

    vector<EntityList::index_t> players{};

    using TexturedRectDescriptor = Renderer::TexturedRectDescriptor;
    using ColouredRectDescriptor = Renderer::ColouredRectDescriptor;

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

            size_t idx = _raw[entity_id];
            size_t last = entitytris->size() - 1;

            if (idx != last) {
                // swap vertex data
                std::swap((*entitytris)[idx], (*entitytris)[last]);

                // fix index map
                EntityList::index_t swapped_entity = 0;
                for (auto& [eid, i] : _raw)
                    if (i == last) swapped_entity = eid;

                _raw[swapped_entity] = idx;
            }

            _raw.erase(entity_id);
            entitytris->pop_back();
        }
        [[nodiscard]] inline bool contains(EntityList::index_t entity_id) const { return _raw.contains(entity_id); }
        [[nodiscard]] inline const auto& operator[](EntityList::index_t entity_id) const { return _raw.at(entity_id); }
        [[nodiscard]] inline auto& operator[](EntityList::index_t entity_id) { return _raw.at(entity_id); }

        map<EntityList::index_t, size_t> _raw{};
    } entitytrisindex{entitytris};

    Renderer::RenderQueue renderqueue{};

    static constexpr uint32_t pack_rgba_from_rrggbb(uint32_t rrggbb) {
        rrggbb &= 0xFFFFFF;  // ignore/require: only RRGGBB, top byte is never colour data
        uint8_t r = (rrggbb >> 16) & 0xFF;
        uint8_t g = (rrggbb >> 8) & 0xFF;
        uint8_t b = rrggbb & 0xFF;
        uint8_t a = 0xFF;
        return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
    }

    struct PrevBG {
        union Data {
            struct Dithered {
                uint32_t top, bottom;
                float pixelSize;
            } dithered;
            struct Gradient {
                uint32_t top, bottom;
            } gradient;
            struct Solid {
                uint8_t r, g, b;
            } solid;
        } data;
        enum { Dithered, Gradient, Solid, None } type = None;

        auto operator==(const PrevBG& oth) const { return memcmp(this, &oth, sizeof(*this)); }
    } prev_bg;

    void set_solid_background(uint8_t r, uint8_t g, uint8_t b) {
        {
            PrevBG thisbg;
            thisbg.type = thisbg.Solid;
            thisbg.data.solid = {r, g, b};
            if (prev_bg == thisbg) return;
            prev_bg = thisbg;
        }
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

    WorldHandler& world_handler = [&] -> WorldHandler& {
        this->ensure_class_added<WorldHandler>([] { return GetWorldHandler(); });
        return this->get<WorldHandler>();
    }();

    struct LevelData : PlacedLevelData {
        vector<glm::vec<2, double>> player_starts;
        vector<Renderer::CameraBound> camera_bounds;

        std::optional<Hitbox> finish;      // BOTTOM_MID anchored, at most one
        std::vector<Hitbox> no_finish;     // TOP_LEFT anchored
        std::vector<Hitbox> share_finish;  // TOP_LEFT anchored
    };

    static LevelData& level_data_of(PlacedLevel& placed) { return *dynamic_cast<LevelData*>(placed.usrdata.get()); }

    const map<uint, string> kDefaultTileGroups = {
        {0, "Air"}, {1, "Solid"}, {2, "Solid"}, {3, "Solid"}, {5, "Solid"}, {4, "Death"}, {6, "Solid"}, {7, "Solid"}};

    bool entity_triggers_finish(const Entity& e, const LevelData& level_data) {
        if (!level_data.finish || !e.has_attribute("canfinish")) return false;
        if (!hitboxes_overlap(e.data, *level_data.finish)) return false;

        for (const auto& no_finish_bound : level_data.no_finish)
            if (hitboxes_overlap(e.data, no_finish_bound)) return false;

        return true;
    }

    std::vector<std::unique_ptr<Entity>> checkpoint_entities{};
    Entity* checkpoint_entities_main_character = nullptr;
    std::map<const Entity*, std::vector<Entity*>> checkpoint_parent_child_map{};
    Renderer::Camera checkpoint_camera;
    bool has_reached_checkpoint = false;

    void do_level_switch(const ldtk::Level& current_ldtk_level, const LevelData& level_data, bool is_dead) {
        if (!is_dead) has_reached_checkpoint = true;
        const std::vector<Hitbox> share_finish_bounds = level_data.share_finish;
        const auto prev_player_end = level_data.finish;
        EntityList& entities = get<EntityList>();

        std::vector<EntityList::index_t> to_move_entities;
        for (auto& [entity_id, entity_ptr] : entities) {
            if (!entity_ptr) continue;

            Entity& e = *entity_ptr;

            const bool overlaps_share_finish = std::any_of(share_finish_bounds.begin(), share_finish_bounds.end(),
                [&](const Hitbox& bound) { return hitboxes_overlap(e.data, bound); });

            if (is_dead || !overlaps_share_finish || !e.transfers_to_new_level()) {
                e.wants_to_despawn = true;
                entitytrisindex.remove_entity(entity_id);
                continue;
            }
            to_move_entities.push_back(entity_id);
        }
        entities.clean_entities();

        if (is_dead) {
            std::map<const Entity*, std::vector<Entity*>> new_parent_child_map{};
            for (auto& checkpt_entity : checkpoint_entities) {
                if (!checkpt_entity) continue;
                size_t idx = entities.get_empty_index();
                if (checkpt_entity.get() == checkpoint_entities_main_character) entities.main_character = idx;
                entities.data[idx] = std::unique_ptr<Entity>(checkpt_entity->clone([&](Entity* child, const Entity* parent) {
                    if (!parent) return;
                    if (!new_parent_child_map.contains(parent)) new_parent_child_map[parent] = {};
                    new_parent_child_map[parent].push_back(child);
                }));
            }

            for (auto& [parent_ptr, children] : new_parent_child_map) {
                for (Entity* child : children) {
                    size_t cidx = entities.get_empty_index();
                    entities.data[cidx] = std::unique_ptr<Entity>(child);
                }
            }
        }

        // loads and populates next level
        PlacedLevel* next_level = [this, is_dead](const ldtk::Level& current_ldtk_level) -> PlacedLevel* {
            const auto& world = world_handler.getworld((size_t)0);

            size_t current_index = 0;
            bool found = false;
            for (const auto& l : world.allLevels()) {
                if (&l == &current_ldtk_level) {
                    found = true;
                    break;
                }
                current_index++;
            }
            if (!found) return nullptr;

            // if its dead then next_index == current_index
            const size_t next_index = current_index + (is_dead ? 0 : 1);
            if (next_index >= world.allLevels().size()) return nullptr;  // current level is the last one in the world

            const ldtk::Level& next_ldtk_level = world_handler.getlevel(next_index, world);

            world_handler.placed_levels.clear();
            renderer->camera_bound = nullptr;
            if (!is_dead) leveltris->clear();

            glm::vec<2, int> offset = {0, 0};
            auto level_data_ptr = make_shared<LevelData>();
            world_handler.placed_levels.push_back(PlacedLevel{next_ldtk_level, offset, level_data_ptr, kDefaultTileGroups});
            PlacedLevel& placed = world_handler.placed_levels.back();

            world_handler.render(next_ldtk_level, *leveltris, *renderer, offset);
            auto& tm = world_handler.all_level_tilemaps[&next_ldtk_level];
            light_shafts.bake_level(*renderer, tm);
            edge_glow.bake_level(*renderer, tm);

            populate_level(placed, *level_data_ptr);

            return &placed;
        }(current_ldtk_level);
        if (!next_level) return;
        LevelData& next_data = level_data_of(*next_level);
        if (next_data.player_starts.empty()) return;

        {  // Moves surviving entities (after removing and after loads / populates)
            const auto player_start = next_data.player_starts[0];

            auto shift_pos = [&](glm::vec<2, double>& pos) {
                if (!prev_player_end) {
                    pos = player_start;
                    return;
                }

                pos -= prev_player_end->pos;
                pos += player_start;
            };
            auto shift_x = [&](float& x) {
                if (!prev_player_end) {
                    x = player_start.x;
                    return;
                }

                x -= prev_player_end->pos.x;
                x += player_start.x;
            };
            auto shift_y = [&](float& y) {
                if (!prev_player_end) {
                    y = player_start.y;
                    return;
                }

                y -= prev_player_end->pos.y;
                y += player_start.y;
            };

            if (!is_dead) {
                for (auto id : to_move_entities) {
                    if (entities.exists(id)) shift_pos(entities[id].data.pos);
                }

                checkpoint_entities.clear();
                checkpoint_entities_main_character = nullptr;
                for (auto id : to_move_entities) {
                    if (!entities.exists(id)) continue;
                    Entity& e = entities[id];
                    if (e.parent) continue;

                    checkpoint_entities.push_back(std::unique_ptr<Entity>(e.clone([&](Entity* child, const Entity* parent) {
                        if (!parent) return;
                        if (!checkpoint_parent_child_map.contains(parent)) checkpoint_parent_child_map[parent] = {};
                        checkpoint_parent_child_map[parent].push_back(child);
                    })));
                    if (id == entities.main_character) checkpoint_entities_main_character = checkpoint_entities.back().get();
                }
            }

            for (auto& [entity_id, entity_ptr] : entities) {
                entitytrisindex.add_entity(entity_id);
                entity_ptr->render(*this, entitytris->at(entitytrisindex[entity_id]));
            }

            shift_x(renderer->camera.x);
            shift_x(renderer->camera.target_x);
            shift_y(renderer->camera.y);
            shift_y(renderer->camera.target_y);

            renderer->camera.zoom_freeze_frames += 3;
            renderer->camera.camera_freeze_frames += 3;
            if (!is_dead) {
                checkpoint_camera = renderer->camera;
                checkpoint_camera.screenshake = 0;
                renderer->camera.screenshake += 1;
            } else
                renderer->camera = checkpoint_camera;
        }
    }

    void populate_level(const Game::PlacedLevel& placed_level, LevelData& level_data) {
        EntityList& entities = get<EntityList>();
        const auto c = placed_level.level.bg_color;
        set_solid_background(c.r, c.g, c.b);

        const auto handle_entity = [&](const ldtk::Entity& entity, const std::string& name) {
            if (name == "PlayerStart") {
                if (!level_data.player_starts.empty()) throw runtime_error("Only one PlayerStart entity is allowed per level!");

                const auto pos = entity.getPosition();
                level_data.player_starts.push_back({pos.x, -pos.y});
                return;
            }

            if (name == "Finish" || name == "NoFinish" || name == "ShareFinish") {
                if (name == "Finish" && level_data.finish) throw runtime_error("Only one Finish entity is allowed per level!");

                auto size = entity.getSize();
                auto pos = entity.getPosition();
                pos.y *= -1;

                Hitbox bound;
                bound.size = {size.x, size.y};
                bound.pos = {pos.x + placed_level.offset.x, pos.y + placed_level.offset.y};
                bound.anchor_point = (name == "Finish") ? Hitbox::BOTTOM_MID : Hitbox::TOP_LEFT;

                if (name == "Finish")
                    level_data.finish = bound;
                else if (name == "NoFinish")
                    level_data.no_finish.push_back(bound);
                else
                    level_data.share_finish.push_back(bound);

                return;
            }

            if (name.contains("Camera")) {
                auto size = entity.getSize();
                auto pos = entity.getPosition();
                pos.y *= -1;
                if (name == "CameraLevelBound") {
                    level_data.camera_bounds.push_back(Renderer::CameraBound{
                        .lock_zoom = false,
                        .snap_in_bounds = entity.getField<bool>("ForceOut").value(),
                        .snappy = false,
                        .center_at = {},
                        .zoom_level = -1,
                        .priority = entity.getField<ldtk::FieldType::Float>("Priority").value(),
                    });
                } else if (name == "CameraLooseBound") {
                    level_data.camera_bounds.push_back(Renderer::CameraBound{
                        .lock_zoom = false,
                        .snap_in_bounds = entity.getField<bool>("ForceOut").value(),
                        .snappy = entity.getField<bool>("Snappy").value(),
                        .center_at = {},
                        .zoom_level = entity.getField<float>("ZoomLevel").value_or(-1),
                        .priority = entity.getField<ldtk::FieldType::Float>("Priority").value(),
                    });
                } else if (name == "CameraZoomOutBound") {
                    level_data.camera_bounds.push_back(Renderer::CameraBound{
                        .lock_zoom = true,
                        .snap_in_bounds = entity.getField<bool>("ForceOut").value(),
                        .snappy = entity.getField<bool>("Snappy").value_or(false),
                        .center_at = {},
                        .zoom_level = -1,
                        .priority = entity.getField<ldtk::FieldType::Float>("Priority").value(),
                    });
                } else if (name == "CameraLockedBound") {
                    level_data.camera_bounds.push_back(Renderer::CameraBound{
                        .lock_zoom = true,
                        .snap_in_bounds = entity.getField<bool>("ForceOut").value(),
                        .snappy = entity.getField<bool>("Snappy").value(),
                        .center_at = {},
                        .zoom_level = -1,
                        .priority = entity.getField<ldtk::FieldType::Float>("Priority").value(),
                    });
                } else if (name == "CameraCenterHere") {
                    return;
                } else
                    throw runtime_error("Unknown camera bound: " + name +
                                        ". (Hint: Don't include \"camera\" in the entity name if it's not a camera bound)");

                auto& bound = level_data.camera_bounds.back();
                bound.x = pos.x + placed_level.offset.x;
                bound.y = pos.y + placed_level.offset.y;
                bound.w = size.x;
                bound.h = size.y;

                return;
            }

            // if (name == "Essence")
            //     for (int i = 0; i < 50; i++) entities.spawn_entity(name, &entity);
            entities.spawn_entity(name, &entity);
        };

        for (const auto& layer : placed_level.level.allLayers())
            for (const auto& entity : layer.allEntities()) handle_entity(entity, entity.getName());
    }

    void init() override {
        print("Testing: {}", fs_helper::get_sview_from_file("assets/test.txt"));

        renderer->renderqueue = [&] -> Renderer::RenderQueue& { return renderqueue; };
        renderer->init();
        fps_counter->init();

        fps_counter->deltaTime = 1.0 / 60.0;

        ensure_class_added<EntityList>([] { return new EntityList(); });

        EntityList& entities = get<EntityList>();

        for (auto& baseclass : classes) baseclass->init();

        {
            const span<const unsigned char> file = fs_helper::get_bytes_from_file<unsigned char>("assets/main.ldtk");
            world_handler.loadFromMemory(file);
            print("Loaded world\n");
        }

        world_handler.uploadAllTilesets(*renderer);

        world_handler.placed_levels.push_back(
            PlacedLevel{world_handler.getlevel(0, 0), {0, 0}, make_shared<LevelData>(), kDefaultTileGroups});
        world_handler.renderDirtyLevels(*leveltris);
        light_shafts.bake_level(*renderer, world_handler.all_level_tilemaps[&world_handler.placed_levels[0].level]);
        light_shafts.register_post_effect(*renderer);
        edge_glow.bake_level(*renderer, world_handler.all_level_tilemaps[&world_handler.placed_levels[0].level]);
        edge_glow.register_post_effect(*renderer);
        bloom.register_post_effect(*renderer);

        auto& level_data = level_data_of(world_handler.placed_levels[0]);

        update_renderer_layers();

        renderer->compile_default_shaders();

        renderer->addTextureFromBytes("assets/player.png", fs_helper::get_bytes_from_file<char>("assets/player.png"));

        // renderer->compile_used_shaders();

        [&](size_t i) {
            Entity& player = entities[i];
            players.push_back(i);
            entities.main_character = i;

            populate_level(world_handler.placed_levels[0], level_data);

            if (level_data.player_starts.empty()) level_data.player_starts.push_back({0, 100});

            player.data.pos = level_data.player_starts[0];
            player.controller = make_unique<KeyboardEntityController>();
#ifdef what_is_going_on
            for (int i = 0; i < what_is_going_on; i++) {
                [&](Entity& e) {
                    e.data.pos = player.data.pos;
                    e.controller = make_unique<RandomController>();
                }(entities[entities.spawn_entity("player")]);
            }
#endif
        }(entities.spawn_entity("player"));

        size_t tris_i = 0;
        for (const auto& [entity_id, entity] : entities) {
            entitytrisindex.add_entity(entity_id);
            entity->render(*this, entitytris->at(entitytrisindex[entity_id]));
        }

        renderer->camera.zoom_freeze_frames = 3;
        renderer->camera.camera_freeze_frames = 3;
    }

    bool slow_motion = false;
    bool did_main_die = false;
    bool is_speedrunning = false;
    bool increment_timer = true;
    double timer = 0;
    std::map<size_t, double> level_times{};
    unsigned int deaths = 0;
    bool just_died = false;
    bool is_dead = false;

    void loop() override {
        fps_counter->loop();
        size_t current_index = 0;
        {
            const auto& world = world_handler.getworld((size_t)0);

            bool found = false;
            for (const auto& l : world.allLevels()) {
                if (&l == &world_handler.placed_levels[0].level) {
                    found = true;
                    break;
                }
                current_index++;
            }

            if (increment_timer && current_index <= 7 && current_index > 0) timer += fps_counter->deltaTime;
        }
        fps_counter->deltaTime *= (slow_motion ? dt_multiplier() : 0.9);
        const double dt = fps_counter->deltaTime;
        ASSUME(dt != NAN);
        ASSUME(dt > 0);

        is_speedrunning = has_flag("speedrun");

        if (is_speedrunning) {
            if (timer == 0)
                debug_screen('t', "Timer: Hasn't started yet");
            else {
                debug_screen('t', "");
                for (auto [idx, time] : level_times) {
                    if (idx < 1 || idx > 7) continue;
                    debug_screen('t' << idx, "Level " << idx << " timer: " << format_timer(time));
                }
            }
            debug_screen('d', "Deaths: " << deaths);
        } else {
            debug_screen('t', "");
            debug_screen('d', "");
        }

        level_times[current_index] = timer;

        if (entities.main_character == EntityList::null_index || entities[entities.main_character].dead) {
            just_died = !is_dead;
            is_dead = true;
            increment_timer = false;
            debug_screen("respawn", "Press R to respawn");
        } else {
            is_dead = false;
            increment_timer = true;
            debug_screen("respawn", "");
        }
        if (just_died) deaths++;

        for (auto& baseclass : classes) baseclass->loop();

        EntityList& entities = get<EntityList>();

        const auto& level_data = level_data_of(world_handler.placed_levels[0]);

        if (entities.exists(entities.camera_following_entity)) {
            const auto& e = entities[entities.camera_following_entity];

            renderer->camera.follow_point(e.data.hitbox_center());

            bool should_replace = !renderer->camera_bound;
            if (renderer->camera_bound) {
                const auto& bound = *renderer->camera_bound;
                const bool overlap_x = e.data.pos.x >= bound.x && e.data.pos.x <= bound.x + bound.w;
                const bool overlap_y = e.data.pos.y <= bound.y && e.data.pos.y >= bound.y - bound.h;
                if (!(overlap_x && overlap_y)) should_replace = true;
            }

            for (const auto& bound : level_data.camera_bounds) {
                const bool overlap_x = e.data.pos.x >= bound.x && e.data.pos.x <= bound.x + bound.w;
                const bool overlap_y = e.data.pos.y <= bound.y && e.data.pos.y >= bound.y - bound.h;
                if (!(overlap_x && overlap_y)) continue;

                if (should_replace || !renderer->camera_bound) {
                    renderer->camera_bound = &bound;
                    continue;
                }
                if (renderer->camera_bound->priority > bound.priority) continue;
                if (renderer->camera_bound->priority < bound.priority) {
                    renderer->camera_bound = &bound;
                    continue;
                }

                const size_t size = bound.w + bound.h;

                if (size > renderer->camera_bound->w + renderer->camera_bound->h) continue;

                renderer->camera_bound = &bound;
            }
        }

        renderer->camera.screenshake -= renderer->camera.screenshake * dt;
        if (renderer->camera.screenshake < 0) renderer->camera.screenshake = 0;

        light_shafts.update(*renderer, -48.0f /* sun angle, wire up however you like */);

        renderer->loop();

        SDL_Event event;
        auto& level0 = world_handler.placed_levels[0].level;
        auto& tm = world_handler.all_level_tilemaps[&level0];

        slow_motion = false;
        bool should_reset = false;

        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    this->running = false;
                    break;

                case SDL_EVENT_KEY_DOWN:
                    switch (event.key.key) {
                        case SDLK_3: {
                            auto loc = std::find(flags.begin(), flags.end(), "speedrun");
                            if (loc != flags.end())
                                flags.erase(loc);
                            else
                                flags.push_back("speedrun");
                        } break;
                        case SDLK_R:
                            // first level no teleporting allowed
                            if (has_reached_checkpoint) should_reset = true;
                            break;
                            // case SDLK_ESCAPE:
                            //     running = false;
                            //     break;
                            // case SDLK_X:
                            //    entities[entities.main_character].data.vel *= 10;
                            //    break;
                            // case SDLK_F:
                            //    slow_motion = true;
                            //    break;
                            // case SDLK_V:
                            //     world_handler.setTile(level0, {(uint)ix, (uint)iy}, 1);
                            //     world_handler.renderDirtyLevels(*leveltris);
                            //     break;
                        case SDLK_M: {
                            auto worldmousepos = renderer->get_world_mouse_pos();
                            entities[entities.main_character].data.pos = worldmousepos;
                        } break;
                            // case SDLK_N: {
                            //    auto worldmousepos = renderer->get_world_mouse_pos();
                            //    for (auto& [i, e] : entities) {
                            //        if (i == entities.main_character) continue;
                            //        if (e->name() != "player") continue;
                            //        e->data.pos = worldmousepos;
                            //    }
                            //} break;
                            // case SDLK_B: {
                            //    auto worldmousepos = renderer->get_world_mouse_pos();
                            //    for (auto& [i, e] : entities) {
                            //        if (i == entities.main_character) continue;
                            //        if (e->name() != "Essence") continue;
                            //        if (e->parent) e->parent->disown(e.get());
                            //        e->data.pos = worldmousepos;
                            //        e->data.vel = {Random::real(-100, 100, 3), Random::real(-100, 100, 3)};
                            //    }
                            //} break;
                            // case SDLK_J: {
                            //    auto worldmousepos = renderer->get_world_mouse_pos();
                            //    for (auto& [i, e] : entities) {
                            //        if (e->name() != "Booster") continue;
                            //        e->data.pos = worldmousepos;
                            //    }
                            //}
                    }
                    break;

                case SDL_EVENT_KEY_UP:
                    break;
            }
            for (auto& [i, entity] : entities) {
                entity->controller->digest_event(event);
            }
        }

        world_handler.renderDirtyLevels(*leveltris);

        float best_cam = 0.f;
        bool should_clean_entities = false;
        auto& placed_level = world_handler.placed_levels[0];

        for (auto& [entity_id, entity] : entities) {
            // Update keyboard controls
            if (auto* controller = dynamic_cast<KeyboardEntityController*>(entity->controller.get())) {
                int size;
                const bool* data = SDL_GetKeyboardState(&size);
                controller->keyboard = std::span<const bool>(data, (size_t)size);
            }
            entity->controller->update_controls(*entity, *this);

            {  // Handle camera
                float cur_cam = entity->camera_need();
                if (cur_cam > 0 && cur_cam > best_cam) {
                    entities.camera_following_entity = entity_id;
                    best_cam = cur_cam;
                }
            }

            // Tick entity
            if (entity->controller->should_slow_motion()) slow_motion = true;
            entity->tick_all(*this);

            // Render
            if (!entitytrisindex.contains(entity_id)) entitytrisindex.add_entity(entity_id);
            entity->render(*this, entitytris->at(entitytrisindex[entity_id]));

            // Handle despawning
            if (!entity || entity->wants_to_despawn) {
                should_clean_entities = true;
                entitytrisindex.remove_entity(entity_id);
                continue;
            }

            if (entity_triggers_finish(*entity, level_data)) do_level_switch(placed_level.level, level_data, false);
        }
        if (should_reset) do_level_switch(placed_level.level, level_data, true);
        if (should_clean_entities) entities.clean_entities();

        check_running(renderer.get());
        check_running(fps_counter.get());
        if (!running) return;
    }

    void quit() override {
        for (auto& baseclass : classes) baseclass->quit();
        fps_counter->quit();
        renderer->quit();
    }
};

GETTER_IMPL(Base::BaseClass, GetApplication, GameClass);
