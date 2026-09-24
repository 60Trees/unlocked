#pragma once
#include "entity.hpp"
#include <LDtkLoader/Entity.hpp>

namespace Game {
    struct EntityList : Base::AppModule {
        void init() override { assert(parent); }
        void loop() override {}
        void quit() override {}

        using index_t = size_t;

        index_t camera_following_entity = EntityList::null_index;
        index_t main_character = EntityList::null_index;

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

        index_t get_entity_index(const Entity* e) const {
            for (const auto& [i, entity] : entities)
                if (entity.get() == e) return i;
            return null_index;
        }

        index_t spawn_entity(std::string_view entity_name, const ldtk::Entity* e = nullptr) {
            if (!Entity::contains(std::string(entity_name))) return null_index;
            const auto id = get_empty_index();
            entities[id] = std::unique_ptr<Entity>{Entity::make_new(std::string{entity_name})};
            entities[id]->spawn(*parent, e);
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
            for (const auto i : deleted_entities) {
                if (camera_following_entity == i) camera_following_entity = null_index;
                if (main_character == i) main_character = null_index;
                entities.erase(i);
            }
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
}  // namespace Game
