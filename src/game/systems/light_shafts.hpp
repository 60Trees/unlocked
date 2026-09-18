/**
 * @file src/game/systems/light_shafts.hpp
 * @author 60Trees_ (github.com/60Trees)
 */

#pragma once
#include <base/renderer.hpp>
#include <game/base/world_handler.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>
#include <glm/vec4.hpp>
#include <SDL3/SDL.h>

struct LightShaftSystem {
    // The renderer gives every draw exactly 256 bytes of params (kParamMax). 6 header vec4s + 2 per pass = 5 passes max.
    static constexpr size_t kMaxPasses = 5;

    bool hard_shadows = true;
    float penumbra_width = 6.0f;    /// (world units) how spread out the soft shadows are
    uint32_t soft_probe_count = 5;  /// how many probes for soft shadows

    /// Details for each noise and how it is affected by noise / distance / clouds
    struct Pass {
        float noise_scale;       /// noise scale (world units) (0 = no noise)
        float noise_threshold;   /// noise threshold (0 to 1, 0.5 = 50% coverage, 0.2 = 20% coverage, etc)
        float speed_multiplier;  /// speed multiplier for cloud speed (0 = dont move)
        float weight;            /// how dark it gets when the light shaft is blocked (0 to 1)
        float fade_start;        /// world units below the top of the level where this layer starts fading out
        float fade_length;       /// world units the fade takes to go from full strength to zero (<=0 = never fades)
    };
    std::vector<Pass> passes = {
        {16.0f, 0.50f, 20.0f, 0.50f, 0.0f, -1.0f},
        {8.0f, 0.40f, 55.0f, 0.35f, 0.0f, 8.0f},
        {4.0f, 0.30f, 130.0f, 0.25f, 40.0f, 16.0f},
    };

    struct ParamsCPU {
        glm::vec4 sun_and_cam;        // sun_dir.xy (direction the light TRAVELS), cam.xy
        glm::vec4 px_viewport_count;  // px_per_unit, viewport.xy, pass_count
        glm::vec4 atlas_rect;         // level origin (bottom-left).xy, level size.xy   (world units)
        glm::vec4 shadow_flags;       // hard(0/1), penumbra_width, probe_count, -
        glm::vec4 shadow_map;         // l_min, 1/bin, s_min, s_range
        glm::vec4 shadow_map_dims;    // map width (texels), bin count, -, -
        struct PassSlot {
            glm::vec4 noise;  // noise_scale, threshold, animated phase, weight
            glm::vec4 fade;   // fade_start, fade_length, -, -
        } pass[kMaxPasses];
    } params{};
    static_assert(sizeof(ParamsCPU) == 256, "params must be exactly the renderer's 256-byte per-draw budget");

