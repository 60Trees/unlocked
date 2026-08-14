#pragma once

#include <sys/types.h>
#include <LDtkLoader/Project.hpp>

#include <base/renderer.hpp>
#include <cstdint>
#include <stdexcept>
#include <string>
#include "glm/detail/qualifier.hpp"

namespace Game {
    struct WorldHandler;
}

GETTER_DEFINITION(Game::WorldHandler, GetWorldHandler);

namespace Game {
    struct CollisionMap {
        enum CollisionType : uint8_t {
            AIR = 0,
            SOLID = 1,
            WATER = 2,
            LADDER = 3,
            LAVA = 4,
        };

        glm::vec<2, int> offset = {0, 0};

        glm::vec<2, uint> size;
        double scale = 1.0;

        void write(CollisionType data, glm::vec<2, uint> pos);
        CollisionType read(glm::vec<2, uint> pos);

        void fix_map_size() {
            map.resize(size.x);
            for (auto& column : map) column.resize(size.y);
        }

        inline CollisionType operator[](size_t x, size_t y) { return read({x, y}); }
        inline CollisionType operator[](glm::vec<2, uint> pos) { return read(pos); }

        std::vector<std::vector<CollisionType>> map;
    };

    struct PlacedLevel;

    struct WorldHandler {
        virtual ~WorldHandler() = default;

        ldtk::Project main_world;

        std::map<const ldtk::Level*, CollisionMap> collisions;
        std::vector<PlacedLevel> placed_levels;

        virtual void render(const ldtk::Level& level, Base::Renderer::VertexArray& leveltris,
            Base::Renderer& renderer = *dynamic_cast<Base::Renderer*>(GetRenderer(false)), glm::vec<2, int> offset = {0, 0}) = 0;

        virtual void uploadAllTilesets(Base::Renderer& r, bool logs = false) = 0;

        inline const ldtk::World& getworld(size_t worldindex) const {
            for (const auto& w : main_world.allWorlds()) {
                if (worldindex == 0) return w;
                worldindex++;
            }
            throw std::runtime_error("Index out of range");
        }
        inline const ldtk::World& getworld(const std::string& worldname) const { return main_world.getWorld(worldname); }

        inline const ldtk::Level& getlevel(size_t levelindex, const ldtk::World& world) const {
            for (const auto& l : world.allLevels()) {
                if (levelindex == 0) return l;
                levelindex++;
            }
            throw std::runtime_error("Index out of range");
        }
        inline const ldtk::Level& getlevel(const std::string& levelname, const ldtk::World& world) const {
            return world.getLevel(levelname);
        }
        inline const ldtk::Level& getlevel(size_t levelindex, const std::string& worldname) const {
            return getlevel(levelindex, getworld(worldname));
        }
        inline const ldtk::Level& getlevel(const std::string& levelname, const std::string& worldname) const {
            return getlevel(levelname, getworld(worldname));
        }
        inline const ldtk::Level& getlevel(size_t levelindex, size_t worldindex) const {
            return getlevel(levelindex, getworld(worldindex));
        }
        inline const ldtk::Level& getlevel(const std::string& levelname, size_t worldindex) const {
            return getlevel(levelname, getworld(worldindex));
        }
    };

    struct PlacedLevel {
        const ldtk::Level& level;
        glm::vec<2, int> offset = {0, 0};

        inline void render(
            Base::Renderer::VertexArray& leveltris, Base::Renderer& renderer = *dynamic_cast<Base::Renderer*>(GetRenderer(false))) {
            GetWorldHandler(false)->render(level, leveltris, renderer, offset);
        }
    };
}  // namespace Game
