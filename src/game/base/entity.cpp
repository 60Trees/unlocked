#include "entity.hpp"
#include <cmath>
#include <game/base/world_handler.hpp>

using namespace std;
using namespace Base;

Game::Entity::vec2_t Game::Entity::get_gravity() { return {0.0, -700}; };

void Game::Entity::tick_all(double deltaTime) {
    tick_position(deltaTime);
    tick(deltaTime);
}

constexpr inline void cap(double& x, double max) {
    if (x > max) x = max;
    if (x < -max) x = -max;
}

void Game::Entity::tick_position(double deltaTime) {
    constexpr bool X_AXIS = 0, Y_AXIS = 1;
    constexpr bool LEFT = 0, RIGHT = 1;

    cap(data.vel.x, 1e6);
    cap(data.vel.y, 1e6);

    constexpr double tick_speed_multiplier = 1.0;
    deltaTime *= tick_speed_multiplier;

    WorldHandler& handler = *GetWorldHandler(false);

    {
        vector<EntityAbility*> triggered{};
        for (auto& ability : current_abilities) {
            if (ability->can_trigger(this, deltaTime)) triggered.push_back(ability.get());
        }

        // <AI>
        std::vector<EntityAbility*> final{};

        for (auto* ability : triggered) {
            bool overridden = false;

            for (auto* other : triggered) {
                if (ability != other && other->does_override(ability)) {
                    overridden = true;
                    break;
                }
            }

            if (!overridden) final.push_back(ability);
        }

        for (auto* ability : final) {
            ability->trigger(this, deltaTime);
        }
        // </AI>
    }

    if (current_movement) current_movement->tick(this, deltaTime);

    const auto sweep_axis = [&](bool axis, double edge_before, double edge_after, double range_lo, double range_hi,
                                double& hit_boundary) -> bool {
        const bool is_y = axis == Y_AXIS;
        const bool is_x = axis == X_AXIS;

        for (const auto& level : handler.placed_levels) {
            const auto& cm = handler.collisions[&level.level];

            const auto to_local = [&](double point, bool target_axis) {
                if (target_axis == X_AXIS)
                    return (point - cm.offset.x) / cm.scale;
                else
                    return -(point + cm.offset.y) / cm.scale;
            };
            const auto tile_solid = [&](int ix, int iy) -> bool {
                if (ix < 0 || iy < 0 || ix >= cm.size.x || iy >= cm.size.y) return false;
                return cm.map[ix][iy] == CollisionMap::CollisionType::SOLID;
            };

            const double local_before = to_local(edge_before, axis);
            const double local_after = to_local(edge_after, axis);

            const bool moving_positive_local = local_after > local_before;
            const int i_before = moving_positive_local ? mth::ceil(local_before) - 1 : mth::floor(local_before);
            const int i_after = static_cast<int>(mth::floor(local_after));
            if (i_before == i_after) continue;

            const double local_lo = to_local(range_lo, !axis);
            const double local_hi = to_local(range_hi, !axis);

            const double local_min = mth::min(local_lo, local_hi);
            const double local_max = mth::max(local_lo, local_hi);

            const int perp_lo = mth::floor(local_min);
            const int perp_hi = mth::ceil(local_max) - 1;

            const int step = (i_after > i_before) ? 1 : -1;

            for (int i = i_before + step; i != i_after + step; i += step)
                for (int p = perp_lo; p <= perp_hi; p++) {
                    const bool solid = (axis == 0) ? tile_solid(i, p) : tile_solid(p, i);
                    if (!solid) continue;

                    hit_boundary = cm.offset[axis] + i * cm.scale;
                    if (step < 0) hit_boundary += cm.scale;
                    if (is_y) hit_boundary *= -1;
                    return true;
                }
        }
        return false;
    };

    constexpr double vel_snap_distance = 0.1;

    bool colliding_left = false, colliding_right = false;
    if (abs(data.vel.x) > vel_snap_distance) {
        const double edge_before = (data.vel.x > 0.0) ? data.right_edge() : data.left_edge();
        const double edge_offset = edge_before - data.pos.x;
        const double new_pos_x = data.pos.x + data.vel.x * deltaTime;
        const double edge_after = edge_before + (new_pos_x - data.pos.x);

        double hit_boundary;
        if (sweep_axis(0, edge_before, edge_after, data.top_edge(), data.bottom_edge(), hit_boundary)) {
            bool log = data.pos.x != hit_boundary - edge_offset;
            data.pos.x = hit_boundary - edge_offset;
            if (data.vel.x > 0)
                colliding_right = true;
            else
                colliding_left = true;
            data.vel.x = 0;
        } else {
            data.pos.x = new_pos_x;
        }
    } else if (data.vel.x != 0)
        data.vel.x = 0;

    data.colliding_with.left.update(deltaTime, colliding_left);
    data.colliding_with.right.update(deltaTime, colliding_right);

    bool colliding_up = false, colliding_down = false;
    if (abs(data.vel.y) > vel_snap_distance) {
        const double edge_before = (data.vel.y > 0.0) ? data.top_edge() : data.bottom_edge();
        const double edge_offset = edge_before - data.pos.y;
        const double new_pos_y = data.pos.y + data.vel.y * deltaTime;
        const double edge_after = edge_before + (new_pos_y - data.pos.y);

        double hit_boundary;
        if (sweep_axis(1, edge_before, edge_after, data.left_edge(), data.right_edge(), hit_boundary)) {
            data.pos.y = hit_boundary - edge_offset;
            if (data.vel.y > 0)
                colliding_up = true;
            else
                colliding_down = true;
            data.vel.y = 0.0;
        } else {
            data.pos.y = new_pos_y;
        }
    } else if (data.vel.y != 0)
        data.vel.y = 0;

    // data.vel *= 1.0 - mat.drag * deltaTime;

    data.colliding_with.up.update(deltaTime, colliding_up);
    data.colliding_with.down.update(deltaTime, colliding_down);
}

void Game::ControlData::update(double deltaTime, bool new_pressed) {
    if (new_pressed != pressed)
        time = 0;
    else {
        if (new_pressed)
            time += deltaTime;
        else
            time -= deltaTime;
    }

    pressed = new_pressed;

    charge += (pressed * 2 - 1) * deltaTime;

    if (charge >= upper_charge_limit) charge = upper_charge_limit;
    if (charge <= lower_charge_limit) charge = lower_charge_limit;
}

