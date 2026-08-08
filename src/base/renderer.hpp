#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <print>
#include <span>
#include <string_view>
#include <vector>
#include <glm/vec2.hpp>

#include "base.hpp"
#include <utils.hpp>

extern "C" Base::BaseClass* GetRenderer();
namespace Base {
    struct Renderer : BaseClass {
        virtual ushort addTextureFromBytes(std::string_view name, std::span<const char> bytes) = 0;
        virtual ushort getTextureID(std::string_view name) = 0;

        struct ScreenSpace {
            short x, y;
            enum AnchorPoint : char {
                TOP_LEFT = 0x1a,
                TOP_MID = 0x2a,
                TOP_RIGHT = 0x3a,
                LEFT = 0x1b,
                MID = 0x2b,
                RIGHT = 0x3b,
                BOTTOM_LEFT = 0x1c,
                BOTTOM_MID = 0x2c,
                BOTTOM_RIGHT = 0x3c,
            } anchor_point;
        };
        struct WorldSpace {
            float depth;
            float x, y;
        };
        struct RGBA {
            uint8_t r, g, b, a;
        };
        struct TextureDesc {
            uint16_t texture_id;
            uint16_t u, v;
        };
        struct Vertex {
            union PosType {
                char raw[mth::max(sizeof(WorldSpace), sizeof(ScreenSpace))];
                WorldSpace world;
                ScreenSpace screen;
            } pos;

            union ShaderData {
                RGBA rgba;
                uint32_t rgba_combined;
                TextureDesc texture;
                char raw[mth::max(sizeof(RGBA), sizeof(TextureDesc))];
            } shaderdata;
        };

        // a pixel_shader that assumes a vertex uses shaderdata.rgba and coloures accordingly
        virtual std::string_view builtin_coloured_pshader() const = 0;
        // a pixel_shader that assumes a vertex uses shaderdata.texture and shades accordingly
        virtual std::string_view builtin_textured_pshader() const = 0;

        // a vertex_shader that assumes a vertex uses pos.world and sets it up accordingly
        virtual std::string_view builtin_worldspace_vshader() const = 0;
        // a vertex_shader that assumes a vertex uses pos.screen and sets it up accordingly
        virtual std::string_view builtin_uispace_vshader() const = 0;

        enum BlendMode {
            Opaque,
            Alpha,
            Additive,
            Multiply,
            Screen,
        };
        struct Material {
            std::string_view vertex_shader;
            std::string_view pixel_shader;

            BlendMode blend_mode = Alpha;

            bool screenspace = false;
            std::span<const std::byte> params = {};
        };

        virtual void compile_default_shaders() = 0;
        virtual void precompile_shaders(std::span<const std::string_view> pixel_shaders, std::span<const BlendMode> blend_modes,
            std::span<const std::string_view> vertex_shaders) = 0;

        struct PostEffect {
            std::string_view pixel_shader;
            std::span<const std::byte> params;
            bool enabled = true;
        };
        std::vector<PostEffect> post_queue{};

        struct Camera {
            float x = 0, y = 0;
            void follow_point(glm::vec<2, double> point, double dt) {
                // x += (point.x - x) / 2 * dt * camera_speed;
                // y += (point.y - y) / 2 * dt * camera_speed;
                // if (mth::abs(point.x - x) <= camera_snap_distance) x = point.x;
                // if (mth::abs(point.y - y) <= camera_snap_distance) y = point.y;
                x = point.x;
                y = point.y;
            }
            double zoom = 160, _real_zoom = 0, zoom_speed = 15, zoom_snap_distance = 0.01;
            void update_zoom(double dt) {
                _real_zoom += (zoom - _real_zoom) / 2 * dt * zoom_speed;
                if (mth::abs(zoom - _real_zoom) <= zoom_snap_distance) _real_zoom = zoom;
            }
        } camera;

        /**
         * @brief
         * Iterates over VertexList and compiles the shaders found in each material.
         *
         * @detail
         * It will cache a const char* as well as the size and if two string_views have the same internal pointer,
         * then no recompiling.
         * That way the pointer is the ID, instead of having another ID system for shaders as well as textures
         */
        virtual void compile_used_shaders() = 0;

        struct VertexLayer {
            std::vector<Vertex> vertices;
            Material material;
            using index_t = size_t;
        };
        using VertexArray = std::vector<VertexLayer>;

