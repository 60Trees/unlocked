#include "world_handler.hpp"

#include "LDtkLoader/DataTypes.hpp"
#include "LDtkLoader/thirdparty/json_fwd.hpp"
#include "fs_utils.hpp"
#include "glm/detail/qualifier.hpp"
#include <sstream>
#include <string>
#include <set>
#include <nlohmann/json.hpp>

// TODO: (Not urgent, doesnt need to be fixed) Fix ldtkimport
#define use_ldtkimport false

#if use_ldtkimport
#    include "ldtkimport/LdtkDefFile.h"
#    include "ldtkimport/Level.h"
#endif

using namespace std;
using namespace Base;
#if use_ldtkimport
using namespace ldtkimport::RunSettings;
#endif

struct WorldHandlerImpl : Game::WorldHandler {
    struct TrisIndex {
        size_t start_pos, end_pos;
    };
    map<const ldtk::Level*, TrisIndex> owned_triangles{};
    set<const ldtk::Level*> rendered_levels{};

#if use_ldtkimport
    ldtkimport::LdtkDefFile ldtkimport_file;
    bool ldtkimport_file_loaded = false;
    map<const ldtk::Level*, ldtkimport::Level> ldtkimport_levels{};
#endif

    void loadFromMemory(span<const unsigned char> bytes) override {
        main_world.loadFromMemory(bytes.data(), bytes.size());

#if use_ldtkimport
        // ldtkimport can load straight from the in-memory json text, no temp file needed.
        ldtkimport_file.loadFromText(reinterpret_cast<const char*>(bytes.data()), bytes.size(), false, "world_handler_collision_rules");
        ldtkimport_file_loaded = ldtkimport_file.isValid();

        if (!ldtkimport_file_loaded) {
            print("Failed to load ldtkimport from memory\n");
        }
#endif
    }

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

#if use_ldtkimport
    void sync_tilemap_rules(const ldtk::Level& level, Game::TileMap& lvlTileMap) {
        if (!ldtkimport_file_loaded) return;

        auto& ii_level = ldtkimport_levels[&level];

        const auto w = lvlTileMap.size.x;
        const auto h = lvlTileMap.size.y;

        // Push current tilemap into ldtkimport
        std::vector<unsigned short> flat(static_cast<size_t>(w) * static_cast<size_t>(h));
        for (uint y = 0; y < h; y++)
            for (uint x = 0; x < w; x++) flat[y * w + x] = static_cast<int>(lvlTileMap.tilemap[x][y]);

        // findings: IMPORTANT - Over here (before setIntGrid is run) flat is full of numbers
        // and ii_level.m_tileGrids is empty
        std::print("Before\n");
        ii_level.setIntGrid((uint16_t)w, (uint16_t)h, std::move(flat));
        std::print("After\n");
        // over here, ii_level.m_tileGrids has 0 entries?!
        ldtkimport_file.runRules(ii_level);
        // over here, ii_level.m_tileGrids has 2 entries?!
        std::print("After after\n");
        /*
dap> ? ii_level.m_tileGrids
size=2
  [0]: {m_layerUid:10, m_randomSeed:9956981, ...}
  [1]: {m_layerUid:91, m_randomSeed:3705503, ...}
  [raw]: std::vector<ldtkimport::TileGrid, std::allocator<ldtkimport::TileGrid> >
dap> ? ii_level.m_tileGrids[0]
{m_layerUid:10, m_randomSeed:9956981, ...}
  m_grid: size=5852
  m_height: 38
  m_layerUid: 10
  m_randomSeed: 9956981
  m_width: 154
dap> ? ii_level.m_tileGrids[1]
{m_layerUid:91, m_randomSeed:3705503, ...}
  m_grid: size=5852
  m_height: 38
  m_layerUid: 91
  m_randomSeed: 3705503
  m_width: 154
  // both of the `m_grid`s are filled with all 0s
*/

        // Pull the (post-rules) IntGrid back into TileMap.tilemap
        const auto& intGrid = ii_level.getIntGrid();  // adjust to actual API
        for (uint y = 0; y < h; y++)
            for (uint x = 0; x < w; x++) lvlTileMap.tilemap[x][y] = intGrid(x, y);
    }
#endif