    // ------------------------------------------------------------------------------------------
    // Shader. Coordinates: for a world point p, with `dir` = unit vector TOWARD the sun and
    // perp = (-dir.y, dir.x):  l = dot(p, perp) is "which sun ray am I on", s = dot(p, dir) is
    // "how far along that ray toward the sun". The CPU bakes, per l-bin, the largest s that is
    // still inside solid terrain. A point is lit iff s is past that.
    // ------------------------------------------------------------------------------------------
    static constexpr std::string_view kPixelShader = R"(
fn hash12(p: vec2f) -> f32 {
    var p3 = fract(vec3f(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

fn value_noise1d(x: f32, seed: f32) -> f32 {
    let i = floor(x);
    let f = fract(x);
    let a = hash12(vec2f(i, seed));
    let b = hash12(vec2f(i + 1.0, seed));
    let u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u);
}

// 1.0 = nothing solid between this point and the sun, 0.0 = in terrain shadow.
fn shadow_lookup(lateral: f32, s: f32) -> f32 {
    let bin_f = floor((lateral - params[4].x) * params[4].y);
    if (bin_f < 0.0 || bin_f >= params[5].y) { return 1.0; }
    let map_w = u32(params[5].x);
    let idx = u32(bin_f);
    let t = textureLoad(extraTex0, vec2<u32>(idx % map_w, idx / map_w), 0);
    let q = round(t.r * 255.0) * 65536.0 + round(t.g * 255.0) * 256.0 + round(t.b * 255.0);
    if (q < 0.5) { return 1.0; }  // nothing solid anywhere along this sun ray
    let occluder_s = params[4].z + (q - 1.0) / 16777214.0 * params[4].w;
    return select(0.0, 1.0, s >= occluder_s - 0.02);
}

fn cloud_transmittance(lateral: f32, pass_count: u32) -> f32 {
    var transmittance = 1.0;
    for (var p: u32 = 0u; p < pass_count; p = p + 1u) {
        let cfg = params[6u + 2u * p];  // x=block_size, y=coverage, z=phase, w=weight
        if (cfg.x <= 0.0) { continue; }

        let x = lateral / cfg.x + cfg.z;
        let n = value_noise1d(x, f32(p) * 17.0);

        let edge = 0.04;
        let inside_cloud = 1.0 - smoothstep(cfg.y - edge, cfg.y + edge, n);
        transmittance *= mix(1.0, 1.0 - clamp(cfg.w, 0.0, 1.0), inside_cloud);
    }
    return transmittance;
}

fn depth_fade(world_pos: vec2f, pass_count: u32) -> f32 {
    let depth = max((params[2].y + params[2].w) - world_pos.y, 0.0);
    var strongest: f32 = 0.0;
    for (var p: u32 = 0u; p < pass_count; p = p + 1u) {
        let fade = params[7u + 2u * p];  // x=fade_start, y=fade_length
        if (fade.y <= 0.0) {
            strongest = 1.0;
            continue;
        }
        strongest = max(strongest, 1.0 - smoothstep(fade.x, fade.x + fade.y, depth));
    }
    return strongest;
}

fn sun_visibility(world_pos: vec2f, march_dir: vec2f, in_level: bool,
                  hard: bool, penumbra_width: f32, probe_count: u32, pass_count: u32) -> f32 {
    let perp = vec2f(-march_dir.y, march_dir.x);
    let s = dot(world_pos, march_dir);
    let lateral = dot(world_pos, perp);

    if (hard || probe_count <= 1u) {
        let terrain = select(1.0, shadow_lookup(lateral, s), in_level);
        return terrain * cloud_transmittance(lateral, pass_count);
    }
    var total: f32 = 0.0;
    for (var i: u32 = 0u; i < probe_count; i = i + 1u) {
        let fi = f32(i) / f32(probe_count - 1u) - 0.5;  // -0.5 .. 0.5 across the probes
        let l = lateral + fi * penumbra_width;
        let terrain = select(1.0, shadow_lookup(l, s), in_level);
        total += terrain * cloud_transmittance(l, pass_count);
    }
    return total / f32(probe_count);
}

@fragment fn fs_main(in: VSOut) -> @location(0) vec4f {
    let base = textureSample(prevTex, prevSamp, in.uv);
    let px_per_unit = params[1].x;
    if (px_per_unit <= 0.0) { return base; }

    let sun_dir = params[0].xy;
    let cam_pos = params[0].zw;
    let viewport = params[1].yz;
    let pass_count = u32(params[1].w);
    let hard_shadows = params[3].x > 0.5;
    let penumbra_width = params[3].y;
    let probe_count = max(u32(params[3].z), 1u);

    let ndc = in.uv * 2.0 - vec2f(1.0);
    let world_pos = cam_pos + vec2f(ndc.x, -ndc.y) * (viewport / (2.0 * px_per_unit));
    let march_dir = -sun_dir;  // toward the sun

    // Outside the level = open sky (same rule as before)
    let box_min = params[2].xy;
    let box_max = box_min + params[2].zw;
    let in_level = all(world_pos >= box_min) && all(world_pos <= box_max);

    let shaft = sun_visibility(world_pos, march_dir, in_level, hard_shadows, penumbra_width, probe_count, pass_count)
              * depth_fade(world_pos, pass_count);

    let shaft_colour = vec3f(0.96, 0.94, 0.58);
    let intensity = 0.3;
    return vec4f(base.rgb + shaft_colour * (shaft * intensity), base.a);
}
)";

    // ------------------------------------------------------------------------------------------
    // CPU side
    // ------------------------------------------------------------------------------------------
    static constexpr uint32_t kMapWidth = 2048, kMapHeight = 128;  // 262144 sideways bins (RGBA8 -> 1 MB), created ONCE

    ushort shadow_map_id = 0;
    glm::vec2 atlas_world_origin{0, 0};  // bottom-left of the level
    glm::vec2 atlas_world_size{0, 0};
    float sun_angle_deg = -48.0f;

    // The level's solid mask is kept so the shadow map can be rebuilt when the sun angle changes.
    std::vector<uint8_t> solid;
    uint32_t tile_w = 0, tile_h = 0;
    float tile_scale = 1.0f;

    void ensure_shadow_map(Base::Renderer& r) {
        if (shadow_map_id != 0) return;
        std::vector<std::byte> blank((size_t)kMapWidth * kMapHeight * 4, std::byte{0});
        shadow_map_id = r.createTexture(kMapWidth, kMapHeight, Base::Renderer::PixelFormat::RGBA8Unorm, blank);
    }

    void apply_sun_params() {
        const float a = sun_angle_deg * 3.14159265f / 180.0f;
        params.sun_and_cam.x = std::cos(a);
        params.sun_and_cam.y = std::sin(a);
    }

    void bake_level(Base::Renderer& r, const Game::TileMap& tm) {
        // all_level_tilemaps[...] default-constructs an empty map if the level hasn't been rendered yet.
        if (tm.size.x == 0 || tm.size.y == 0) return;

        tile_w = tm.size.x;
        tile_h = tm.size.y;
        tile_scale = (float)tm.scale;
        solid.assign((size_t)tile_w * tile_h, 0);
        for (uint32_t y = 0; y < tile_h; ++y)
            for (uint32_t x = 0; x < tile_w; ++x) solid[(size_t)y * tile_w + x] = tm.tilemap[x][y] != 0 ? 1 : 0;

        atlas_world_origin = {(float)tm.offset.x, -(float)tm.offset.y - tile_h * tile_scale};
        atlas_world_size = {tile_w * tile_scale, tile_h * tile_scale};
        params.atlas_rect = {atlas_world_origin.x, atlas_world_origin.y, atlas_world_size.x, atlas_world_size.y};

        rebuild_shadow_map(r);
    }

    void rebuild_shadow_map(Base::Renderer& r) {
        ensure_shadow_map(r);
        apply_sun_params();
        if (solid.empty()) return;

        constexpr double kPi = 3.14159265358979323846;
        constexpr double kInf = std::numeric_limits<double>::infinity();

        const double a = (double)sun_angle_deg * kPi / 180.0;
        const double dir_x = -std::cos(a), dir_y = -std::sin(a);  // toward the sun (matches the shader's -sun_dir)
        const double perp_x = -dir_y, perp_y = dir_x;

        const double sc = tile_scale;
        const double ox = atlas_world_origin.x, oy = atlas_world_origin.y;
        const double right = ox + tile_w * sc, top = oy + tile_h * sc;

        // The level's extent in (l, s) space
        double l_min = kInf, l_max = -kInf, s_min = kInf, s_max = -kInf;
        for (double cx : {ox, right})
            for (double cy : {oy, top}) {
                const double l = cx * perp_x + cy * perp_y, s = cx * dir_x + cy * dir_y;
                l_min = std::min(l_min, l);
                l_max = std::max(l_max, l);
                s_min = std::min(s_min, s);
                s_max = std::max(s_max, s);
            }

        // 1/8 of a game pixel per bin; coarsened only if the level is so big it wouldn't fit the texture
        const double capacity = (double)kMapWidth * kMapHeight - 2.0;
        const double bin = std::max(0.125, (l_max - l_min) / capacity);
        const uint32_t bin_count = (uint32_t)std::ceil((l_max - l_min) / bin) + 1;
        const double s_span = std::max(s_max - s_min, 1e-3);

        std::vector<double> max_s(bin_count, -kInf);

        auto is_solid = [&](int x, int y) {
            return x >= 0 && y >= 0 && x < (int)tile_w && y < (int)tile_h && solid[(size_t)y * tile_w + x] != 0;
        };
        // Clips the line p(s) = p0 + s*d to the slab [lo, hi], narrowing [s0, s1]
        auto clip = [](double p0, double d, double lo, double hi, double& s0, double& s1) {
            if (std::abs(d) < 1e-12) return p0 >= lo && p0 <= hi;
            double ta = (lo - p0) / d, tb = (hi - p0) / d;
            if (ta > tb) std::swap(ta, tb);
            s0 = std::max(s0, ta);
            s1 = std::min(s1, tb);
            return true;
        };

        for (int y = 0; y < (int)tile_h; ++y) {
            for (int x = 0; x < (int)tile_w; ++x) {
                if (!is_solid(x, y)) continue;
                // A tile fully buried in solid tiles can never be the LAST solid on a ray: skip it.
                if (is_solid(x - 1, y) && is_solid(x + 1, y) && is_solid(x, y - 1) && is_solid(x, y + 1)) continue;

                const double x0 = ox + x * sc, x1 = x0 + sc;
                const double y1 = top - y * sc, y0 = y1 - sc;

                double t_lmin = kInf, t_lmax = -kInf;
                for (double cx : {x0, x1})
                    for (double cy : {y0, y1}) {
                        const double l = cx * perp_x + cy * perp_y;
                        t_lmin = std::min(t_lmin, l);
                        t_lmax = std::max(t_lmax, l);
                    }
                const int b0 = std::max(0, (int)std::floor((t_lmin - l_min) / bin));
                const int b1 = std::min((int)bin_count - 1, (int)std::floor((t_lmax - l_min) / bin));

                for (int b = b0; b <= b1; ++b) {
                    const double lc = l_min + (b + 0.5) * bin;
                    double s0 = -kInf, s1 = kInf;
                    // point on this sun ray: lc*perp + s*dir  -> where does it leave the tile?
                    if (!clip(lc * perp_x, dir_x, x0, x1, s0, s1)) continue;
                    if (!clip(lc * perp_y, dir_y, y0, y1, s0, s1)) continue;
                    if (s0 <= s1) max_s[b] = std::max(max_s[b], s1);
                }
            }
        }

        // Encode as 24-bit in RGB. 0 = "no solid on this ray", 1..16777215 = position within [s_min, s_min+s_span]
        const uint32_t rows = (bin_count + kMapWidth - 1) / kMapWidth;
        std::vector<std::byte> texels((size_t)rows * kMapWidth * 4, std::byte{0});
        for (uint32_t b = 0; b < bin_count; ++b) {
            if (max_s[b] == -kInf) continue;
            const double v = std::clamp((max_s[b] - s_min) / s_span, 0.0, 1.0);
            const uint32_t q = 1u + (uint32_t)std::llround(v * 16777214.0);
            std::byte* px = &texels[(size_t)b * 4];
            px[0] = (std::byte)((q >> 16) & 0xFF);
            px[1] = (std::byte)((q >> 8) & 0xFF);
            px[2] = (std::byte)(q & 0xFF);
            px[3] = (std::byte)0xFF;
        }
        r.updateTextureRegion(shadow_map_id, 0, 0, kMapWidth, rows, Base::Renderer::PixelFormat::RGBA8Unorm, texels);

        params.shadow_map = {(float)l_min, (float)(1.0 / bin), (float)s_min, (float)s_span};
        params.shadow_map_dims = {(float)kMapWidth, (float)bin_count, 0.0f, 0.0f};
    }

    /// Call every frame (signature unchanged). Only does real work when the sun angle changes.
    void update(Base::Renderer& r, float angle_deg) {
        if (std::abs(angle_deg - sun_angle_deg) > 1e-3f) {
            sun_angle_deg = angle_deg;
            rebuild_shadow_map(r);
        }
        apply_sun_params();
    }

    /// Camera-dependent params. Runs from the renderer's pre_render hook, i.e. AFTER the camera has moved for this frame.
    void refresh_view(const Base::Renderer& r) {
        const auto vp = r.get_viewport_size();
        const float min_dim = (float)std::min(vp.x, vp.y);
        const float px_per_unit = r.camera._real_zoom > 0 ? min_dim / (float)r.camera._real_zoom : 0.0f;

        params.sun_and_cam.z = r.camera.x;
        params.sun_and_cam.w = r.camera.y;
        params.px_viewport_count = {px_per_unit, (float)vp.x, (float)vp.y, (float)std::min(passes.size(), kMaxPasses)};
        params.shadow_flags = {hard_shadows ? 1.0f : 0.0f, penumbra_width, (float)soft_probe_count, 0.0f};

        const float t = (float)SDL_GetTicks() / 1000.0f;
        for (size_t i = 0; i < kMaxPasses; ++i) {
            if (i < passes.size()) {
                const float phase = t * 0.005f * passes[i].speed_multiplier;
                params.pass[i].noise = {passes[i].noise_scale, passes[i].noise_threshold, phase, passes[i].weight};
                params.pass[i].fade = {passes[i].fade_start, passes[i].fade_length, 0.0f, 0.0f};
            } else {
                params.pass[i].noise = {0.0f, 0.0f, 0.0f, 0.0f};
                params.pass[i].fade = {0.0f, 0.0f, 0.0f, 0.0f};
            }
        }
    }

    void register_post_effect(Base::Renderer& r) {
        ensure_shadow_map(r);
        r.post_queue.push_back(Base::Renderer::PostEffect{
            .pixel_shader = kPixelShader,
            .params = std::as_bytes(std::span{&params, 1}),
            .extra_textures = std::span{&shadow_map_id, 1},
            .pre_render = [this](const Base::Renderer& rr) { refresh_view(rr); },
        });
    }
};
