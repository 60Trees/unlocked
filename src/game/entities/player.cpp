#include "player.hpp"
#include <base/renderer.hpp>
#include <base/fps_counter.hpp>
#include <span>
#include "game/base/entity_movements.hpp"
#include "game/base/world_handler.hpp"
#include "utils.hpp"

using namespace Game;
using namespace Base;
using namespace std;
constexpr bool X_AXIS = 0, Y_AXIS = 1;
constexpr bool LEFT = 0, RIGHT = 1;

_REGISTER_FOR(new Player(), "player", Entity)

#define _register_movement(class_name) _REGISTER_FOR(new class_name(), #class_name, EntityMovement)
#define _register_ability(class_name) _REGISTER_FOR(new class_name(), #class_name, EntityAbility)

namespace PlayerMovements {
    struct Fall : EntityMovement {
        virtual float speed_multiplier() { return 1.0f; }
        virtual glm::vec<2, double> drag_multiplier() { return {1.0f, 1.0f}; }

        void tick(Entity* _e, double deltaTime) override {
            Entity& e = *_e;

            e.data.vel += e.get_gravity();

            constexpr static Player::MaterialProps floor_props_moving = {
                .drag = {6, 0.1},
                .speed = 20,
            };
            constexpr static Player::MaterialProps floor_props_stationary = {
                .drag = {8, 0.1},
                .speed = 0,
            };

            Player::MaterialProps mat = e.controls.left || e.controls.right ? floor_props_moving : floor_props_stationary;
            e.data.vel *= 1.0 - (mat.drag * drag_multiplier() * deltaTime);

            if (!(e.controls.left || e.controls.right) && !e.data.colliding_with.down) mat.drag.x = 0;

            const auto multiplier = speed_multiplier();

            if (e.controls.left) e.data.vel.x -= e.data.speed * mat.speed * deltaTime * multiplier;
            if (e.controls.right) e.data.vel.x += e.data.speed * mat.speed * deltaTime * multiplier;
        }
    } _register_movement(Fall);

    struct QuickTurn : Fall {
        float speed_multiplier() override { return 2.0f; }

        Duration time_left = 0.3;
        void tick(Entity* _e, double deltaTime) override {
            Fall::tick(_e, deltaTime);
            // if it goes negative, it is handled by the ability
            time_left -= deltaTime;
        }
    } _register_movement(QuickTurn);
}  // namespace PlayerMovements

namespace PlayerAbilities {
    struct Jump : EntityAbility {
        bool can_trigger(const Entity* e, double) override { return e->controls.jump && e->data.colliding_with.down; }
        void trigger(Entity* e, double) override { e->data.vel.y += 200; }
    } _register_ability(Jump);

    struct QuickTurn : ConditionalEntityAbility {
        bool is_active(Entity* e, double deltaTime) override {
            const bool is_walking = dynamic_cast<PlayerMovements::Fall*>(e->current_movement.get());

            if (!is_walking) return false;
            if (!e->data.colliding_with.down) return false;

            {
                auto* movement = dynamic_cast<PlayerMovements::QuickTurn*>(e->current_movement.get());
                if (movement) return movement->time_left > 0.0;
            }

            const auto& c = e->controls;

            if (c.left && !c.right && e->data.colliding_with.left < 0.1 && c.right.charge > 0.4) return true;
            if (c.right && !c.left && e->data.colliding_with.right < 0.1 && c.left.charge > 0.4) return true;

            return false;
        }

        void when_active(Entity* e, double deltaTime) override {
            const bool is_quickturning = dynamic_cast<PlayerMovements::QuickTurn*>(e->current_movement.get());
            if (!is_quickturning) e->set_movement("QuickTurn");
        }
        void when_deactive(Entity* e, double deltaTime) override {
            const bool is_quickturning = dynamic_cast<PlayerMovements::QuickTurn*>(e->current_movement.get());
            if (is_quickturning) e->set_movement("Fall");
        }
    } _register_ability(QuickTurn);
}  // namespace PlayerAbilities

using namespace std;
using namespace Base;
using Vertex = Renderer::Vertex;
using Material = Renderer::Material;

Hitbox Player::get_defaults() const {
    return {
        .size = {10, 15},
        .pos = {},
        .vel = {0, 0},
        .speed = 30,
    };
}

