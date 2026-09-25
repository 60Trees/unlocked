#include <base/renderer.hpp>
#include <base/fps_counter.hpp>
#include <cmath>
#include "base/app.hpp"
#include "game/anim.hpp"
#include "game/base/entity_movements.hpp"
#include "utils.hpp"
#include <game/base/entity.hpp>

namespace Game {
    struct Player : Entity {
        Player(const Player& oth, std::function<void(Entity*, const Entity*)> regentity) : Entity(oth, regentity) {}
        Player() = default;

        Entity* clone(std::function<void(Entity*, const Entity*)> regentity) const override { return new Player(*this, regentity); }
        Hitbox get_defaults() const override;
        std::string name() const override { return "player"; }
        void spawn(Base::Application&, const ldtk::Entity* e = nullptr) override;
        std::string get_default_attributes() const override {
            return Entity::get_default_attributes() + ",pick_up_triangles,triggers,canfinish,pushed1,";
        }
        bool does_render() const override { return (!dead) && movement.get(); }

        void tick_all(Base::Application& app) override {
            if (!dead) Entity::tick_all(app);
        }

        float camera_need() const override { return 10.f; }
    };
}  // namespace Game

#undef debug_screen
#define debug_screen(key, msg) \
    do {                       \
    } while (0)

using namespace Game;
using namespace Base;
using namespace std;

_REGISTER_FOR(new Player(), "player", Entity)

static std::vector<EntityAbility* (*)()> player_abilities{};

