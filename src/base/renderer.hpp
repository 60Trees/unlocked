/**
 * @file src/base/renderer.hpp
 * @author 60Trees_ (github.com/60Trees)
 */

#pragma once

#include <sys/types.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <print>
#include <span>
#include <string_view>
#include <vector>
#include <glm/vec2.hpp>

#include "SDL3/SDL_mouse.h"
#include "base.hpp"
#include "glm/common.hpp"
#include <utils.hpp>

GETTER_DEFINITION(Base::BaseClass, GetRenderer);
#define GETTER_CLASS_DEFINITION(type)                     \
    inline static std::unique_ptr<type> get() {           \
        std::unique_ptr<type> retval = nullptr;           \
        retval.reset(dynamic_cast<type*>(::Get##type())); \
        return retval;                                    \
    }

namespace Base {
    struct Renderer : BaseClass {
        GETTER_CLASS_DEFINITION(Renderer);
        // <AI>
        enum class PixelFormat : uint8_t { R8Unorm, RGBA8Unorm };

        // Upload raw CPU pixel data as a texture — no image decode. Useful for masks, LUTs,
        // baked data, noise textures, minimaps... not just this feature.
        virtual ushort createTexture(uint32_t width, uint32_t height, PixelFormat format, std::span<const std::byte> pixels) = 0;

        // Patch a sub-rect of a texture created via createTexture/addTextureFromBytes.
        virtual void updateTextureRegion(
            ushort id, uint32_t x, uint32_t y, uint32_t width, uint32_t height, PixelFormat format, std::span<const std::byte> pixels) = 0;

        // Lets game code do screen<->world math for screen-space effects without duplicating
        // renderer-internal state.
        virtual glm::vec<2, uint32_t> get_viewport_size() const = 0;
        // </AI>

        struct CameraBound {
            double x, y, w, h;

            // zoom out as much as possible ?
            bool lock_zoom = false;

            bool snap_in_bounds = false;
            bool snappy = false;

            // none = center on player, multiple = center at closest one to player
            std::vector<glm::vec<2, double>> center_at{};

            float zoom_level = -1;

            // ignored by renderer
            float priority = 0;
            inline auto operator<=>(const CameraBound& oth) { return priority <=> oth.priority; }
        };

        const CameraBound* camera_bound = nullptr;

        virtual ushort addTextureFromBytes(std::string_view name, std::span<const char> bytes) = 0;
        virtual ushort getTextureID(std::string_view name) = 0;
        virtual glm::vec<2, int> get_screen_size() const = 0;

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
            static constexpr int kMaxPostExtraTextures = 4;
            std::span<const ushort> extra_textures = {};
            bool enabled = true;
            std::function<void(const Renderer&)> pre_render = nullptr;
        };
        std::vector<PostEffect> post_queue{};

        struct Camera {
            float x = 0, y = 0;
            float target_x = 0, target_y = 0;

            // TODO: Add screen shaking
            float screenshake = 0.0;

            void follow_point(glm::vec<2, double> point);

            void update_camera(double dt);

            double zoom = 160, _target_zoom = 0, _real_zoom = 0, zoom_speed = 15, zoom_snap_distance = 0.01;

            inline double get_raw_zoom(const Renderer& r) const {
                auto minscreen = glm::min(r.get_screen_size().x, r.get_screen_size().y);
                return minscreen / _target_zoom;
            }

            void update_zoom(double dt);
        } camera;

        inline double get_raw_zoom() const { return camera.get_raw_zoom(*this); }
        inline glm::vec2 get_world_mouse_pos() const {
            glm::vec2 mousepos = {};

            SDL_GetMouseState(&mousepos.x, &mousepos.y);
            auto campos = glm::vec2{camera.x, camera.y};
            auto screenCenter = glm::vec2{get_screen_size()} / 2.f;

            return (mousepos - screenCenter) * glm::vec2{1.f, -1.f} / (float)get_raw_zoom() + campos;
        }

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

        /// These will be ordered first -> frontmost
        std::function<RenderQueue&()> renderqueue = [] -> RenderQueue& {
            static RenderQueue render_queue{};
            return render_queue;
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