void Player::spawn() {
    Entity::spawn();
    const auto add_ability = [&](const std::string& name) {
        current_abilities.push_back(unique_ptr<EntityAbility>(EntityAbility::make_new(name)));
    };

    set_movement("Fall");
    add_ability("QuickTurn");
    add_ability("Jump");
}

void Player::render(Renderer& r, Renderer::VertexLayer& layer, double deltaTime) const {
    auto new_layer = anim.get_rendered(deltaTime, *this);
    layer = new_layer;
}

void Player::tick_position(double deltaTime) {
    constexpr double tick_speed_multiplier = 1.0;
    deltaTime *= tick_speed_multiplier;

    constexpr static float time_window_for_jumping = 0.3;

    static struct {
        float floored;
        float pressed_jump;
    } seconds_since;

    // controls.jumping = controls.jumping && (seconds_since.pressed_jump < time_window_for_jumping);

    static bool has_jumped = false;

    WorldHandler& handler = *GetWorldHandler(false);
    // MaterialProps mat = controls.left || controls.right ? floor_props_moving : floor_props_stationary;
    // if (!(controls.left || controls.right) && !data.colliding_with.down) mat.drag.x = 0;

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

    // VOCAB: l/r means the respective side (left or right), mirrorable
    // r/l is the opposite of the respective side

    {
        // TODO: Special action - if the l/r side of the hitbox is colliding with a wall
        // and the top l/r corner of the hitbox is directly touching a ledge, then the
        // player is hanging.
        // [DETECTION] To avoid skipping ledge hanging, then if the player hitbox is colliding
        // l/r and the top l/r corner (if on boundary then bottom-most l/r tile) is not solid, then
        // can_hang = true, else can_hang = false. If it could previously hang but now can't, and
        // the player hitbox is still colliding l/r, then snap upwards until the top l/r corner is
        // between a vertical boundary of (on top = not solid, on bottom = solid)
        // Letting go of l/r or pressing down will make the plalyer jump off,
        // but pressing jump will get back onto the ledge.
    }

    {
        // TODO: Special action - dive - if the player presses boost and down
        // near the same frame (0.1 second difference) while moving up and l/r, the player will
        // do a dive (y vel *= -1.2 to go down, x vel *= 1.3). When on the ground, it will cause the player to tumble.
        //
        // TODO: You can only jump while tumbling (can't steer). Boosting 0.2 seconds within jumping while tumbling
        // will make the player go up and l/r decently far. Attempting to steer in a rollout at least
        // 0.5 seconds old will cause a rollout where you stop tumbling and continue walking in the direction that
        // was steered. Boosting 0.2 seconds within a rollout will kickstart you in the same direction as being steered
        // Tumbling automatically stops after 1.5 seconds if not steered.
    }

    {
        // TODO: Special action - vaulting. `can_vault = can_hang && grounded` If `can_hang` and grounded, then the ledge is too low for
        // hanging on. It isn't too low for vaulting! Jumping after 0.3 seconds of `can_vault` will be as slow as climbing up a ledge
        // Jumping before 0.3 seconds of `can_vault` will cause a proper vault by snapping up, moving l/r and applying x velocity
        // so the player isn't still in freefall.
        // Boosting before 0.2 seconds of being snapped up will kickstart you sideways
    }

    /*
    if (controls.jumping)
        seconds_since.pressed_jump = 0;
    else
        seconds_since.pressed_jump += deltaTime;
    if (data.colliding_with.down) {
        seconds_since.floored = 0;
        has_jumped = false;
    } else
        seconds_since.floored += deltaTime;

    if (controls.left) data.vel.x -= data.speed * mat.speed * deltaTime;
    if (controls.right) data.vel.x += data.speed * mat.speed * deltaTime;
    // if (controls.up) data.vel.y += data.speed * mat.speed * deltaTime;
    // if (controls.down) data.vel.y -= data.speed * mat.speed * deltaTime;
    if (controls.jumping && seconds_since.floored <= 0.0675) {
        data.vel.y += 8 * deltaTime * 300;
        // has_jumped = true;
    }
    //*/

    // TODO: Fix messy code and make it easier to understand

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

    // ------------------------------------------------------------
    // X movement
    // ------------------------------------------------------------

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

    // ------------------------------------------------------------
    // Y movement
    // ------------------------------------------------------------

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
