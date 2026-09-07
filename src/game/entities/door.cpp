/**
 * @date 03/09/2026
 * @author 60Trees_ (github.com/60Trees)
 * @file src/game/entities/door.cpp
 */

#include "LDtkLoader/DataTypes.hpp"
#include "base/app.hpp"
#include "base/fps_counter.hpp"
#include "game/base/entity.hpp"
#include "puzzle_aspects.hpp"
#include "game/base/world_handler.hpp"
#include "utils.hpp"

using namespace Game;
using namespace Base;
using namespace std;

struct Door : PuzzleObject {
    optional<bool> get_state() const override { return nullopt; }
    std::string name() const override { return "Door"; }

    Hitbox get_defaults() const override { return {}; };

    bool inverted = false;

    glm::vec<2, uint> grid_position;

    void spawn(Base::Application& app, const ldtk::Entity* e) override {
        own_layer = e->layer;
        grid_position = {e->getGridPosition().x, e->getGridPosition().y};

        PuzzleObject::spawn(app, e);
        if (!e) return;

        inverted = e->getField<ldtk::FieldType::Bool>("Negated").value_or(false);

        data.size.x = e->getSize().x;
        data.size.y = e->getSize().y;
    }

    const ldtk::Layer* own_layer = nullptr;

    ControlData is_open{};

    inline bool is_currently_open(const Base::Application& app) const {
        const auto x = app.get<Game::PuzzleState>().active_colours;
        if (!x.contains(colour)) return false;
        return x.at(colour) ? !inverted : inverted;
    }

    inline uint getscale() const { return own_layer ? own_layer->getCellSize() : 8; }

    AnimationFrame get_anim_frame(Base::Application& app) const override {
        const int width = get_tile_size().x, height = get_tile_size().y;
        const double dt = app.get<FpsCounter>().deltaTime;

        const float anim_speed = 1;

        // primitive door = outline when open, solid when closed
        const bool primitive_door = width > 1;
        // TODO: Not primitive door = vertical chain (for when its 1 tile wide)

        /*if (primitive_door)*/ {
            const glm::vec<2, uint> solid_door_open = {48, 0};
            const glm::vec<2, uint> solid_door_closed = {40, 0};

            return {.top_left = is_open ? solid_door_open : solid_door_closed,
                .size = {getscale(), getscale()},
                .tileset = "assets/buttons_n_shi.png",
                .direction = RIGHT};
        }
    }

    void render(Base::Application& app, Base::Renderer::VertexLayer& layer) const override {
        auto& r = app.get<Base::Renderer>();
        const double deltaTime = app.get<Base::FpsCounter>().deltaTime;

        if (!does_render()) {
            layer.vertices.clear();
            return;
        }

        const auto worldspace = r.builtin_worldspace_vshader();
        const auto uispace = r.builtin_uispace_vshader();
        const auto textured = r.builtin_textured_pshader();
        const auto coloured = r.builtin_coloured_pshader();
        layer.material.pixel_shader = textured;
        layer.material.vertex_shader = worldspace;
        layer.material.blend_mode = Renderer::Alpha;

        layer.vertices.clear();

        for (double left = data.pos.x; left < data.pos.x + data.size.x; left += getscale()) {
            for (double top = data.pos.y - data.size.y; top < data.pos.y; top += getscale()) {
                app.set_user_data<Door, glm::vec<2, double>>(std::make_shared<glm::vec<2, double>>(left, top));

                const AnimationFrame anim_frame = get_anim_frame(app);
                ASSUME(anim_frame.size.x > 0);
                ASSUME(anim_frame.size.y > 0);

                const glm::vec<2, uint> bottom_middle = anim_frame.bottom_middle.value_or(glm::vec<2, uint>{anim_frame.size.x / 2, 0});

                Renderer::TexturedRectDescriptor rect{};

                // world position: anchor `bottom_middle` (local to the frame) onto data.pos,
                // matching how Hitbox::pos already means "bottom center" for collision purposes
                const auto dopos = [&](const double num) -> float {
                    if (anim_frame.snap_to_pixel_grid) return mth::round(num);
                    return num;
                };

                rect.pos.l = dopos(left);
                rect.pos.r = dopos(left + anim_frame.size.x);
                rect.pos.t = dopos(top + anim_frame.size.y);
                rect.pos.b = dopos(top);

                const uint16_t uv_l = anim_frame.top_left.x;
                const uint16_t uv_t = anim_frame.top_left.y;
                const uint16_t uv_r = anim_frame.top_left.x + anim_frame.size.x;
                const uint16_t uv_b = anim_frame.top_left.y + anim_frame.size.y;

                const bool flip_lr = anim_frame.direction != get_direction();

                rect.uv.l = flip_lr ? uv_r : uv_l;
                rect.uv.r = flip_lr ? uv_l : uv_r;
                rect.uv.t = uv_t;
                rect.uv.b = uv_b;

                rect.atlas_index = r.getTextureID(anim_frame.tileset);
                // std::print("Atlas index={}\n", rect.atlas_index);
                rect.layer = 0;

                Renderer::make_textured_square(rect, layer.vertices);
            }
        }
    }

    inline glm::vec<2, int> get_tile_pos(const Game::TileMap& placedlevel) const {
        const int tilex = (int)std::floor((data.pos.x - placedlevel.offset.x) / placedlevel.scale);
        const int tiley = (int)std::floor(-(data.pos.y + placedlevel.offset.y - 1) / placedlevel.scale);
        return {tilex, tiley};
    }
    inline glm::vec<2, int> get_tile_size() const {
        const int width = data.size.x / getscale(), height = data.size.y / getscale();
        return {width, height};
    }

    void tick(Base::Application& app, double dt) override {
        PuzzleObject::tick(app, dt);

        is_open.update(dt, is_currently_open(app));

        auto& worldhandler = app.get<WorldHandler>();
        auto& placedlevel = worldhandler.all_level_tilemaps[own_layer->level];

        const auto tilepos = get_tile_pos(placedlevel);

        const auto tilesize = get_tile_size();
        const int width = tilesize.x, height = tilesize.y;

        for (int ix = tilepos.x; ix < tilepos.x + width; ix++)
            for (int iy = tilepos.y; iy < tilepos.y + height; iy++)
                app.get<WorldHandler>().setTile(*own_layer->level, {ix, iy}, is_open ? 1 : 0);
    }
};

_REGISTER_FOR(new Door(), "Door", Entity);
