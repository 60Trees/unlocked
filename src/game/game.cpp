/*
 * @file src/game/game.cpp
 * @author 60Trees_ (github.com/60Trees)
 */

// #define what_is_going_on 1000

#include <base/app.hpp>
#include <base/renderer.hpp>
#include <cmath>
#include <random>
#include <game/base/entity.hpp>
#include <game/base/entity_list.hpp>

#include <limits>
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
#include "SDL3/SDL_mouse.h"
#include "SDL3/SDL_stdinc.h"
#include "game/base/world_handler.hpp"
#include "game/systems/light_shafts.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/trigonometric.hpp"
#include "utils.hpp"

#ifdef DEBUG_SCREEN
#    include <imgui_impl_sdl3.h>
#endif

using namespace std;
using namespace Game;
using namespace Base;

struct Point {
    int x, y;
};

extern "C" double dt_multiplier();

using VertexArray = Renderer::VertexArray;
// <AI>
// Same test as Entity::colliding_with, but for two raw Hitboxes -- the Finish/NoFinish/
// ShareFinish bounds are never spawned as real Entities, so there's no Entity to compare.
static bool hitboxes_overlap(const Hitbox& a, const Hitbox& b) {
    return a.left() < b.right() && a.right() > b.left() && a.bottom() < b.top() && a.top() > b.bottom();
}

static Hitbox::vec2_t bottom_mid(const Hitbox& h) { return {(h.left() + h.right()) / 2.0, h.bottom()}; }

// Translates `h` (preserving size/anchor) so its bottom-mid point lands on `target`.
static void snap_bottom_mid_to(Hitbox& h, Hitbox::vec2_t target) { h.pos += target - bottom_mid(h); }
// </AI>

struct KeyboardControls {
    SDL_Scancode up = SDL_SCANCODE_W, down = SDL_SCANCODE_S, left = SDL_SCANCODE_A, right = SDL_SCANCODE_D, jump = SDL_SCANCODE_SPACE;
};

#ifdef what_is_going_on
struct RandomController : EntityController {
    uint seed = 0;
    uint tick_count = 0;
    Duration time_left = 0.0;
    enum { WAIT, MOVE_LEFT, MOVE_RIGHT, JUMP, SHOOT } Action;
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
            systems / light_shafts(forced_dir) {
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

        own.controls.up.update(app, false);
        own.controls.down.update(app, false);
        own.controls.jump.update(app, Action == JUMP);

        tick_count++;
    }
};
#endif

struct KeyboardEntityController : EntityController {
    KeyboardControls controls;
    span<const bool> keyboard;
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

        own.controls.boost.update(app, SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_MASK(SDL_BUTTON_LEFT));

        {
            auto worldmousepos = r.get_world_mouse_pos();
            auto diff = glm::vec2{own.data.pos} - worldmousepos;

            own.controls.focusDegrees = mth::fmod(-glm::degrees(std::atan2(diff.x, -diff.y)) + 360, 360);
            debug_screen("a", own.controls.focusDegrees);
        }

        own.controls.up.update(app, keyboard[controls.up]);
        own.controls.down.update(app, keyboard[controls.down]);
        own.controls.jump.update(app, keyboard[controls.jump]);
    }
};

struct GameClass : Application {
    LightShaftSystem light_shafts;

    Game::EntityList& entities = [&] -> Game::EntityList& {
        this->ensure_class_added<Game::EntityList>([] { return new Game::EntityList(); });
        return this->get<Game::EntityList>();
    }();

    EntityList::index_t camera_following_entity = EntityList::null_index;

    vector<EntityList::index_t> players{};

    using TexturedRectDescriptor = Renderer::TexturedRectDescriptor;
    using ColouredRectDescriptor = Renderer::ColouredRectDescriptor;

    // New pixel shader for the saturation post effect.
    // prevTex, prevSamp, params, and VSOut are already declared by buildPostPipeline() --
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
    DitherBgParams ditherBgParams{};  // member of GameClass -- must outlive the frame it's uploaded on

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

