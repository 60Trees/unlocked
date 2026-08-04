#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "base.hpp"
#include <utils.hpp>

extern "C" Base::BaseClass* GetRenderer();
namespace Base {
    struct Renderer : BaseClass {
        virtual ushort addTextureFromBytes(std::string_view name, std::span<const char> bytes) = 0;
        virtual ushort getTextureID(std::string_view name) = 0;

        struct Vertex {
            union PosType {
                struct WorldSpace {
                    float depth;
                    float x, y;
                } world;
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
                } screen;
                char raw[12];
            } pos;

            union ShaderData {
                struct RGBA {
                    uint8_t r, g, b, a;
                } rgba;
                uint32_t rgba_combined;
                struct TextureDesc {
                    uint16_t texture_id;
                    uint16_t u, v;
                } texture;
                char raw[6];
            } shaderdata;
        };

        // a pixel_shader that assumes a vertex uses shaderdata.rgba and coloures accordingly
        virtual std::string_view builtin_coloured_pshader() = 0;
        // a pixel_shader that assumes a vertex uses shaderdata.texture and shades accordingly
        virtual std::string_view builtin_textured_pshader() = 0;

        // a vertex_shader that assumes a vertex uses pos.world and sets it up accordingly
        virtual std::string_view builtin_worldspace_vshader() = 0;
        // a vertex_shader that assumes a vertex uses pos.screen and sets it up accordingly
        virtual std::string_view builtin_uispace_vshader() = 0;

        struct Material {
            std::string_view vertex_shader;
            std::string_view pixel_shader;

            enum BlendMode {
                Opaque,
                Alpha,
                Additive,
                Multiply,
                Screen,
            } blend_mode;

            bool screenspace = false;
            std::span<const std::byte> params;
        };

        struct PostEffect {
            std::string_view pixel_shader;
            std::span<const std::byte> params;
            bool enabled = true;
        };
        std::vector<PostEffect> post_queue{};

        struct Camera {
            float x = 0, y = 0;
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
        virtual void compile_all_shaders() = 0;

        struct VertexList {
            std::span<const Vertex> vertices;
            Material material;
        };

        /// These will be ordered first -> frontmost
        std::vector<VertexList> render_queue{};

        inline static std::unique_ptr<Renderer> get() {
            std::unique_ptr<Renderer> retval = nullptr;
            retval.reset(dynamic_cast<Renderer*>(GetRenderer()));
            return retval;
        };
    };
}  // namespace Base
