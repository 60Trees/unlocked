#pragma once
#include <sys/types.h>
#include <LDtkLoader/Project.hpp>
#include <base/renderer.hpp>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <map>
#include "glm/detail/qualifier.hpp"
namespace Game {
    struct WorldHandler;
}
GETTER_DEFINITION(Game::WorldHandler, GetWorldHandler);
namespace Game {
    struct TileMap {
        enum CollisionType : uint8_t {
            AIR = 0,
            SOLID = 1,
            WATER = 2,
            LADDER = 3,
            LAVA = 4,
        };
        std::function<CollisionType(int)> intgridval_to_collision_type = [](int x) { return static_cast<CollisionType>(x); };
        glm::vec<2, int> offset = {0, 0};
        glm::vec<2, uint> size;
        double scale = 1.0;
        void write(int data, glm::vec<2, uint> pos);
        int get(glm::vec<2, uint> pos);
        CollisionType getcollision(glm::vec<2, uint> pos) { return intgridval_to_collision_type(get(pos)); }
        void fix_map_size() {
            tilemap.resize(size.x);
            for (auto& column : tilemap) column.resize(size.y);
        }
        inline int operator[](size_t x, size_t y) { return get({x, y}); }
        inline int operator[](glm::vec<2, uint> pos) { return get(pos); }
        std::vector<std::vector<int>> tilemap;
    };

    struct PlacedLevel;
    struct WorldHandler {
        virtual ~WorldHandler() = default;
        ldtk::Project main_world;
        virtual void loadFromMemory(std::span<const unsigned char> bytes) = 0;
        std::map<const ldtk::Level*, TileMap> all_level_tilemaps;
        std::vector<PlacedLevel> placed_levels;
        virtual void render(const ldtk::Level& level, Base::Renderer::VertexArray& leveltris,
            Base::Renderer& renderer = *dynamic_cast<Base::Renderer*>(GetRenderer(false)), glm::vec<2, int> offset = {0, 0}) = 0;
        virtual void uploadAllTilesets(Base::Renderer& r, bool logs = false) = 0;

        // Sets a single IntGrid cell on the level's collision layer, records it in
        // that level's EditsMap, and regenerates the collision layer's auto-tiles.
        // The level must have been rendered at least once already.
        virtual void setTile(const ldtk::Level& level, glm::vec<2, uint> pos, int value) = 0;

        // Flood-fills (4-directional) connected IntGrid cells starting at `pos` on
        // the level's collision layer, replacing the region matching the starting
        // cell's value with `value`, recording every touched cell in EditsMap, then
        // regenerates that layer's auto-tiles.
        virtual void floodFillTile(const ldtk::Level& level, glm::vec<2, uint> pos, int value) = 0;

        // Renders every level in `placed_levels` that is "dirty" -- i.e. hasn't been
        // rendered yet, or was marked dirty by setTile/floodFillTile since its last render.
        virtual void renderDirtyLevels(
            Base::Renderer::VertexArray& leveltris, Base::Renderer& renderer = *dynamic_cast<Base::Renderer*>(GetRenderer(false))) = 0;

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