    void setTile(const ldtk::Level& level, glm::vec<2, uint> pos, int value) override {
        auto it = all_level_tilemaps.find(&level);
        if (it == all_level_tilemaps.end()) return;  // level must be rendered/placed at least once first

        auto& lvlTilemap = it->second;
        if (pos.x >= lvlTilemap.size.x || pos.y >= lvlTilemap.size.y) return;

        lvlTilemap.tilemap[pos.x][pos.y] = value;

#if use_ldtkimport
        sync_tilemap_rules(level, lvlTilemap);
        rendered_levels.erase(&level);  // mark dirty
#endif
    }

    void floodFillTile(const ldtk::Level& level, glm::vec<2, uint> pos, int value) override {
        auto it = all_level_tilemaps.find(&level);
        if (it == all_level_tilemaps.end()) return;

        auto& lvlMap = it->second;
        if (pos.x >= lvlMap.size.x || pos.y >= lvlMap.size.y) return;

        const auto target = lvlMap.tilemap[pos.x][pos.y];
        if (target == value) return;

        vector<glm::vec<2, uint>> stack{pos};
        while (!stack.empty()) {
            auto cur = stack.back();
            stack.pop_back();

            if (cur.x >= lvlMap.size.x || cur.y >= lvlMap.size.y) continue;
            if (lvlMap.tilemap[cur.x][cur.y] != target) continue;

            lvlMap.tilemap[cur.x][cur.y] = value;

            if (cur.x > 0) stack.push_back({cur.x - 1, cur.y});
            if (cur.x + 1 < lvlMap.size.x) stack.push_back({cur.x + 1, cur.y});
            if (cur.y > 0) stack.push_back({cur.x, cur.y - 1});
            if (cur.y + 1 < lvlMap.size.y) stack.push_back({cur.x, cur.y + 1});
        }

#if use_ldtkimport
        sync_tilemap_rules(level, lvlMap);
        rendered_levels.erase(&level);  // mark dirty
#endif
    }

    void renderDirtyLevels(Base::Renderer::VertexArray& leveltris, Base::Renderer& renderer) override {
        for (auto& placed : placed_levels) {
            if (rendered_levels.count(&placed.level) == 0) {
                print("Re-rendering level {}\n", placed.level.name);
                render(placed.level, leveltris, renderer, placed.offset);
            }
        }
    }