        struct RenderQueue {
            std::vector<std::shared_ptr<VertexArray>> raw;
            inline size_t size() const {
                size_t retval = 0;
                for (const auto& q : raw) retval += q->size();
                return retval;
            }
            inline const VertexLayer& operator[](size_t index) const {
                for (const auto q : raw) {
                    const auto size = q->size();
                    if (size <= index) {
                        index -= size;
                        continue;
                    }
                    return (*q)[index];
                }
                throw std::runtime_error("Index out of range!");
            }
            inline const VertexLayer& operator[](size_t index1, size_t index2) const { return raw[index1]->at(index2); }
            inline VertexLayer& operator[](size_t index1, size_t index2) { return raw[index1]->at(index2); }
            inline VertexLayer& operator[](size_t index) {
                for (auto q : raw) {
                    const auto size = q->size();
                    if (size <= index) {
                        index -= size;
                        continue;
                    }
                    return (*q)[index];
                }
                throw std::runtime_error("Index out of range!");
            }

            inline void append(std::shared_ptr<VertexArray> array) { raw.push_back(array); }

            // Disowns all VertexArrays. Their contents are not touched
            inline void clear() { raw.clear(); }
        };

        std::function<RenderQueue&()> renderqueue = [] -> RenderQueue& {
            static RenderQueue render_queue{};
            return render_queue;
        };

        /// These will be ordered first -> frontmost

        inline static std::unique_ptr<Renderer> get() {
            std::unique_ptr<Renderer> retval = nullptr;
            retval.reset(dynamic_cast<Renderer*>(GetRenderer()));
            return retval;
        };

        struct ColouredRectDescriptor {
            template <typename T>
            struct Rect {
                /// left, top, right, bottom
                T l, t, r, b;
            };
            template <typename T>
            struct RectCorners {
                T tl, tr, bl, br;
            };
            Rect<float> pos;
            RectCorners<uint32_t> colours;
            int8_t layer = 0;
        };
        struct TexturedRectDescriptor {
            template <typename T>
            struct Rect {
                /// left, top, right, bottom
                T l, t, r, b;
            };
            Rect<float> pos;
            Rect<uint16_t> uv;
            ushort atlas_index;
            int8_t layer = 0;
        };

        static void make_coloured_square(ColouredRectDescriptor rect, std::vector<Vertex>& verts) {
            std::array<Vertex, 6> tris;

            for (auto& tri : tris) {
                tri.pos.world.depth = rect.layer;
            }

            tris[0].pos.world.x = rect.pos.l;
            tris[0].pos.world.y = rect.pos.t;
            tris[0].shaderdata.rgba_combined = rect.colours.tl;

            tris[1].pos.world.x = rect.pos.l;
            tris[1].pos.world.y = rect.pos.b;
            tris[1].shaderdata.rgba_combined = rect.colours.bl;

            tris[2].pos.world.x = rect.pos.r;
            tris[2].pos.world.y = rect.pos.b;
            tris[2].shaderdata.rgba_combined = rect.colours.br;

            tris[3] = tris[0];

            tris[4] = tris[2];

            tris[5].pos.world.x = rect.pos.r;
            tris[5].pos.world.y = rect.pos.t;
            tris[5].shaderdata.rgba_combined = rect.colours.br;

            for (auto& t : tris) verts.push_back(std::move(t));
        }
        static void make_textured_square(TexturedRectDescriptor rect, std::vector<Vertex>& verts) {
            if (rect.atlas_index == 0) {
                std::print("atlasIndex is invalid!\n");
                return;
            }

            std::array<Vertex, 6> tris;

            for (auto& tri : tris) {
                tri.shaderdata.texture.texture_id = rect.atlas_index;
                tri.pos.world.depth = rect.layer;
            }

            tris[0].pos.world.x = rect.pos.l;
            tris[0].pos.world.y = rect.pos.t;
            tris[0].shaderdata.texture.u = rect.uv.l;
            tris[0].shaderdata.texture.v = rect.uv.t;

            tris[1].pos.world.x = rect.pos.l;
            tris[1].pos.world.y = rect.pos.b;
            tris[1].shaderdata.texture.u = rect.uv.l;
            tris[1].shaderdata.texture.v = rect.uv.b;

            tris[2].pos.world.x = rect.pos.r;
            tris[2].pos.world.y = rect.pos.b;
            tris[2].shaderdata.texture.u = rect.uv.r;
            tris[2].shaderdata.texture.v = rect.uv.b;

            tris[3] = tris[0];

            tris[4] = tris[2];

            tris[5].pos.world.x = rect.pos.r;
            tris[5].pos.world.y = rect.pos.t;
            tris[5].shaderdata.texture.u = rect.uv.r;
            tris[5].shaderdata.texture.v = rect.uv.t;

            for (auto& t : tris) verts.push_back(std::move(t));
        }
    };
}  // namespace Base
