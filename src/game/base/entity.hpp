#pragma once

#include <SDL3/SDL_events.h>
#include <base/renderer.hpp>
#include <base/base.hpp>
#include <cmath>
#include <glm/vec2.hpp>
#include <memory>
#include "base/app.hpp"
#include "entity_movements.hpp"
#include "game/anim.hpp"
#include <LDtkLoader/Entity.hpp>
#include <nlohmann/json.hpp>

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
        Duration time = INFINITY;

        /**
         * @note Not dependant on `pressed` (unlike `time`)
         *
         * This goes up when pressed and down when not pressed
         * (1.0f per second). It is limited by both
         * `upper_charge_limit` and `lower_charge_limit`.
         */
        float charge = 0;
        constexpr static float upper_charge_limit = 0.5f;
        constexpr static float lower_charge_limit = 0.0f;

        bool pressed = false;

        constexpr inline operator bool() const { return pressed; }
        inline bool just_pressed() { return pressed && time == 0; }
        inline bool just_released() { return !pressed && time == 0; }

        void update(const Base::Application&, bool new_pressed);
    };

    struct Hitbox {
        using vec2_t = glm::vec<2, double>;
        vec2_t size;
        vec2_t pos = {0, 0};
        vec2_t vel = {0, 0};
        double speed = 0;

        struct {
            ControlData up, down, left, right;
        } colliding_with;

        _nodisc_i double left() const { return pos.x - size.x / 2; }
        _nodisc_i double right() const { return pos.x + size.x / 2; }
        _nodisc_i double top() const { return pos.y + size.y; }
        _nodisc_i double bottom() const { return pos.y; }
        _nodisc_i vec2_t top_left() const { return {left(), top()}; }
        _nodisc_i vec2_t top_right() const { return {right(), top()}; }
        _nodisc_i vec2_t bottom_left() const { return {left(), bottom()}; }
        _nodisc_i vec2_t bottom_right() const { return {right(), bottom()}; }
        _nodisc_i vec2_t hitbox_center() const { return {pos.x, pos.y + size.y / 2}; }
    };

    struct EntityController {
        virtual ~EntityController() = default;
        virtual void digest_event(SDL_Event& e) {}
        virtual void update_controls(Entity& own, const Base::Application&) {}
    };

    struct Entity {
        using vec2_t = glm::vec<2, double>;
        Hitbox data;

        std::shared_ptr<EntityMovement> movement;
        std::vector<std::shared_ptr<EntityAbility>> current_abilities;

        std::vector<std::string> attributes{};
        virtual std::string get_default_attributes() const { return {}; };
        virtual bool has_attribute(std::string_view to_find) const {
            return std::find(attributes.begin(), attributes.end(), to_find) != attributes.end();
        }

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
            // NAN = not focused, 0-360 is angle
            float focusDegrees = NAN;
        } controls;

        std::unique_ptr<EntityController> controller = std::make_unique<EntityController>();

        virtual vec2_t get_gravity();

        struct MaterialProps {
            // vel *= 1 - (drag * deltaTime)
            vec2_t drag;
            double speed;
        };

        std::vector<Entity*> children{};
        Entity* parent = nullptr;

        inline void adopt(Entity* new_child) {
            // Fully transfer ownership -- an entity must never be a "child" of more than one parent,
            // or every parent's tick_all() will keep fighting over its ->parent pointer forever.
            if (new_child->parent && new_child->parent != this) {
                new_child->parent->disown(new_child, true);
            }

            new_child->parent = this;
            if (std::find(children.begin(), children.end(), new_child) != children.end()) return;

            children.push_back(new_child);
        }
        inline void disown(Entity* unwanted_child, bool silent = false) {
            // if it's already disowed then ignore
            if (std::find(children.begin(), children.end(), unwanted_child) == children.end()) return;
            if (!silent) unwanted_child->when_disowned();
            children.erase(remove_if(children.begin(), children.end(), [&](Entity* i) { return i == unwanted_child; }), children.end());
            unwanted_child->parent = nullptr;
        }

        virtual void spawn(Base::Application& app, const ldtk::Entity* e = nullptr);

        virtual void tick_all(Base::Application& app);
        virtual void tick(Base::Application& app) {}
        virtual void tick_position(Base::Application& app);
        virtual void despawn() {}

        /// Ran directly before it will be disowned, so `parent` is still valid
        virtual void when_disowned() {}

        Duration pause_time = 0.0f;

        virtual bool colliding_with(const Entity* other) const;

        virtual Hitbox get_defaults() const = 0;

        virtual void render(Base::Application& app, Base::Renderer::VertexLayer& layer);
        virtual std::string name() const = 0;
        virtual ~Entity() = default;

        bool wants_to_despawn = false;

        virtual bool does_render() const { return movement.get(); }
        virtual AnimationFrame get_anim_frame(Base::Application& app) const {
            if (!movement) return {};
            return movement->anim_frame(this);
        }
        virtual Direction get_direction() const {
            if (!movement) return false;
            return movement->direction;
        }

        virtual nlohmann::json dump_as_json() const { return {{"name", name()}, {"x", data.pos.x}, {"y", data.pos.y}}; }

        friend std::ostream& operator<<(std::ostream& os, const Entity& obj) {
            os << obj.dump_as_json();
            return os;
        }

        _REGISTERABLE(Entity);
    };
};  // namespace Game