    void set_dithered_gradient_background(uint32_t top, uint32_t bottom, float pixelSize = 4.0f) {
        {
            PrevBG thisbg;
            thisbg.type = thisbg.Dithered;
            thisbg.data.dithered = {top, bottom, pixelSize};
            if (prev_bg == thisbg) return;
            prev_bg = thisbg;
        }
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
        {
            PrevBG thisbg;
            thisbg.type = thisbg.Gradient;
            thisbg.data.gradient = {top, bottom};
            if (prev_bg == thisbg) return;
            prev_bg = thisbg;
        }
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

        // <AI>
        // Finish/NoFinish/ShareFinish bounds, stored the same way camera bounds are --
        // baked from the LDtk entity at load time, never spawned as real Entities.
        // Hitbox is reused purely for its anchor-aware left()/right()/top()/bottom().
        std::optional<Hitbox> finish;      // BOTTOM_MID anchored, at most one
        std::vector<Hitbox> no_finish;     // TOP_LEFT anchored
        std::vector<Hitbox> share_finish;  // TOP_LEFT anchored
        // </AI>
    };

    // <AI>
    // A "game entity" (Entity*, owned by EntityList) is the live, ticking thing that moves
    // and renders every frame. An "ldtk entity" (ldtk::Entity) is just a static declaration
    // inside the level file -- populate_level() reads each one once, at level-load time, and
    // either spawns a game entity for it (entities.spawn_entity) or -- for PlayerStart /
    // Finish / NoFinish / ShareFinish / Camera* -- stores it as a plain Hitbox/bound inside
    // LevelData instead, with no game entity created at all. Everything below that deals
    // with "the next level" only ever creates or destroys game entities; ldtk entities are
    // read-only data that populate_level() alone is responsible for parsing.

    static LevelData& level_data_of(PlacedLevel& placed) { return *dynamic_cast<LevelData*>(placed.usrdata.get()); }

    // Finds `current_ldtk_level`'s index within world 0's level list (world 0 matches what
    // init() uses: world_handler.getlevel(0, 0)), then loads and fully populates the level
    // right after it. Returns nullptr if `current_ldtk_level` isn't found, or is already the
    // last level in the world.
    //
    // This is a single-active-level game (do_level_switch despawns everything that doesn't
    // carry over), so a switch fully replaces world_handler.placed_levels rather than
    // accumulating levels. spawn_level() below calls populate_level() exactly once for the
    // new level -- that's the only place its game entities get spawned.
    // NOTE: clearing placed_levels destroys the *current* level's LevelData (and PlacedLevel)
    // -- callers must copy out anything they still need from it before calling this.
    PlacedLevel* load_next_level(const ldtk::Level& current_ldtk_level) {
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

        const size_t next_index = current_index + 1;
        if (next_index >= world.allLevels().size()) return nullptr;  // current level is the last one in the world

        const ldtk::Level& next_ldtk_level = world_handler.getlevel(next_index, world);

        world_handler.placed_levels.clear();
        leveltris->clear();

        return &spawn_level(next_ldtk_level);
    }
    // </AI>

    // True if game entity `e` has "canfinish", overlaps the level's Finish bound, and isn't
    // blocked by any NoFinish bound. Finish/NoFinish are plain Hitboxes baked from ldtk
    // entities at load time -- there's no game entity behind them to tick or despawn.
    bool entity_triggers_finish(const Entity& e, const LevelData& level_data) {
        if (!level_data.finish || !e.has_attribute("canfinish")) return false;
        if (!hitboxes_overlap(e.data, *level_data.finish)) return false;

        for (const auto& no_finish_bound : level_data.no_finish)
            if (hitboxes_overlap(e.data, no_finish_bound)) return false;

        return true;
    }