#define _register_movement(class_name) _REGISTER_FOR(new class_name(), #class_name, EntityMovement)
#define _register_ability(class_name)                                          \
    _REGISTER_FOR(new class_name(), #class_name, EntityAbility)                \
    static hidden::Registerer _concat(__player__hidden_counter_, __COUNTER__){ \
        nullptr, [](void* (*)()) { player_abilities.push_back([] -> EntityAbility* { return new class_name(); }); }};

// if we ignore these five lines of spaghetti, this code is actually pretty clean

namespace {
    double max_walk_speed;
}

template <typename T>
inline bool is_doing(const Entity* e) {
    return dynamic_cast<T*>(e->movement.get());
}
template <typename T>
inline bool is_doing(const Entity& e) {
    return dynamic_cast<T*>(e.movement.get());
}
extern "C" double gravity_multiplier();

namespace PlayerMovements {
    struct Walk : EntityMovement {
        Duration time_spent_walking = 0.0f;
        bool is_walking = false;
        double anim_offset = 0.0;
        Walk(const Walk& oth)
            : EntityMovement(oth), time_spent_walking(oth.time_spent_walking), is_walking(oth.is_walking), anim_offset(oth.anim_offset) {}
        Walk() = default;

        EntityMovement* clone() override { return new Walk(*this); }
        virtual bool can_be_considered_walking(const Entity*) { return true; }

        virtual bool is_changing_direction(const Entity* e) {
            if (e->controls.right && e->data.vel.x < 0) return true;
            if (e->controls.left && e->data.vel.x > 0) return true;
            return false;
        }

        using v2i = glm::vec<2, int>;

        virtual float speed_multiplier(const Entity*) { return 1.0f; }
        virtual glm::vec<2, double> drag_multiplier(const Entity*) { return {1.0f, 1.0f}; }

        virtual v2i get_running_frame(const Entity* e) {
            constexpr uint stride_length = 8;
            constexpr std::array frames = {
                v2i{0, 16},
                v2i{16, 16},
                v2i{32, 16},
            };
            return frames[(long)mth::round((e->data.pos.x - anim_offset) / stride_length * (direction == RIGHT ? 1 : -1)) % frames.size()];
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
            if (vel_percentage > 90 && is_controlling) {
                frame = get_running_frame(e);
            } else if (is_controlling) {
                frame = get_walking_frame(e);
                anim_offset = e->data.pos.x;
            } else if (hspeed > 90) {
                frame = get_breaking_frame(e, 'f');
                anim_offset = e->data.pos.x;
            } else if (hspeed > 4) {
                frame = get_breaking_frame(e, 'm');
                anim_offset = e->data.pos.x;
            } else {
                anim_offset = e->data.pos.x;
                frame = get_standing_frame(e);
            }

            debug_screen(e << "anim_offset", "Anim offset: " << anim_offset << "\nX offset: " << e->data.pos.x);

            AnimationFrame animframe;
            animframe.top_left = frame;
            animframe.size = {16, 16};
            animframe.tileset = "assets/player.png";
            animframe.direction = RIGHT;
            animframe.snap_to_pixel_grid = hspeed <= 2;

            return animframe;
        }

        virtual bool has_drag(const Entity* e) { return e->data.colliding_with.down || (e->controls.left || e->controls.right); }

        virtual Player::MaterialProps get_material_props(const Entity* e) {
            constexpr static Player::MaterialProps floor_props_moving = {
                .drag = {6, 0.1},
                .speed = 20,
            };
            constexpr static Player::MaterialProps floor_props_stationary = {
                .drag = {10, 0.1},
                .speed = 0,
            };
            return e->controls.left || e->controls.right ? floor_props_moving : floor_props_stationary;
        }

        void tick(Entity* _e, double deltaTime) override {
            Entity& e = *_e;

            if (deltaTime < 0) return;

            const auto normalize_float = [](const double i) -> double {
                if (isnan(i)) return 0;
                if (isinf(i)) return 0;
                if (i < 0) return 0;
                return i;
            };

            debug_screen("tsw", "Time spent walking: " << time_spent_walking);

            time_spent_walking += deltaTime;
            if (time_spent_walking < 0) time_spent_walking = 0;
            if (isnan(time_spent_walking)) time_spent_walking = 0;
            if (isinf(time_spent_walking)) time_spent_walking = 0;

            e.data.vel += e.get_gravity() * deltaTime * gravity_multiplier();

            const auto spdmultiplier = speed_multiplier(_e);
            const auto dragmultiplier = drag_multiplier(_e);

            auto mat = get_material_props(_e);

            if (has_drag(_e)) e.data.vel *= 1.0 - (mat.drag * dragmultiplier * deltaTime);

            if (!(e.controls.left || e.controls.right) && !e.data.colliding_with.down) mat.drag.x = 0;

            is_walking = true;
            if (e.controls.left)
                e.data.vel.x -= e.data.speed * mat.speed * deltaTime * spdmultiplier;
            else if (e.controls.right)
                e.data.vel.x += e.data.speed * mat.speed * deltaTime * spdmultiplier;
            else
                is_walking = false;

            // <AI>
            const auto new_max_walk_speed = e.data.speed * mat.speed * spdmultiplier / (mat.drag.x * dragmultiplier.x);
            // </AI>
            if (normalize_float(new_max_walk_speed) != 0) max_walk_speed = new_max_walk_speed;

            const auto absvelx = abs(e.data.vel.x);

            const double vel_percentage = normalize_float(absvelx / max_walk_speed);
            if (round(absvelx) == round(max_walk_speed)) {
                if (e.data.vel.x < 0)
                    e.data.vel.x = -max_walk_speed;
                else
                    e.data.vel.x = max_walk_speed;
            }

            const bool is_controlling = e.controls.left || e.controls.right;
            if (abs(e.data.vel.x) <= 5.0 && !is_controlling && e.data.colliding_with.down) e.data.vel.x = 0;

            debug_screen(_e << " vel", "X velocity: " << e.data.vel.x);
            debug_screen(_e << " max speed", "Max walking speed: " << max_walk_speed);
        }
    } _register_movement(Walk);

    struct QuickTurn : Walk {
        bool is_slow;
        constexpr static Duration default_time_left = 0.3;
        Duration time_left = default_time_left;
        bool has_enacted_boost = false;

        QuickTurn(const QuickTurn& oth)
            : Walk(oth), is_slow(oth.is_slow), time_left(oth.time_left), has_enacted_boost(oth.has_enacted_boost) {}
        QuickTurn() = default;

        EntityMovement* clone() override { return new QuickTurn(*this); }

        bool can_be_considered_walking(const Entity* e) override { return true; }

        AnimationFrame anim_frame(const Entity* e) override {
            if (is_changing_direction(e)) {
                anim_offset = e->data.pos.x;
                return {get_breaking_frame(e, 'f'), {16, 16}, "assets/player.png", LEFT};
            }
            max_walk_speed *= 1 / speed_multiplier(e);
            const auto retval = Walk::anim_frame(e);
            max_walk_speed /= 1 / speed_multiplier(e);
            return retval;
        }

        float speed_multiplier(const Entity* e) override { return is_changing_direction(e) ? 0.5 : 2.0f; }

        void tick(Entity* e, double deltaTime) override {
            Walk::tick(e, deltaTime);

            if (e->data.colliding_with.left || e->data.colliding_with.right) is_slow = true;

            if (!is_changing_direction(e) && !has_enacted_boost) {
                forced_direction = std::nullopt;
                if (e->data.vel.x < 0)
                    e->data.vel.x = -max_walk_speed;
                else
                    e->data.vel.x = max_walk_speed;
                has_enacted_boost = true;
            }

            if (!is_changing_direction(e))
                time_left -= deltaTime;
            else
                time_spent_walking = 0.0f;

            if (time_left <= 0 || !e->data.colliding_with.down || (is_slow && !is_changing_direction(e))) {
                e->set_movement<Walk>();
                auto walk = e->get_movement<Walk>();
                walk->anim_offset = anim_offset;

                if (!is_slow) {
                    walk->time_spent_walking = default_time_left;
                } else {
                    walk->time_spent_walking = 0;
                }
            }
        }
    } _register_movement(QuickTurn);

    struct Jumping : Walk {
        bool can_be_considered_walking(const Entity*) override { return false; }

        virtual Player::MaterialProps get_material_props(const Entity* e) override {
            constexpr static Player::MaterialProps air_props_opposing = {
                .drag = {1, 0.1},
                .speed = 8,
            };
            constexpr static Player::MaterialProps air_props_normal = {
                .drag = {0, 0.1},
                .speed = 0,
            };
            bool is_normal = false;
            if ((e->data.vel.x > 0) == direction) is_normal = true;
            if (abs(e->data.vel.x) < 100) is_normal = false;
            if (is_normal)
                debug_screen("on", "");
            else
                debug_screen("on", "Opposing");
            return is_normal ? air_props_normal : air_props_opposing;
        }

        constexpr static Duration max_jump_time = 0.07;
        bool just_jumped = true;

        void tick(Entity* e, double deltaTime) override {
            if (!just_jumped && e->data.colliding_with.down) {
                e->set_movement<Walk>();
            }

            const bool is_jumping = jumptime < max_jump_time && e->controls.jump;

            if (just_jumped && is_jumping) e->data.vel.y += 100;
            just_jumped = false;

            if (is_jumping) {
                // <AI> Fix to stop lag spikes causing the player to jump too high
                const double remaining = max_jump_time - jumptime;
                const double dt_boost = std::min(deltaTime, remaining);
                e->data.vel.y += 2000 * dt_boost;
                jumptime += deltaTime;
                // </AI>
            } else if (!e->controls.jump)
                jumptime = max_jump_time;

            debug_screen("hai", "Jumptime: " << jumptime << "\ndt: " << deltaTime);
            debug_screen("aaa", "Y vel: " << e->data.vel.y);

            Walk::tick(e, deltaTime);
        }

        Duration jumptime = 0.0;

        AnimationFrame anim_frame(const Entity* e) override {
            // horizontal speed
            const auto hspeed = abs(e->data.vel.x);

            v2i frame = {32, 48};

            constexpr static std::array jumping{v2i{32, 64}, v2i{32, 48}, v2i{16, 48}};
            constexpr static std::array falling{v2i{0, 48}, v2i{0, 64}, v2i{16, 64}};

            const auto yvel = e->data.vel.y;

            AnimationFrame animframe;
            // tilted high
            if (yvel >= 100) animframe.top_left = jumping[0];
            // tilted medium
            else if (yvel > 30)
                animframe.top_left = jumping[1];
            // straight
            else if (yvel > -10)
                animframe.top_left = jumping[2];
            // tilted a little down
            else if (yvel > -100)
                animframe.top_left = falling[0];
            // tilted a lot down
            else if (yvel > -170)
                animframe.top_left = falling[1];
            // practically diving
            else
                animframe.top_left = falling[2];
            animframe.size = {16, 16};
            animframe.tileset = "assets/player.png";
            animframe.direction = true ? (e->data.vel.x > 0) == (direction) : direction;
            animframe.snap_to_pixel_grid = false;

            return animframe;
        }
    } _register_movement(Jumping);
}  // namespace PlayerMovements

namespace PlayerAbilities {
    struct Jump : EntityAbility {
        Jump(const Jump& oth) : EntityAbility(oth) {}
        Jump() = default;
        EntityAbility* clone() override { return new Jump(*this); }

        bool can_trigger(const Entity* e, double) override {
            // if already jumping then ignore
            if (e->movement_based_off<PlayerMovements::Jumping>()) return false;
            if (!e->movement_based_off<PlayerMovements::Walk>()) return false;
            if (!e->get_movement<PlayerMovements::Walk>()->can_be_considered_walking(e)) return false;

            if (e->controls.jump.time == 0 && e->controls.jump && e->data.colliding_with.down) {
                return true;
            }
            if (!e->data.colliding_with.down) {
                return true;
            }
            return false;
        }
        void trigger(Entity* e, double) override {
            e->set_movement<PlayerMovements::Jumping>();
            bool can_jump = e->data.colliding_with.down;
            e->get_movement<PlayerMovements::Jumping>()->jumptime = can_jump ? 0.0f : PlayerMovements::Jumping::max_jump_time;
        }
    } _register_ability(Jump);

    struct QuickTurn : EntityAbility {
        QuickTurn(const QuickTurn& oth) : EntityAbility(oth) {}
        QuickTurn() = default;
        EntityAbility* clone() override { return new QuickTurn(*this); }

        bool can_trigger(const Entity* e, double deltaTime) override {
            if (!e->movement_is_exactly<PlayerMovements::Walk>()) return false;
            PlayerMovements::Walk* const walking = e->get_movement<PlayerMovements::Walk>();

            if (!walking) return false;
            if (!e->data.colliding_with.down) return false;

            const auto& c = e->controls;

            if (!walking->is_changing_direction(e)) return false;

            if (c.right && !c.left) return true;
            if (c.left && !c.right) return true;

            return false;
        }

        void trigger(Entity* e, double deltaTime) override {
            auto* walking = e->get_movement<PlayerMovements::Walk>();
            const auto dir = walking->direction;

            const auto is_slow = [&] {
                if (abs(e->data.vel.x) < 80) return true;
                return false;
            }();

            e->set_movement<PlayerMovements::QuickTurn>();

            if (is_slow)
                debug_screen(e << "slow", "Slow (time=" << walking->time_spent_walking << ")");
            else
                debug_screen(e << "slow", "Not slow");

            e->set_movement<PlayerMovements::QuickTurn>();

            auto* turning = dynamic_cast<PlayerMovements::QuickTurn*>(e->movement.get());
            turning->is_slow = is_slow;
            turning->forced_direction = dir;
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
        .speed = 30,
    };
}

void Player::spawn(Base::Application& app, const ldtk::Entity* e) {
    Entity::spawn(app, e);
    const auto add_ability = [&](EntityAbility* ability) {
        current_abilities.push_back(unique_ptr<EntityAbility>(dynamic_cast<EntityAbility*>(ability)));
    };

    set_movement<PlayerMovements::Walk>();
    for (const auto factory : player_abilities) add_ability(factory());
}
