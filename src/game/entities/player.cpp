#include "player.hpp"
#include <base/renderer.hpp>
#include "game/base/world_handler.hpp"

_REGISTER_FOR(new Game::Player(), "player", Game::Entity)

using namespace std;
using namespace Base;
using Vertex = Renderer::Vertex;
using Material = Renderer::Material;

Game::Entity::PosData Game::Player::get_defaults() const {
    return {
        .pos = {},
        .vel = {0, 0},
        .size = {10, 15},
        .speed = 30,
    };
}

void Game::Player::spawn() {
    Game::Entity::spawn();
    struct TestAnim : EntityAnim {};
}

void Game::Player::render(Base::Renderer& r, Base::Renderer::VertexLayer& layer) const {
    const auto worldspace = r.builtin_worldspace_vshader();
    const auto uispace = r.builtin_uispace_vshader();
    const auto textured = r.builtin_textured_pshader();
    const auto coloured = r.builtin_coloured_pshader();

    layer.material.pixel_shader = coloured;
    layer.material.vertex_shader = worldspace;
    layer.material.blend_mode = Renderer::Alpha;
    layer.vertices.clear();
    Renderer::make_coloured_square(
        {
            .pos = {(float)data.left_edge(), (float)data.top_edge(), (float)data.right_edge(), (float)data.bottom_edge()},
            .colours = {0xffff00ff, 0x00ffffff, 0x00ff00ff, 0x000000ff},
        },
        layer.vertices);
}

void Game::Player::tick_position(double deltaTime) {
    WorldHandler& handler = *GetWorldHandler(false);
    const MaterialProps& mat = flight_props;

    if (controls.left) data.vel.x -= data.speed * mat.speed * deltaTime;
    if (controls.right) data.vel.x += data.speed * mat.speed * deltaTime;
    if (controls.up) data.vel.y += data.speed * mat.speed * deltaTime;
    if (controls.down) data.vel.y -= data.speed * mat.speed * deltaTime;

    // TODO: Fix messy code and make it easier to understand
    auto to_local = [](glm::vec<2, double> point, const CollisionMap& cm) -> glm::vec2 {
        return {
            (point.x - cm.offset.x) / cm.scale,
            -(point.y + cm.offset.y) / cm.scale,
        };
    };

    auto tile_solid = [&](const CollisionMap& cm, int ix, int iy) -> bool {
        if (ix < 0 || iy < 0 || ix >= static_cast<int>(cm.size.x) || iy >= static_cast<int>(cm.size.y)) return false;
        return cm.map[ix][iy] == CollisionMap::CollisionType::SOLID;
    };

    auto sweep_axis = [&](int axis, double edge_before, double edge_after, double range_lo, double range_hi, double& hit_boundary) -> bool {
        for (const auto& level : handler.placed_levels) {
            const auto& cm = handler.collisions[&level.level];

            const double local_before = (axis == 0) ? to_local({edge_before, 0}, cm).x : to_local({0, edge_before}, cm).y;
            const double local_after = (axis == 0) ? to_local({edge_after, 0}, cm).x : to_local({0, edge_after}, cm).y;

            const bool moving_positive_local = local_after > local_before;
            const int i_before = moving_positive_local
                                     ? static_cast<int>(mth::ceil(local_before)) - 1  // treat exact boundary as the cell behind you
                                     : static_cast<int>(mth::floor(local_before));
            const int i_after = static_cast<int>(mth::floor(local_after));
            if (i_before == i_after) continue;

            const double local_lo = (axis == 0) ? to_local({0, range_lo}, cm).y : to_local({range_lo, 0}, cm).x;
            const double local_hi = (axis == 0) ? to_local({0, range_hi}, cm).y : to_local({range_hi, 0}, cm).x;

            const double local_min = mth::min(local_lo, local_hi);
            const double local_max = mth::max(local_lo, local_hi);

            const int perp_lo = static_cast<int>(mth::floor(local_min));
            const int perp_hi = static_cast<int>(mth::ceil(local_max)) - 1;

            const int step = (i_after > i_before) ? 1 : -1;

            for (int i = i_before + step;; i += step) {
                for (int p = perp_lo; p <= perp_hi; ++p) {
                    const bool solid = (axis == 0) ? tile_solid(cm, i, p) : tile_solid(cm, p, i);
                    if (solid) {
                        if (axis == 0) {
                            hit_boundary = (step > 0) ? cm.offset.x + i * cm.scale : cm.offset.x + (i + 1) * cm.scale;
                        } else {
                            hit_boundary = (step > 0) ? -cm.offset.y - i * cm.scale : -cm.offset.y - (i + 1) * cm.scale;
                        }
                        return true;
                    }
                }
                if (i == i_after) break;
            }
        }
        return false;
    };

    // ------------------------------------------------------------
    // X movement
    // ------------------------------------------------------------

    if (data.vel.x != 0.0) {
        const double edge_before = (data.vel.x > 0.0) ? data.right_edge() : data.left_edge();
        const double edge_offset = edge_before - data.pos.x;  // works for center- or corner-based pos
        const double new_pos_x = data.pos.x + data.vel.x * deltaTime;
        const double edge_after = edge_before + (new_pos_x - data.pos.x);

        double hit_boundary;
        if (sweep_axis(0, edge_before, edge_after, data.top_edge(), data.bottom_edge(), hit_boundary)) {
            bool log = data.pos.x != hit_boundary - edge_offset;
            if (log) std::print("[SPAM] Collided prevx={} ", data.pos.x);
            data.pos.x = hit_boundary - edge_offset;
            if (log) std::print("x={}\n", data.pos.x);
            data.vel.x = 0;
        } else {
            data.pos.x = new_pos_x;
        }
    }

    // ------------------------------------------------------------
    // Y movement
    // ------------------------------------------------------------

    if (data.vel.y != 0.0) {
        const double edge_before = (data.vel.y > 0.0) ? data.top_edge() : data.bottom_edge();
        const double edge_offset = edge_before - data.pos.y;
        const double new_pos_y = data.pos.y + data.vel.y * deltaTime;
        const double edge_after = edge_before + (new_pos_y - data.pos.y);

        double hit_boundary;
        if (sweep_axis(1, edge_before, edge_after, data.left_edge(), data.right_edge(), hit_boundary)) {
            data.pos.y = hit_boundary - edge_offset;
            data.vel.y = 0.0;
        } else {
            data.pos.y = new_pos_y;
        }
    }

    data.vel *= 1.0 - mat.drag * deltaTime;
}
