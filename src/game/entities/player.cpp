#include "player.hpp"
#include <base/renderer.hpp>
#include <base/fps_counter.hpp>
#include <cmath>
#include <sstream>
#include "game/anim.hpp"
#include "game/base/entity_movements.hpp"
#include "utils.hpp"

using namespace Game;
using namespace Base;
using namespace std;

_REGISTER_FOR(new Player(), "player", Entity)

static std::vector<EntityAbility* (*)()> player_abilities{};

#define _register_movement(class_name) _REGISTER_FOR(new class_name(), #class_name, EntityMovement)
#define _register_ability(class_name)                                                                          \
    _REGISTER_FOR(new class_name(), #class_name, EntityAbility)                                                \
    static hidden::Registerer _concat(__player__hidden_counter_, __COUNTER__){[] -> void* { return nullptr; }, \
        [](void* (*)()) { player_abilities.push_back([] -> EntityAbility* { return new class_name(); }); }};

// if we ignore these five lines of spaghetti, this code is actually pretty clean

namespace {
    double max_walk_speed;
}

template <typename T>
inline bool is_doing(const Entity* e) {
    return dynamic_cast<T*>(e->current_movement.get());
}
template <typename T>
inline bool is_doing(const Entity& e) {
    return dynamic_cast<T*>(e.current_movement.get());
}

namespace PlayerMovements {
    struct Walk : EntityMovement {
        using v2i = glm::vec<2, int>;

        virtual float speed_multiplier() { return 1.0f; }
        virtual glm::vec<2, double> drag_multiplier() { return {1.0f, 1.0f}; }

        bool is_walking = false;

        virtual v2i get_running_frame(const Entity* e) {
            constexpr uint stride_length = 8;
            constexpr std::array frames = {
                v2i{0, 16},
                v2i{16, 16},
                v2i{32, 16},
            };
            return frames[(long)mth::round(e->data.pos.x / stride_length * (direction == RIGHT ? 1 : -1)) % frames.size()];
        }
        virtual v2i get_walking_frame(const Entity* e) {
            constexpr uint stride_length = 6;
            constexpr std::array frames = {
                v2i{48, 16},
                v2i{64, 16},
                v2i{80, 16},
            };
            return frames[(long)mth::round(e->data.pos.x / stride_length * (direction == RIGHT ? 1 : -1)) % frames.size()];
        }

        // 's' = slow break (not leaned back as far), 'm' = medium break and 'f' = fast break (leaned back extra). default = m
        virtual v2i get_breaking_frame(const Entity*, char break_level = 'm') {
            switch (break_level) {
                case 's':
                    return {32, 32};
                case 'f':
                    return {16, 32};
                case 'm':
                    return {0, 32};
                default:
                    return {0, 32};
            }
        }
        virtual v2i get_standing_frame(const Entity*) { return {0, 0}; }

        AnimationFrame anim_frame(const Entity* e) override {
            // horizontal speed
            const auto hspeed = abs(e->data.vel.x);

            const auto fix_number = [](const double i) -> double {
                if (isnan(i)) return 0;
                if (isinf(i)) return 0;
                if (i < 0) return 0;
                return i;
            };

            const double vel_percentage = fix_number(hspeed / max_walk_speed) * 100;

            const bool is_controlling = e->controls.left || e->controls.right;

            v2i frame;
            if (vel_percentage > 90 && is_controlling)
                // 90 percent or more of speed
                frame = get_running_frame(e);
            else if (is_controlling)
                frame = get_walking_frame(e);
            else if (hspeed > 90)
                frame = get_breaking_frame(e, 'f');
            else
                frame = get_breaking_frame(e, 'm');
            if (hspeed < 4) frame = get_standing_frame(e);

            AnimationFrame animframe;
            animframe.top_left = frame;
            animframe.size = {16, 16};
            animframe.tileset = "assets/player.png";
            animframe.direction = RIGHT;
            animframe.snap_to_pixel_grid = hspeed <= 2;

            return animframe;
        }

        virtual bool has_drag(const Entity* e) { return e->data.colliding_with.down || (e->controls.left || e->controls.right); }

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
            if (has_drag(_e)) e.data.vel *= 1.0 - (mat.drag * drag_multiplier() * deltaTime);

            if (!(e.controls.left || e.controls.right) && !e.data.colliding_with.down) mat.drag.x = 0;

            const auto multiplier = speed_multiplier();

            is_walking = true;
            if (e.controls.left)
                e.data.vel.x -= e.data.speed * mat.speed * deltaTime * multiplier;
            else if (e.controls.right)
                e.data.vel.x += e.data.speed * mat.speed * deltaTime * multiplier;
            else
                is_walking = false;

            // <AI>
            max_walk_speed = e.data.speed * mat.speed * speed_multiplier() / (mat.drag.x * drag_multiplier().x);
            // </AI>

            const auto absvelx = abs(e.data.vel.x);

            const auto normalize_float = [](const double i) -> double {
                if (isnan(i)) return 0;
                if (isinf(i)) return 0;
                if (i < 0) return 0;
                return i;
            };
            const double vel_percentage = normalize_float(absvelx / max_walk_speed);
            if (round(absvelx) == round(max_walk_speed)) {
                if (e.data.vel.x < 0)
                    e.data.vel.x = -max_walk_speed;
                else
                    e.data.vel.x = max_walk_speed;
            }

            const bool is_controlling = e.controls.left || e.controls.right;
            if (abs(e.data.vel.x) <= 5.0 && !is_controlling && e.data.colliding_with.down) e.data.vel.x = 0;
        }
    } _register_movement(Walk);

    struct QuickTurn : Walk {
        AnimationFrame anim_frame(const Entity* e) override {
            if (time_left > 0.15) return {{16, 32}, {16, 16}, "assets/player.png", LEFT};
            return Walk::anim_frame(e);
        }

        float speed_multiplier() override { return 2.0f; }

        Duration time_left = 0.3;
        void tick(Entity* _e, double deltaTime) override {
            Walk::tick(_e, deltaTime);
            // if it goes negative, it is handled by the ability
            time_left -= deltaTime;
            if (time_left <= 0) _e->set_movement("Walk");
        }
    } _register_movement(QuickTurn);
}  // namespace PlayerMovements

