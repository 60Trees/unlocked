#pragma once

#include <base/renderer.hpp>
#include <base/base.hpp>
#include <glm/vec2.hpp>
#include <memory>
#include "entity_movements.hpp"

namespace Game {
    /// `0` = activated this frame, `<0` = not activated, `>0` = how long it's been activated for (seconds)
    typedef float Duration;

    struct Entity;
    struct EntityList;

    struct ControlData {
        /**
         * @note Dependant on `pressed` being `true` or `false`.
         *
         * 0 means prsesed on this frame
         *
         * >0 means for how many seconds it has been pressed or
         * released
         */
        Duration time;

        /**
         * @note Not dependant on `pressed` (unlike `time`)
         *
         * This goes up when pressed and down when not pressed
         * (1.0f per second). It is limited by both
         * `upper_charge_limit` and `lower_charge_limit`.
         */
        float charge;
        constexpr static float upper_charge_limit = 0.5f;
        constexpr static float lower_charge_limit = 0.0f;

        bool pressed;

        constexpr inline operator bool() const { return pressed; }

        void update(double deltaTime, bool new_pressed);
    };

    struct Hitbox {
        using vec2_t = glm::vec<2, double>;
        vec2_t size;
        vec2_t pos;
        vec2_t vel;
        double speed;

        struct {
            ControlData up, down, left, right;
        } colliding_with;

        _nodisc_i double left_edge() const { return pos.x - size.x / 2; }
        _nodisc_i double right_edge() const { return pos.x + size.x / 2; }
        _nodisc_i double top_edge() const { return pos.y + size.y; }
        _nodisc_i double bottom_edge() const { return pos.y; }
        _nodisc_i vec2_t top_left() const { return {left_edge(), top_edge()}; }
        _nodisc_i vec2_t top_right() const { return {right_edge(), top_edge()}; }
        _nodisc_i vec2_t bottom_left() const { return {left_edge(), bottom_edge()}; }
        _nodisc_i vec2_t bottom_right() const { return {right_edge(), bottom_edge()}; }
        _nodisc_i vec2_t hitbox_center() const { return {pos.x, pos.y + size.y / 2}; }
    };

    struct EntityController {
        virtual ~EntityController() = default;
        virtual void update_controls(Entity& own, const EntityList& others, double deltaTime) const {}
    };

    struct Entity {
        using vec2_t = glm::vec<2, double>;
        Hitbox data;

        std::shared_ptr<EntityMovement> movement;
        std::vector<std::shared_ptr<EntityAbility>> current_abilities;

        /// @detail Does nothing if `T` cant convert to EntityMovement
        template <typename T>
        inline void set_movement(std::function<T*()> create = []() { return new T(); }) {
            std::shared_ptr<EntityMovement> new_ptr = std::shared_ptr<EntityMovement>{dynamic_cast<EntityMovement*>(create())};
            if (!new_ptr) return;

            const auto previous_direction = movement ? movement->direction : RIGHT;
            movement = new_ptr;
            movement->direction = previous_direction;
        }

        template <typename T>
        inline T* get_movement() const {
            if (!movement) return nullptr;
            return dynamic_cast<T*>(movement.get());
        }

        template <typename T>
        inline bool movement_is_exactly() const {
            if (!movement) return false;
            auto mptr = movement.get();
            return typeid(*mptr) == typeid(T);
        }
        template <typename T>
        inline bool movement_based_off() const {
            if (!movement) return false;
            auto mptr = movement.get();
            return dynamic_cast<T*>(mptr);
        }

        struct Controls {
            ControlData up, down, left, right;
            ControlData boost;
            ControlData jump;
        } controls;

        std::unique_ptr<EntityController> controller = std::make_unique<EntityController>();

        virtual vec2_t get_gravity();

        struct MaterialProps {
            // vel *= 1 - (drag * deltaTime)
            vec2_t drag;
            double speed;
        };

        virtual void spawn() { data = get_defaults(); }

        virtual void tick_all(double deltaTime);
        virtual void tick(double deltaTime) {}
        virtual void tick_position(double deltaTime);
        virtual void despawn() {}

        virtual Hitbox get_defaults() const = 0;

        virtual void render(Base::Renderer& r, Base::Renderer::VertexLayer& layer, double deltaTime) const;
        virtual std::string name() const = 0;
        virtual ~Entity() = default;

        bool wants_to_despawn = false;

        _REGISTERABLE(Entity);
    };
};  // namespace Game