    // <AI>
    // Switches from `current_ldtk_level` to the next level in the world, carrying over only
    // the game entities that overlap a ShareFinish bound (and opt in via
    // transfers_to_new_level()) -- everything else despawns.
    //
    // Order matters here:
    //   1. Every *existing* (old-level) game entity's fate is decided and applied FIRST,
    //      while `entities` still only contains the old level's entities. Only after that do
    //      we load the next level, which spawns ITS OWN game entities via populate_level
    //      (called exactly once, inside load_next_level -> spawn_level). That way the new
    //      level's freshly-spawned game entities are never visible to the "does this overlap
    //      the old ShareFinish bound" check, so they can't get wrongly marked to despawn.
    //   2. populate_level() only ever runs once per level load -- there's no second call
    //      here re-parsing the same ldtk entities into an already-populated LevelData (that
    //      second call was what threw "Only one PlayerStart entity is allowed per level!").
    // Despawning follows the same two-step pattern the main loop() uses: drop the entity's
    // triangles from entitytris via entitytrisindex.remove_entity() *before* the entity
    // itself is destroyed by clean_entities(), so entitytris never ends up holding a "ghost"
    // triangle for an entity that no longer exists.
    void do_level_switch(const ldtk::Level& current_ldtk_level, const LevelData& level_data) {
        // Copy out what we still need -- load_next_level() below clears placed_levels, which
        // owns (and destroys) level_data.
        const std::vector<Hitbox> share_finish_bounds = level_data.share_finish;
        EntityList& entities = get<EntityList>();

        // Step 1: decide every existing (old-level) game entity's fate and despawn the ones
        // that don't carry over. Nothing from the next level exists yet at this point.
        std::vector<EntityList::index_t> surviving_entity_ids;
        for (auto& [entity_id, entity_ptr] : entities) {
            if (!entity_ptr) continue;
            Entity& e = *entity_ptr;

            const bool overlaps_share_finish = std::any_of(share_finish_bounds.begin(), share_finish_bounds.end(),
                [&](const Hitbox& bound) { return hitboxes_overlap(e.data, bound); });

            if (overlaps_share_finish && e.transfers_to_new_level()) {
                surviving_entity_ids.push_back(entity_id);
            } else {
                e.wants_to_despawn = true;
                entitytrisindex.remove_entity(entity_id);  // drop its triangles before it's destroyed below
            }
        }
        entities.clean_entities();  // actually erases everything just marked wants_to_despawn

        // Step 2: load the next level. This is the ONLY populate_level() call it gets -- it
        // spawns all of the new level's game entities (and bakes its own PlayerStart/Finish/
        // NoFinish/ShareFinish/Camera bounds) exactly once.
        PlacedLevel* next_level = load_next_level(current_ldtk_level);
        if (!next_level) return;
        LevelData& next_data = level_data_of(*next_level);
        if (next_data.player_starts.empty()) return;

        const auto player_start = next_data.player_starts[0];

        // Step 3: move the surviving old-level game entities onto the new PlayerStart. The
        // new level's own game entities are never touched -- they're already positioned by
        // whatever populate_level/spawn_entity did for them.
        for (auto id : surviving_entity_ids)
            if (entities.exists(id)) entities[id].data.pos = player_start;

        // Step 4: rebuild entitytris for everything currently in `entities` -- the surviving
        // old entities (just moved) plus the new level's freshly-spawned game entities (which
        // have no entitytrisindex entry yet). add_entity() no-ops for ids already present, so
        // this is safe to run unconditionally over the whole list.
        for (auto& [entity_id, entity_ptr] : entities) {
            entitytrisindex.add_entity(entity_id);
            entity_ptr->render(*this, entitytris->at(entitytrisindex[entity_id]));
        }

        renderer->camera.x = renderer->camera.target_x = player_start.x;
        renderer->camera.y = renderer->camera.target_y = player_start.y;
    }
    // </AI>

    void check_finish() {
        auto& placed_level = world_handler.placed_levels[0];
        auto& level_data = level_data_of(placed_level);

        EntityList& entities = get<EntityList>();
        for (auto& [entity_id, entity_ptr] : entities) {
            if (!entity_ptr) continue;
            if (!entity_triggers_finish(*entity_ptr, level_data)) continue;

            debug_screen("aaa", "Level is finishing!!!");
            do_level_switch(placed_level.level, level_data);
            break;  // placed_level / level_data are potentially dangling after this -- don't touch them again
        }
    }
    const map<uint, string> kDefaultTileGroups = {{0, "Air"}, {1, "Solid"}, {2, "Solid"}, {3, "Solid"}, {5, "Solid"}, {4, "Death"}};

    // Parses PlayerStart/Finish/NoFinish/ShareFinish/Camera-bound markers and spawns every
    // other named entity for `placed_level`, populating `level_data` and the entity list in
    // place. Shared between init() (first level) and spawn_level() (levels loaded on a switch).
    void populate_level(const Game::PlacedLevel& placed_level, LevelData& level_data) {
        EntityList& entities = get<EntityList>();

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

            entities.spawn_entity(name, &entity);
        };