namespace PlayerAbilities {
    struct Jump : EntityAbility {
        const Duration time_to_jump = 0.25;
        bool can_trigger(const Entity* e, double) override { return e->controls.jump && e->data.colliding_with.down; }
        void trigger(Entity* e, double) override { e->data.vel.y += 200; }
    } _register_ability(Jump);

    struct QuickTurn : EntityAbility {
        bool can_trigger(const Entity* e, double deltaTime) override {
            const bool is_walking = dynamic_cast<PlayerMovements::Walk*>(e->current_movement.get());

            if (!is_walking) return false;
            if (!e->data.colliding_with.down) return false;

            const auto& c = e->controls;

            if (c.right && !c.left && e->data.vel.x < -max_walk_speed * 0.75 && c.left.charge > 0.4) return true;
            if (c.left && !c.right && e->data.vel.x > max_walk_speed * 0.75 && c.right.charge > 0.4) return true;

            return false;
        }

        void trigger(Entity* e, double deltaTime) override {
            const auto dir = e->current_movement->direction;
            e->set_movement("QuickTurn");
            e->current_movement->forced_direction = dir;
        }
    } _register_ability(QuickTurn);
}  // namespace PlayerAbilities

using namespace std;
using namespace Base;
using Vertex = Renderer::Vertex;
using Material = Renderer::Material;

Hitbox Player::get_defaults() const {
    return {
        .size = {14, 10},
        .pos = {},
        .vel = {0, 0},
        .speed = 30,
    };
}

void Player::spawn() {
    Entity::spawn();
    const auto add_ability = [&](EntityAbility* ability) {
        current_abilities.push_back(unique_ptr<EntityAbility>(dynamic_cast<EntityAbility*>(ability)));
    };

    set_movement("Walk");
    for (const auto factory : player_abilities) add_ability(factory());
}
