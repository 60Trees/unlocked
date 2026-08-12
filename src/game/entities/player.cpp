#include "player.hpp"
#include <base/renderer.hpp>
#include <base/fps_counter.hpp>
#include "game/base/entity_movements.hpp"
#include "utils.hpp"

using namespace Game;
using namespace Base;
using namespace std;
constexpr bool X_AXIS = 0, Y_AXIS = 1;
constexpr bool LEFT = 0, RIGHT = 1;

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

            // <AI>
            max_walk_speed = e.data.speed * mat.speed * speed_multiplier() / (mat.drag.x * drag_multiplier().x);
            // </AI>
        }
    } _register_movement(Walk);

    struct QuickTurn : Walk {
        float speed_multiplier() override { return 2.0f; }

        Duration time_left = 0.3;
        void tick(Entity* _e, double deltaTime) override {
            Walk::tick(_e, deltaTime);
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
            const bool is_walking = dynamic_cast<PlayerMovements::Walk*>(e->current_movement.get());

            if (!is_walking) return false;
            if (!e->data.colliding_with.down) return false;

            {
                auto* movement = dynamic_cast<PlayerMovements::QuickTurn*>(e->current_movement.get());
                if (movement) return movement->time_left > 0.0;
            }

            const auto& c = e->controls;

            if (c.right && !c.left && e->data.vel.x < -max_walk_speed * 0.75 && c.left.charge > 0.4) return true;
            if (c.left && !c.right && e->data.vel.x > max_walk_speed * 0.75 && c.right.charge > 0.4) return true;

            return false;
        }

        void when_active(Entity* e, double deltaTime) override {
            const bool is_quickturning = dynamic_cast<PlayerMovements::QuickTurn*>(e->current_movement.get());
            if (!is_quickturning) e->set_movement("QuickTurn");
        }
        void when_deactive(Entity* e, double deltaTime) override {
            const bool is_quickturning = dynamic_cast<PlayerMovements::QuickTurn*>(e->current_movement.get());
            if (is_quickturning) e->set_movement("Walk");
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
    const auto add_ability = [&](EntityAbility* ability) {
        current_abilities.push_back(unique_ptr<EntityAbility>(dynamic_cast<EntityAbility*>(ability)));
    };

    set_movement("Walk");
    for (const auto factory : player_abilities) add_ability(factory());
}

void Player::render(Renderer& r, Renderer::VertexLayer& layer, double deltaTime) const {
    auto new_layer = anim.get_rendered(deltaTime, *this);
    layer = new_layer;
}