        for (const auto& layer : placed_level.level.allLayers())
            for (const auto& entity : layer.allEntities()) handle_entity(entity, entity.getName());
    }

    // Creates and renders a PlacedLevel for `level` at `offset`, parses its entities into a
    // fresh LevelData, and appends it to world_handler.placed_levels.
    PlacedLevel& spawn_level(const ldtk::Level& level, glm::vec<2, int> offset = {0, 0}) {
        auto level_data_ptr = make_shared<LevelData>();
        world_handler.placed_levels.push_back(PlacedLevel{level, offset, level_data_ptr, kDefaultTileGroups});
        PlacedLevel& placed = world_handler.placed_levels.back();

        world_handler.render(level, *leveltris, *renderer, offset);
        light_shafts.bake_level(*renderer, world_handler.all_level_tilemaps[&level]);

        populate_level(placed, *level_data_ptr);

        return placed;
    }
    // </AI>
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

        {
            const auto c = world_handler.main_world.allWorlds()[0].getBgColor();
            debug_screen("bgcolour", "Background colour: " << +c.r << "," << +c.g << "," << +c.b);
            // set_solid_background(c.r, c.g, c.b);
            set_solid_background(0, 0, 0);
        }

        world_handler.uploadAllTilesets(*renderer);

        world_handler.placed_levels.push_back(PlacedLevel{world_handler.getlevel(0, 0), {0, 0}, make_shared<LevelData>(),
            {{0, "Air"}, {1, "Solid"}, {2, "Solid"}, {3, "Solid"}, {5, "Solid"}, {4, "Death"}}});
        world_handler.renderDirtyLevels(*leveltris);
        light_shafts.bake_level(*renderer, world_handler.all_level_tilemaps[&world_handler.placed_levels[0].level]);
        light_shafts.register_post_effect(*renderer);

        auto& level_data = level_data_of(world_handler.placed_levels[0]);

        update_renderer_layers();

        renderer->compile_default_shaders();

        renderer->addTextureFromBytes("assets/player.png", fs_helper::get_bytes_from_file<char>("assets/player.png"));

        // renderer->compile_used_shaders();

        players.push_back(entities.spawn_entity("player"));
        populate_level(world_handler.placed_levels[0], level_data);
        if (level_data.player_starts.empty()) {
            level_data.player_starts.push_back({0, 100});
        }
        entities[players[0]].data.pos = level_data.player_starts[0];
        entities[players[0]].controller = make_unique<KeyboardEntityController>();
        camera_following_entity = players[0];

        size_t tris_i = 0;
        for (const auto& [entity_id, entity] : entities) {
            entitytrisindex.add_entity(entity_id);
            entity->render(*this, entitytris->at(entitytrisindex[entity_id]));
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

    bool slow_motion = false;

    void loop() override {
        fps_counter->loop();
        for (auto& baseclass : classes) baseclass->loop();

        EntityList& entities = get<EntityList>();

        fps_counter->deltaTime *= (slow_motion ? dt_multiplier() : 1);
        const double dt = fps_counter->deltaTime;
        ASSUME(dt != NAN);
        ASSUME(dt > 0);

        // for (const auto [colour, active] : puzzle_state->active_colours) {
        // }

        // debug_screen("colours", "Game colours: " << puzzle_state->active_colours);

        const auto& level_data = level_data_of(world_handler.placed_levels[0]);

        if (entities.exists(camera_following_entity)) {
            const auto& e = entities[camera_following_entity];

            renderer->camera.follow_point(e.data.hitbox_center());

            // debug_screen("xb", "Player pos: " << e.data.pos.x << ',' << e.data.pos.y);

            bool should_replace = !renderer->camera_bound;
            if (renderer->camera_bound) {
                const auto& bound = *renderer->camera_bound;
                const bool overlap_x = e.data.pos.x >= bound.x && e.data.pos.x <= bound.x + bound.w;
                const bool overlap_y = e.data.pos.y <= bound.y && e.data.pos.y >= bound.y - bound.h;
                if (!(overlap_x && overlap_y)) should_replace = true;
            }

            for (const auto& bound : level_data.camera_bounds) {
                // debug_screen("xa", "Bound pos: " << bound.x << "," << bound.y << "\n      size: " << bound.w << ',' << bound.h);
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

        const auto playerpos = entities[players[0]].data.hitbox_center();
        const int ix = (int)std::floor((playerpos.x - tm.offset.x) / tm.scale);
        const int iy = (int)std::floor(-(playerpos.y + tm.offset.y - 1) / tm.scale);

        debug_screen(this << "x", "Player solid tile pos: " << ix << ',' << iy);

        while (SDL_PollEvent(&event)) {
#ifdef DEBUG_SCREEN
            ImGui_ImplSDL3_ProcessEvent(&event);
#endif
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    this->running = false;
                    break;

                case SDL_EVENT_KEY_DOWN:
                    switch (event.key.key) {
                        case SDLK_ESCAPE:
                            running = false;
                            break;
                        case SDLK_R:
                            entities[players[0]].data.pos = level_data.player_starts[0];
                            break;
                        case SDLK_X:
                            entities[players[0]].data.vel *= 10;
                            break;
                        case SDLK_F:
                            slow_motion = !slow_motion;
                            break;
                        case SDLK_V:
                            debug_screen("test1", "Yes Working level: " << &level0 << "\nYes working handler: " << &world_handler);
                            world_handler.setTile(level0, {(uint)ix, (uint)iy}, 1);
                            world_handler.renderDirtyLevels(*leveltris);
                            break;
                        case SDLK_M: {
                            auto worldmousepos = renderer->get_world_mouse_pos();
                            for (auto i : players) entities[i].data.pos = worldmousepos;
                        } break;
                        case SDLK_N: {
                            auto worldmousepos = renderer->get_world_mouse_pos();
                            for (auto& [i, e] : entities) {
                                if (std::find(players.begin(), players.end(), i) != players.end()) continue;
                                if (e->name() != "player") continue;
                                e->data.pos = worldmousepos;
                            }
                        } break;
                        case SDLK_B: {
                            auto worldmousepos = renderer->get_world_mouse_pos();
                            for (auto& [i, e] : entities) {
                                if (std::find(players.begin(), players.end(), i) != players.end()) continue;
                                if (e->name() != "Essence") continue;
                                if (e->parent) e->parent->disown(e.get());
                                e->data.pos = worldmousepos;
                                e->data.vel = {Random::real(-100, 100, 3), Random::real(-100, 100, 3)};
                            }
                        } break;
                    }
                    inputs.do_inputs(event.key.key, 16.0f);
                    break;

                case SDL_EVENT_KEY_UP:
                    inputs.do_inputs(event.key.key, 0.0f);
                    break;
            }
            for (auto& [i, entity] : entities) {
                entity->controller->digest_event(event);
            }
        }

        {
            auto& level0 = world_handler.placed_levels[0].level;
            auto& tm = world_handler.all_level_tilemaps[&level0];

            const auto playerpos = entities[players[0]].data.hitbox_center();
            const int ix = (int)std::floor((playerpos.x - tm.offset.x) / tm.scale);
            const int iy = (int)std::floor(-(playerpos.y + tm.offset.y - 1) / tm.scale);
            debug_screen("tile_under_player",
                "Tile under player: " << ix << ", " << iy << "\ncurrent value: "
                                      << (ix >= 0 && iy >= 0 && (uint)ix < tm.size.x && (uint)iy < tm.size.y ? tm.tilemap[ix][iy] : -123));
        }

        world_handler.renderDirtyLevels(*leveltris);

        for (auto& [i, e] : entities) {
            if (auto* controller = dynamic_cast<KeyboardEntityController*>(e->controller.get())) {
                int size;
                const bool* data = SDL_GetKeyboardState(&size);
                controller->keyboard = std::span<const bool>(data, (size_t)size);
            }
        }

        bool should_clean_entities = false;
        for (auto& [entity_id, entity] : entities) {
            if (!entity || entity->wants_to_despawn) {
                should_clean_entities = true;
                entitytrisindex.remove_entity(entity_id);
                continue;
            }
            entity->controller->update_controls(*entity, *this);
            entity->tick_all(*this);
            if (entity->wants_to_despawn) should_clean_entities = true;

            if (!entitytrisindex.contains(entity_id)) entitytrisindex.add_entity(entity_id);
            entity->render(*this, entitytris->at(entitytrisindex[entity_id]));
        }
        if (should_clean_entities) entities.clean_entities();

        check_finish();

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
