#pragma once

#include <base/renderer.hpp>
#include <base/base.hpp>
#include <functional>
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

    struct EntityAnim {
        typedef Base::Renderer::VertexLayer RenderedOutput;

        /**
         * Returns a vector of vertexes as well as a material.
         * The vertexes are positioned so that 0,0 is the bottom middle
         * of the player, so when rendered it will be shifted by player_pos
         * units.
         */
        std::function<RenderedOutput(uint*, double deltaTime, const Hitbox& hitbox, Base::Renderer& r)> render_lambda = nullptr;

        RenderedOutput get_rendered(
            double deltaTime, const Hitbox& hitbox, Base::Renderer& r = *dynamic_cast<Base::Renderer*>(GetRenderer(false))) const;
        RenderedOutput get_rendered(
            double deltaTime, const Entity& e, Base::Renderer& r = *dynamic_cast<Base::Renderer*>(GetRenderer(false))) const;

        _REGISTERABLE_SINGLETON(EntityAnim);
    };

    struct EntityController {
        virtual ~EntityController() = default;
        virtual void update_controls(Entity& own, const EntityList& others, double deltaTime) const {}
    };

    struct Entity {
        using vec2_t = glm::vec<2, double>;
        Hitbox data;


        std::unique_ptr<EntityMovement> current_movement;
        std::vector<std::unique_ptr<EntityAbility>> current_abilities;

        /// @detail Does nothing if `movement` doesn't exist
        inline void set_movement(const std::string& movement) {
            auto* new_movement = EntityMovement::make_new(movement);
            if (new_movement) current_movement.reset(new_movement);
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

        EntityAnim anim{};

        virtual void spawn() { data = get_defaults(); }

        virtual void tick_all(double deltaTime);
        virtual void tick(double deltaTime) {}
        virtual void tick_position(double deltaTime);
        virtual void despawn() {}

        virtual Hitbox get_defaults() const = 0;

        virtual void render(Base::Renderer& r, Base::Renderer::VertexLayer& layer, double deltaTime) const = 0;
        virtual std::string name() const = 0;
        virtual ~Entity() = default;

        bool wants_to_despawn = false;

        _REGISTERABLE(Entity);
    };

    struct EntityList {
        using index_t = size_t;
        constexpr static index_t null_index = std::numeric_limits<index_t>::max();
        [[nodiscard]] index_t get_id_from(const Entity* other) const {
            for (const auto& [i, entity] : entities) {
                if (entity.get() == other) return i;
            }
            return null_index;
        };
        [[nodiscard]] index_t get_empty_index() const {
            index_t expected = 0;
            for (const auto& [i, _] : entities) {
                if (expected != null_index && i != expected) return expected;
                expected++;
            }
            if (expected == null_index) expected++;
            return expected;
        }
        [[nodiscard]] index_t spawn_entity(std::string_view entity_name) {
            const auto id = get_empty_index();
            entities[id] = std::unique_ptr<Entity>{Entity::make_new(std::string{entity_name})};
            entities[id]->spawn();
            return id;
        }
        [[nodiscard]] bool exists(const index_t entity_index) const {
            return entity_index != null_index && entities.contains(entity_index);
        }
        void delete_entity(index_t specific_entity = null_index, bool clean = true) {
            if (specific_entity != null_index) entities.erase(specific_entity);
            std::vector<index_t> deleted_entities{};
            for (const auto& [i, entity] : entities) {
                if (!entity || entity->wants_to_despawn) deleted_entities.push_back(i);
                if (entity) entity->despawn();
            }
            for (const auto i : deleted_entities) entities.erase(i);
        }
        inline void clean_entities() { delete_entity(null_index, true); }

        inline Entity& operator[](const index_t i) { return *entities.at(i); }
        inline const Entity& operator[](const index_t i) const { return *entities.at(i); }

        inline auto begin() { return entities.begin(); }
        inline const auto begin() const { return entities.begin(); }
        inline auto end() { return entities.end(); }
        inline const auto end() const { return entities.end(); }
        inline auto rbegin() { return entities.rbegin(); }
        inline const auto rbegin() const { return entities.rbegin(); }
        inline auto rend() { return entities.rend(); }
        inline const auto rend() const { return entities.rend(); }

        private:
        std::map<index_t, std::unique_ptr<Entity>> entities;
    };
};  // namespace Game
