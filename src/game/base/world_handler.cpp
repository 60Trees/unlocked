#include "world_handler.hpp"

#include "LDtkLoader/DataTypes.hpp"
#include "LDtkLoader/thirdparty/json_fwd.hpp"
#include "fs_utils.hpp"
#include "glm/detail/qualifier.hpp"
#include <sstream>
#include <string>
#include <nlohmann/json.hpp>

using namespace std;
using namespace Base;

struct WorldHandlerImpl : Game::WorldHandler {
    struct TrisIndex {
        size_t start_pos, end_pos;
    };
    map<const ldtk::Level*, TrisIndex> owned_triangles{};

    void remove_rendered(const ldtk::Level& level, Renderer::VertexArray& leveltris) {
        const auto it = owned_triangles.find(&level);

        if (it == owned_triangles.end()) return;

        const auto [start_pos, end_pos] = it->second;
        const size_t removed = end_pos - start_pos;

        leveltris.erase(leveltris.begin() + start_pos, leveltris.begin() + end_pos);

        for (auto& [lvl, range] : owned_triangles) {
            if (range.start_pos >= end_pos) {
                range.start_pos -= removed;
                range.end_pos -= removed;
            }
        }

        owned_triangles.erase(it);
    }

    void render(
        const ldtk::Level& level, Base::Renderer::VertexArray& leveltris, Base::Renderer& renderer, glm::vec<2, int> offset) override {
        collisions[&level] = {};
        Game::CollisionMap& lvlCollision = collisions[&level];
        lvlCollision.offset = offset;

        remove_rendered(level, leveltris);
        auto& cur_owned_triangles = owned_triangles[&level];
        cur_owned_triangles.start_pos = leveltris.size();

        istringstream stream(level.getField<string>("CollisionType").value());
        nlohmann::json collision_json = nlohmann::json::parse(stream);

        size_t leveltris_i = 0;
        for (auto& layer : level.allLayers()) {
            if (!layer.hasTileset()) continue;

            bool is_collision_layer = false;
            // print("Collision JSON: {}\n", nlohmann::to_string(collision_json));
            if (layer.getName() == collision_json["target_layer"].get<string>()) {
                is_collision_layer = true;
                lvlCollision.scale = layer.getCellSize();
                lvlCollision.size = {layer.getGridSize().x, layer.getGridSize().y};
                lvlCollision.fix_map_size();

                const auto gridsize = layer.getGridSize();
                for (uint x = 0; x < layer.getGridSize().x; x++)
                    for (uint y = 0; y < layer.getGridSize().y; y++) {
                        const auto tile = layer.getIntGridVal(x, y);
                        const auto tileid = tile.value;
                        const auto tileidstr = std::to_string(tileid);

                        const auto currentCollisionVal =
                            collision_json.contains(tileidstr) ? std::stoi(collision_json[tileidstr].get<string>()) : tileid;
                        lvlCollision.map[x][y] = static_cast<Game::CollisionMap::CollisionType>(currentCollisionVal);
                    }
            };

            if (leveltris.size() <= leveltris_i) leveltris.push_back({});
            auto& tris = leveltris.back();

            tris.material.pixel_shader = renderer.builtin_textured_pshader();
            tris.material.vertex_shader = renderer.builtin_worldspace_vshader();

            auto tileset = layer.getTileset();
            auto atlas_id = renderer.getTextureID("assets/" + tileset.path);

            if (atlas_id == 0) print("Atlas ID for {} is invalid!\n", tileset.path);

            struct IntPoint {
                int x, y;
                IntPoint(const ldtk::IntPoint& o) : x(o.x), y(o.y) {}
                IntPoint(IntPoint&&) = default;
                IntPoint(const IntPoint& o) : x(o.x), y(o.y) {}
                auto operator<=>(const IntPoint&) const = default;
                auto operator<=>(const ldtk::IntPoint& o) const { return this->operator<=>(IntPoint{o}); }
            };

            for (auto& tile : layer.allTiles()) {
                auto tilepos = tile.getPosition();
                // TODO: Fix storing the collision data into a tilemap

                // const auto collisionmap_size = ldtk::Point<int>{(int)collisionmap.map.size(), (int)collisionmap.map[0].size()};
                // if (collision_json.contains(std::to_string(tile.tileId)))
                //     collisionmap.map[tilepos.x][tilepos.y] = static_cast<Game::CollisionMap::CollisionType>(
                //         std::stoul(collision_json[std::to_string(tile.tileId)].get<string>()));
                // else if (tilepos.x < collisionmap_size.x && tilepos.y < collisionmap_size.y)
                //     collisionmap.map[tilepos.x][tilepos.y] = static_cast<Game::CollisionMap::CollisionType>(tile.tileId);
                // else {
                //     DEBUG_BREAK();
                // }

                //-> -->>> // :::::::
                tilepos.x += offset.x;
                tilepos.y += offset.y;

                const auto tilesize = layer.getCellSize();
                const glm::vec<2, int> visual_offset = {0, -tilesize};

                const float scaloid = 1.0f;
                const auto texturerect = tile.getTextureRect();

                Base::Renderer::TexturedRectDescriptor rect{
                    .pos = {(float)tilepos.x + visual_offset.x, (float)-tilepos.y * scaloid + visual_offset.y,
                        ((float)tilepos.x + tilesize) * scaloid + visual_offset.x,
                        -((float)tilepos.y - tilesize) * scaloid + visual_offset.y},
                    .uv = {(ushort)texturerect.x, (ushort)(texturerect.y + texturerect.height), (ushort)(texturerect.x + texturerect.width),
                        (ushort)texturerect.y},
                    .atlas_index = atlas_id,
                };

                if (tile.flipX) swap(rect.uv.l, rect.uv.r);
                if (tile.flipY) swap(rect.uv.t, rect.uv.b);

                Renderer::make_textured_square(rect, tris.vertices);
            }
            leveltris_i++;
        }
        cur_owned_triangles.end_pos = leveltris.size() - 1;
    }

    void uploadAllTilesets(Renderer& r, bool logs) override {
        for (auto& tileset : main_world.allTilesets()) {
            try {
                string _path = "assets/" + tileset.path;
                if (tileset.path.empty()) continue;
                if (logs) print("Tileset\n- {}\n", _path);
                if (!fs.exists(_path)) {
                    if (logs) print("- (INVALID PATH)\n");
                    continue;
                }
                r.addTextureFromBytes(_path, fs_helper::get_bytes_from_file<char>(_path));
                if (logs) print("- (id={})\n", r.getTextureID(_path));
            } catch (std::system_error) {
                if (logs) print("- (INVALID PATH)\n");
                continue;
            }
        }
    }
};

GETTER_IMPL(Game::WorldHandler, GetWorldHandler, WorldHandlerImpl);
