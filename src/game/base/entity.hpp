#pragma once

#include <base/renderer.hpp>
#include <base/base.hpp>
#include <functional>
#include <glm/vec2.hpp>

namespace Game {
    using RenderFunctionParams = std::pair<std::vector<Base::Renderer::Vertex>, Base::Renderer::Material>;
    struct Entity;
    struct EntityList;

    struct EntityAnim {
        virtual ~EntityAnim() = default;
        virtual Base::Renderer::TexturedRectDescriptor::Rect<uint16_t> get_anim_stage() const = 0;
        virtual std::string get_atlas() const = 0;
        virtual bool has_custom_draw_command() const { return false; }
        virtual void custom_render(const Entity* own_entity, Base::Renderer& r, Base::Renderer::VertexLayer& layer) const {
            unimplemented_code;
        }

        _REGISTERABLE_SINGLETON(EntityAnim);
    };

    struct EntityController {
        virtual ~EntityController() = default;
        virtual void update_controls(Entity& own, const EntityList& others) const {}
    };
    struct BasicEntityController : EntityController {
        using Func = std::function<void(Entity&, const EntityList&)>;
        Func func;
        BasicEntityController(Func f) : func(f) {}
        BasicEntityController() : func([](Entity&, const EntityList&) {}) {}
        BasicEntityController(BasicEntityController&& o) = default;
        BasicEntityController(const BasicEntityController& o) = default;
        virtual void update_controls(Entity& own, const EntityList& others) const override { func(own, others); }
    };

    struct Entity {
        using vec2_t = glm::vec<2, double>;
        // using EntityController = std::function<void(Entity& own, const EntityList& others)>;
        struct PosData {
            vec2_t pos;
            vec2_t vel;
            vec2_t size;
            double speed;

            inline double left_edge() const { return pos.x - size.x / 2; }
            inline double right_edge() const { return pos.x + size.x / 2; }
            inline double top_edge() const { return pos.y + size.y; }
            inline double bottom_edge() const { return pos.y; }
            inline vec2_t hitbox_center() const { return {pos.x, pos.y + size.y / 2}; }
        } data;

        struct Controls {
            bool up, down, left, right;
        } controls;

        std::unique_ptr<EntityController> controller = std::make_unique<EntityController>();

        virtual vec2_t get_gravity() { return {0.0, 1.0}; };

        // 0 = no drag
        struct MaterialProps {
            double drag;
            double speed;
        };

        constexpr static MaterialProps air_props = {
            .drag = 0.2,
            .speed = 0.8,
        };
        constexpr static MaterialProps flight_props = {
            .drag = 4,
            .speed = 20,
        };

        std::string anim_stage = "null";

        virtual void spawn() { data = get_defaults(); }
        virtual void tick_all(double deltaTime) {
            tick_position(deltaTime);
            tick(deltaTime);
        }
        virtual void tick(double deltaTime) {}
        virtual void tick_position(double deltaTime) {
            const MaterialProps& mat = flight_props;
            if (controls.left) data.vel.x -= data.speed * mat.speed * deltaTime;
            if (controls.right) data.vel.x += data.speed * mat.speed * deltaTime;
            if (controls.up) data.vel.y += data.speed * mat.speed * deltaTime;
            if (controls.down) data.vel.y -= data.speed * mat.speed * deltaTime;

            data.pos += data.vel * deltaTime;
            data.vel *= 1.0 - mat.drag * deltaTime;
        }
        virtual void despawn() {}

        virtual PosData get_defaults() const = 0;

        virtual void render(Base::Renderer& r, Base::Renderer::VertexLayer& layer) const = 0;
        virtual std::string name() const = 0;
        inline const EntityAnim& anim() const { return EntityAnim::get(anim_stage); }
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
