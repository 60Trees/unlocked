#include "world_handler.hpp"

#include "LDtkLoader/DataTypes.hpp"
#include "LDtkLoader/thirdparty/json_fwd.hpp"
#include "fs_utils.hpp"
#include "glm/detail/qualifier.hpp"
#include "utils.hpp"
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

    void render(const ldtk::Level& level, Renderer& renderer, Renderer::VertexArray& leveltris, glm::vec<2, int> offset) override {
        collisions[&level] = {};
        Game::CollisionMap& collisionmap = collisions[&level];
        collisionmap.offset = offset;

        remove_rendered(level, leveltris);
        auto& cur_owned_triangles = owned_triangles[&level];
        cur_owned_triangles.start_pos = leveltris.size();

        // Json::Value collision_json;
        //{
        //     // <AI>
        //     Json::CharReaderBuilder builder;
        //     std::string errors;

        //    std::istringstream stream();

        //    if (!Json::parseFromStream(builder, stream, &collision_json, &errors)) {
        //        std::cerr << "JSON parse error: " << errors << '\n';
        //    }
        //    // </AI>
        //}

        istringstream stream(level.getField<string>("CollisionType").value());
        nlohmann::json collision_json = nlohmann::json::parse(stream);

        size_t leveltris_i = 0;
        for (auto& layer : level.allLayers()) {
            if (!layer.hasTileset()) continue;

            bool is_collision_layer = false;
            // print("Collision JSON: {}\n", nlohmann::to_string(collision_json));
            if (layer.getName() == collision_json["target_layer"].get<string>()) {
                is_collision_layer = true;
                collisionmap.scale = layer.getCellSize();
                collisionmap.size = {layer.getGridSize().x, layer.getGridSize().y};
                collisionmap.fix_map_size();
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

                tilepos.x += offset.x;
                tilepos.y += offset.y;

                auto tilesize = layer.getCellSize();
                float scaloid = 1.0f;
                auto texturerect = tile.getTextureRect();

                Base::Renderer::TexturedRectDescriptor rect{
                    .pos = {(float)tilepos.x, (float)-tilepos.y * scaloid, ((float)tilepos.x + tilesize) * scaloid,
                        ((float)-tilepos.y + tilesize) * scaloid},
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

    void uploadAllTilesets(Renderer& r, bool logs = true) override {
        logs = true;
        for (auto& tileset : main_world.allTilesets()) {
            try {
                string _path = "assets/" + tileset.path;
                if (logs) print("Tileset\n- {}\n", _path);

                r.addTextureFromBytes(_path, fs_helper::get_bytes_from_file<char>(_path));
                if (logs) print("- (id={})\n", r.getTextureID(_path));
            } catch (std::system_error) {
                if (logs) print("- (INVALID PATH)\n");
                continue;
            }
        }
    }
};

extern "C" Game::WorldHandler* GetWorldHandler() { return new WorldHandlerImpl(); }