    void render(
        const ldtk::Level& level, Base::Renderer::VertexArray& leveltris, Base::Renderer& renderer, glm::vec<2, int> offset) override {
        Game::TileMap& lvlTileMap = all_level_tilemaps[&level];  // default-constructs on first render
        lvlTileMap.offset = offset;

        remove_rendered(level, leveltris);
        auto& cur_owned_triangles = owned_triangles[&level];
        cur_owned_triangles.start_pos = leveltris.size();

        istringstream stream(level.getField<string>("CollisionType").value());
        nlohmann::json collision_json = nlohmann::json::parse(stream);

        size_t leveltris_i = 0;
        for (auto& layer : level.allLayers()) {
            if (!layer.hasTileset()) continue;

            bool is_collision_layer = false;
            if (layer.getName() == collision_json["target_layer"].get<string>()) {
                is_collision_layer = true;
                lvlTileMap.scale = layer.getCellSize();
                lvlTileMap.size = {layer.getGridSize().x, layer.getGridSize().y};

                const bool needs_bake = lvlTileMap.tilemap.empty();  // check BEFORE fix_map_size touches it
                lvlTileMap.fix_map_size();

                if (needs_bake) {
                    // std::set<int> seen;
                    // for (uint x = 0; x < layer.getGridSize().x; x++)
                    //     for (uint y = 0; y < layer.getGridSize().y; y++) seen.insert(layer.getIntGridVal(x, y).value);
                    // print("Raw IntGrid values in '{}': ", layer.getName());
                    // for (auto v : seen) print("{} ", v);
                    // print("\n");

                    const auto normalize_vals = [&](int val) {
                        if (val < 0) return 0;
                        if (val > 10) return 0;
                        return val;
                    };

                    for (uint x = 0; x < layer.getGridSize().x; x++)
                        for (uint y = 0; y < layer.getGridSize().y; y++) {
                            lvlTileMap.tilemap[x][y] = normalize_vals(layer.getIntGridVal(x, y).value);  // keep RAW value
                        }
                }

                // Translate raw IntGrid value -> gameplay CollisionType lazily, on read only.
                // (collision_json captured by value so this stays valid after render() returns)
                lvlTileMap.intgridval_to_collision_type = [collision_json](int raw) -> Game::TileMap::CollisionType {
                    const auto key = std::to_string(raw);
                    if (collision_json.contains(key))
                        return static_cast<Game::TileMap::CollisionType>(std::stoi(collision_json[key].get<string>()));
                    return static_cast<Game::TileMap::CollisionType>(raw);
                };

#if use_ldtkimport
                sync_tilemap_rules(level, lvlTileMap);
#endif
            };

#if use_ldtkimport
            print("Layer '{}': collision_target='{}', is_collision_layer={}, ldtkimport_loaded={}\n", layer.getName(),
                collision_json["target_layer"].get<string>(), is_collision_layer, ldtkimport_file_loaded);

            if (is_collision_layer && ldtkimport_file_loaded) {
                const ldtkimport::Layer* ii_layer = nullptr;
                for (auto lyr = ldtkimport_file.layerCBegin(), lyrEnd = ldtkimport_file.layerCEnd(); lyr != lyrEnd; ++lyr) {
                    print("  ldtkimport layer candidate: '{}'\n", lyr->name);
                    if (lyr->name == layer.getName()) {
                        ii_layer = &(*lyr);
                        print("  -> matched ldtkimport layer '{}'\n", lyr->name);
                        break;
                    }
                }

                if (!ii_layer) {
                    print("  !! no matching ldtkimport layer for '{}'\n", layer.getName());
                } else {
                    auto* ii_tileset = ldtkimport_file.getTileset(ii_layer->tilesetDefUid);
                    if (!ii_tileset) {
                        print("  !! no tileset for tilesetDefUid={}\n", ii_layer->tilesetDefUid);
                    } else {
                        print("  -> ldtkimport tileset imagePath='{}'\n", ii_tileset->imagePath);
                    }
                }
            }
#endif

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

            // Shared "emit one textured quad" logic, reused by both the normal
            // LDtkLoader-baked path and the ldtkimport-generated collision-layer path.
            auto push_tile_quad = [&](glm::vec<2, int> tilepos, int cell_px, int rectL, int rectT, int rectW, int rectH, bool flipX,
                                      bool flipY, uint32_t atlas_index) {
                const glm::vec<2, int> visual_offset = {0, -cell_px};
                constexpr float scaloid = 1.0f;

                Base::Renderer::TexturedRectDescriptor rect{
                    .pos = {(float)tilepos.x + visual_offset.x, (float)-tilepos.y * scaloid + visual_offset.y,
                        ((float)tilepos.x + cell_px) * scaloid + visual_offset.x,
                        -((float)tilepos.y - cell_px) * scaloid + visual_offset.y},
                    .uv = {(ushort)rectL, (ushort)(rectT + rectH), (ushort)(rectL + rectW), (ushort)rectT},
                    .atlas_index = (ushort)atlas_index,
                };

                if (flipX) swap(rect.uv.l, rect.uv.r);
                if (flipY) swap(rect.uv.t, rect.uv.b);

                Renderer::make_textured_square(rect, tris.vertices);
            };

#if use_ldtkimport
            bool drew_via_ldtkimport = false;
            // TODO: Fix this if block. I will call it `ldtkimport renderer`
            _disabled if (is_collision_layer && ldtkimport_file_loaded) {
                // Resolve the matching layer/tileset in the ldtkimport-side file by name.
                const ldtkimport::Layer* ii_layer = nullptr;
                for (auto lyr = ldtkimport_file.layerCBegin(), lyrEnd = ldtkimport_file.layerCEnd(); lyr != lyrEnd; ++lyr) {
                    if (lyr->name == layer.getName()) {
                        ii_layer = &(*lyr);
                        break;
                    }
                }

                const ldtkimport::TileSet* ii_tileset = nullptr;
                if (ii_layer != nullptr) {
                    ii_tileset = ldtkimport_file.getTileset(ii_layer->tilesetDefUid);
                }

                if (is_collision_layer && ldtkimport_file_loaded) {
                    const ldtkimport::Layer* ii_layer = nullptr;
                    // findings: this successfully gets the right layer
                    // but could use uid instead potentially
                    for (auto lyr = ldtkimport_file.layerCBegin(), lyrEnd = ldtkimport_file.layerCEnd(); lyr != lyrEnd; ++lyr) {
                        if (lyr->name == layer.getName()) {
                            ii_layer = &(*lyr);
                            break;
                        }
                    }

                    // findings: the ii_tileset has the correct name according to debugger
                    const ldtkimport::TileSet* ii_tileset = nullptr;
                    if (ii_layer != nullptr) {
                        ii_tileset = ldtkimport_file.getTileset(ii_layer->tilesetDefUid);
                    }

                    if (ii_layer != nullptr && ii_tileset != nullptr) {
                        auto collision_atlas_id = renderer.getTextureID("assets/" + ii_tileset->imagePath);
                        const int cellPx = ii_layer->cellPixelSize;
                        const float halfCell = cellPx * 0.5f;

                        // findings: ii_level is valid with debugger data {m_tileGrids:size=2}
                        ldtkimport::Level& ii_level = ldtkimport_levels[&level];

                        const ldtkimport::TileGrid* tileGrid = nullptr;
                        // findings: in the debugger, tileGrid is valid after this block
                        // and is as expected
                        // the uid is global across all ldtk loaders and not impl specific
                        for (int i = 0, n = (int)ii_level.getTileGridCount(); i < n; ++i) {
                            const auto& candidate = ii_level.getTileGridByIdx(i);
                            if (candidate.getLayerUid() == ii_layer->uid) {
                                tileGrid = &candidate;
                                break;
                            }
                        }

                        // findings: it's not nullptr, it has a proper random seed / m_layerUid
                        if (tileGrid == nullptr) {
                            // this block doesn't run
                            print("  !! no ldtkimport TileGrid found for layer '{}' (uid={})\n", layer.getName(), ii_layer->uid);
                        } else {
                            // findings: it got the grid size correct
                            const auto grid = layer.getGridSize();

                            for (uint gy = 0; gy < grid.y; gy++) {
                                for (uint gx = 0; gx < grid.x; gx++) {
                                    // findings: cellTiles is always empty!!!!!! Why???
                                    // in the debugger i dumped `tileGrid`'s internal vector and
                                    // it's ALLLL std vectors with `size=0`
                                    // the level width = 154 and height = 38
                                    // 154 * 38 = 5852
                                    // there are 5852 entries in `tileGrid`'s internal vector
                                    // and they are all std vectors with size=0
                                    // it is a std::vector<tiles_t> (aka std::vector<std::vector<TileInCell>>)
                                    const auto& cellTiles = (*tileGrid)(gx, gy);

                                    // as a result, this for loop doesn't run -> no tiles rendered!
                                    for (auto t = cellTiles.crbegin(), tEnd = cellTiles.crend(); t != tEnd; ++t) {
                                        // dont know whats in here since (MAIN PROBLEM) cellTiles is always empty

                                        int16_t tsX, tsY;
                                        ii_tileset->getCoordinates(t->tileId, tsX, tsY);

                                        glm::vec<2, int> tilepos{(int)(gx * cellPx + t->getOffsetX(halfCell)) + offset.x,
                                            (int)(gy * cellPx + t->getOffsetY(halfCell)) + offset.y};

                                        push_tile_quad(tilepos, cellPx, tsX * cellPx, tsY * cellPx, cellPx, cellPx, t->isFlippedX(),
                                            t->isFlippedY(), collision_atlas_id);
                                    }
                                }
                            }

                            drew_via_ldtkimport = true;
                        }
                    }
                }
            }
            if (!drew_via_ldtkimport) {
#else
            {
#endif

                std::print("Rendering statically\n");
                // Original path: bake straight from LDtkLoader's static tile data.
                // Used for every non-collision layer, and as a fallback if the
                // ldtkimport layer/tileset couldn't be resolved above.
                for (auto& tile : layer.allTiles()) {
                    auto tilepos = tile.getPosition();
                    tilepos.x += offset.x;
                    tilepos.y += offset.y;

                    const auto tilesize = layer.getCellSize();
                    const auto texturerect = tile.getTextureRect();

                    push_tile_quad({tilepos.x, tilepos.y}, tilesize, texturerect.x, texturerect.y, texturerect.width, texturerect.height,
                        tile.flipX, tile.flipY, atlas_id);
                }
            }

            leveltris_i++;
        }
        cur_owned_triangles.end_pos = leveltris.size();
        rendered_levels.insert(&level);
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

#if use_ldtkimport
        // Also upload whatever tileset(s) the ldtkimport collision-rules file
        // references, since the collision layer is now drawn from that atlas.
        if (ldtkimport_file_loaded) {
            for (auto tileset = ldtkimport_file.tilesetCBegin(), end = ldtkimport_file.tilesetCEnd(); tileset != end; ++tileset) {
                try {
                    if (tileset->imagePath.empty()) continue;
                    string _path = "assets/" + tileset->imagePath;
                    if (logs) print("Tileset (collision rules)\n- {}\n", _path);
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
#endif
    }
};

GETTER_IMPL(Game::WorldHandler, GetWorldHandler, WorldHandlerImpl);
