/**
 * @file src/game/systems/edge_glow.hpp
 */

#pragma once
#include <base/renderer.hpp>
#include <game/base/world_handler.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct EdgeGlowSystem {
    // World units == game pixels (1 tile = 8)
    float fade_start = 5.0f * 8.0f;   /// openings bigger than this start to dim...
    float fade_end = 10.0f * 8.0f;    /// ...and are completely dark at this size
    float reach = 14.0f;              /// how far the glow bleeds in from the screen edge
    float probe_offset = 4.0f;        /// how far outside the screen edge we look for air
    float intensity = 0.35f;
    glm::vec3 colour{0.75f, 0.90f, 1.0f};

    struct ParamsCPU {
        glm::vec4 cam_and_view;   // cam.xy, px_per_unit, -
        glm::vec4 viewport_glow;  // viewport.xy, reach, probe_offset
        glm::vec4 level_rect;     // level origin (bottom-left).xy, level size.xy
        glm::vec4 mask_info;      // tile_scale, mask tex width, mask tex height, intensity
        glm::vec4 colour;         // rgb, -
    } params{};

    static constexpr std::string_view kPixelShader = R"(
fn falloff(d: f32, reach: f32) -> f32 {
    let t = clamp(1.0 - d / reach, 0.0, 1.0);
    return t * t;
}

// Bilinear lookup of the baked opening factors at a world position.
//   x = opening size measured along a column (feeds the left/right screen edges)
//   y = opening size measured along a row    (feeds the top/bottom screen edges)
// The texture has a 1-texel zeroed border, so clamp-to-edge means "outside the level = not passable".
fn opening(world_pos: vec2f) -> vec2f {
    let origin = params[2].xy;
    let size = params[2].zw;
    let tile = params[3].x;
    let dims = params[3].yz;
    let texel = vec2f((world_pos.x - origin.x) / tile + 1.0, (origin.y + size.y - world_pos.y) / tile + 1.0);
    return textureSampleLevel(extraTex0, prevSamp, texel / dims, 0.0).rg;
}

@fragment fn fs_main(in: VSOut) -> @location(0) vec4f {
    let base = textureSample(prevTex, prevSamp, in.uv);
    let cam = params[0].xy;
    let ppu = params[0].z;
    if (ppu <= 0.0) { return base; }

    let half_view = params[1].xy / (2.0 * ppu);  // half the visible area, world units
    let reach = params[1].z;
    let probe = params[1].w;

    // this pixel as an offset from the screen centre, world units, +y up
    let rel = vec2f((in.uv.x * 2.0 - 1.0) * half_view.x, (1.0 - in.uv.y * 2.0) * half_view.y);
    let pos = cam + rel;

    // distance from this pixel to each screen edge
    let d_left = half_view.x + rel.x;
    let d_right = half_view.x - rel.x;
    let d_bottom = half_view.y + rel.y;
    let d_top = half_view.y - rel.y;

    // is there a (small enough) opening just outside each edge, at this pixel's position along it?
    let a_left = opening(vec2f(cam.x - half_view.x - probe, pos.y)).x;
    let a_right = opening(vec2f(cam.x + half_view.x + probe, pos.y)).x;
    let a_bottom = opening(vec2f(pos.x, cam.y - half_view.y - probe)).y;
    let a_top = opening(vec2f(pos.x, cam.y + half_view.y + probe)).y;

    let g_left = a_left * falloff(d_left, reach);
    let g_right = a_right * falloff(d_right, reach);
    let g_bottom = a_bottom * falloff(d_bottom, reach);
    let g_top = a_top * falloff(d_top, reach);

    // "Screen" union: never brighter than the brightest edge, so where two edges overlap (the corners)
    // the glows blend instead of adding up to a hot spot.
    let glow = 1.0 - (1.0 - g_left) * (1.0 - g_right) * (1.0 - g_bottom) * (1.0 - g_top);

    return vec4f(base.rgb + params[4].rgb * (glow * params[3].w), base.a);
}
)";

    ushort mask_id = 0;
    glm::vec<2, uint32_t> mask_dims{0, 0};  // includes the 1-texel border
    glm::vec2 world_origin{0, 0};           // bottom-left of the level
    glm::vec2 world_size{0, 0};
    float tile_scale = 8.0f;

    void bake_level(Base::Renderer& r, const Game::TileMap& tm) {
        const uint32_t w = tm.size.x, h = tm.size.y;
        if (w == 0 || h == 0) return;

        tile_scale = (float)tm.scale;
        world_origin = {(float)tm.offset.x, -(float)tm.offset.y - h * tile_scale};
        world_size = {w * tile_scale, h * tile_scale};

        auto is_air = [&](uint32_t x, uint32_t y) { return tm.tilemap[x][y] == 0; };
        // opening length (in tiles) -> 1 (small, glows) .. 0 (large, no glow)
        auto factor_for = [&](uint32_t run_tiles) {
            const float len = run_tiles * tile_scale;
            const float t = std::clamp((len - fade_start) / std::max(fade_end - fade_start, 1e-3f), 0.0f, 1.0f);
            return 1.0f - t * t * (3.0f - 2.0f * t);
        };

        const uint32_t tw = w + 2, th = h + 2;
        std::vector<std::byte> px((size_t)tw * th * 4, std::byte{0});
        auto put = [&](uint32_t x, uint32_t y, int channel, float v) {
            px[((size_t)(y + 1) * tw + (x + 1)) * 4 + channel] = (std::byte)(uint8_t)std::lround(v * 255.0f);
        };

        for (uint32_t x = 0; x < w; ++x) {  // R: contiguous air along each column
            for (uint32_t y = 0; y < h;) {
                if (!is_air(x, y)) { ++y; continue; }
                uint32_t end = y;
                while (end < h && is_air(x, end)) ++end;
                const float f = factor_for(end - y);
                for (uint32_t yy = y; yy < end; ++yy) put(x, yy, 0, f);
                y = end;
            }
        }
        for (uint32_t y = 0; y < h; ++y) {  // G: contiguous air along each row
            for (uint32_t x = 0; x < w;) {
                if (!is_air(x, y)) { ++x; continue; }
                uint32_t end = x;
                while (end < w && is_air(end, y)) ++end;
                const float f = factor_for(end - x);
                for (uint32_t xx = x; xx < end; ++xx) put(xx, y, 1, f);
                x = end;
            }
        }

        for (uint32_t i = 0; i < tw * th; ++i) px[(size_t)i * 4 + 3] = (std::byte)0xFF;

        using PF = Base::Renderer::PixelFormat;
        if (mask_id == 0 || mask_dims.x != tw || mask_dims.y != th) {
            // New id on a size change. Safe now thanks to the bind-group fix in section 2.
            // (The renderer has no destroyTexture yet, so the old one stays allocated.)
            mask_id = r.createTexture(tw, th, PF::RGBA8Unorm, px);
            mask_dims = {tw, th};
        } else {
            r.updateTextureRegion(mask_id, 0, 0, tw, th, PF::RGBA8Unorm, px);
        }
    }

    void refresh_view(const Base::Renderer& r) {
        const auto vp = r.get_viewport_size();
        const float min_dim = (float)std::min(vp.x, vp.y);
        const float ppu = r.camera._real_zoom > 0 ? min_dim / (float)r.camera._real_zoom : 0.0f;

        params.cam_and_view = {r.camera.x, r.camera.y, ppu, 0.0f};
        params.viewport_glow = {(float)vp.x, (float)vp.y, reach, probe_offset};
        params.level_rect = {world_origin.x, world_origin.y, world_size.x, world_size.y};
        params.mask_info = {tile_scale, (float)mask_dims.x, (float)mask_dims.y, intensity};
        params.colour = {colour.r, colour.g, colour.b, 0.0f};
    }

    void register_post_effect(Base::Renderer& r) {
        r.post_queue.push_back(Base::Renderer::PostEffect{
            .pixel_shader = kPixelShader,
            .params = std::as_bytes(std::span{&params, 1}),
            .extra_textures = std::span{&mask_id, 1},
            .pre_render = [this](const Base::Renderer& rr) { refresh_view(rr); },
        });
    }
};
