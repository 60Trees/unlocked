/**
 * @file src/game/systems/light_shafts.hpp
 * @author 60Trees_ (github.com/60Trees)
 */

#pragma once
#include <base/renderer.hpp>
#include <game/base/world_handler.hpp>
#include <vector>
#include <span>
#include <cmath>
#include <glm/vec4.hpp>
#include <SDL3/SDL.h>

struct LightShaftSystem {
    ushort atlas_id = 0;
    glm::vec<2, uint32_t> atlas_dims{0, 0};
    glm::vec2 atlas_world_origin{0, 0};
    glm::vec2 atlas_world_size{0, 0};

    bool hard_shadows = true;
    float penumbra_width = 6.0f;    /// (world units) how spread out the soft shadows are
    uint32_t soft_probe_count = 5;  /// how many steps for soft shadows

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
        glm::vec4 sun_and_cam;
        glm::vec4 px_viewport_count;
        glm::vec4 atlas_rect;
        glm::vec4 shadow_flags;  // x=hard_shadows(0/1), y=penumbra_width, z=soft_probe_count, w=reserved
        glm::vec4 pass_data[13];
        glm::vec4 pass_fade[13];  // x=fade_start, y=fade_length, per pass
    } params{};

    static constexpr std::string_view kPixelShader = R"(
fn hash12(p: vec2f) -> f32 {
    var p3 = fract(vec3f(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

fn box_exit_dist(pos: vec2f, dir: vec2f, box_min: vec2f, box_max: vec2f) -> f32 {
    if (pos.x < box_min.x || pos.x > box_max.x || pos.y < box_min.y || pos.y > box_max.y) {
        return 0.0;
    }
    var t_exit = 1e30;
    if (dir.x > 0.0) { t_exit = min(t_exit, (box_max.x - pos.x) / dir.x); }
    else if (dir.x < 0.0) { t_exit = min(t_exit, (box_min.x - pos.x) / dir.x); }
    if (dir.y > 0.0) { t_exit = min(t_exit, (box_max.y - pos.y) / dir.y); }
    else if (dir.y < 0.0) { t_exit = min(t_exit, (box_min.y - pos.y) / dir.y); }
    return max(t_exit, 0.0);
}

fn terrain_visible(origin: vec2f, march_dir: vec2f, atlas_origin: vec2f, atlas_size: vec2f) -> f32 {
    let box_min = atlas_origin;
    let box_max = atlas_origin + atlas_size;
    let max_dist = box_exit_dist(origin, march_dir, box_min, box_max);
    if (max_dist <= 0.0) { return 1.0; }  // starting outside the level entirely -> open sky
    let steps = clamp(u32(max_dist / 4.0), 8u, 48u);  // tune 4.0 to your tile size
    for (var s: u32 = 0u; s < steps; s = s + 1u) {
        let t = (f32(s) + 0.5) / f32(steps);
        let sample_pos = origin + march_dir * (t * max_dist);
        var uv = (sample_pos - atlas_origin) / atlas_size;
        uv.y = 1.0 - uv.y;
        let in_bounds = all(uv >= vec2f(0.0)) && all(uv <= vec2f(1.0));
        if (!in_bounds) { break; }  // exited the level -> nothing left to hit
        let mask = textureSampleLevel(extraTex0, prevSamp, uv, 0.0).r;
        if (mask > 0.5) { return 0.0; }
    }
    return 1.0;
}

fn value_noise1d(x: f32, seed: f32) -> f32 {
    let i = floor(x);
    let f = fract(x);
    let a = hash12(vec2f(i, seed));
    let b = hash12(vec2f(i + 1.0, seed));
    let u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u);
}

fn cloud_transmittance(world_pos: vec2f, march_dir: vec2f, pass_count: u32) -> f32 {
    let perp = vec2f(-march_dir.y, march_dir.x);
    let lateral = dot(world_pos, perp);  // this ray's position across the sun's rays -- the ONLY thing that matters

    var transmittance = 1.0;
    for (var p: u32 = 0u; p < pass_count; p = p + 1u) {
        let cfg = params[4u + p];  // x=block_size, y=coverage_pct, z=phase, w=weight (0..1 opacity)
        if (cfg.x <= 0.0) { continue; }  // 0 = clear-sky baseline, never blocks

        let x = lateral / cfg.x + cfg.z;
        let n = value_noise1d(x, f32(p) * 17.0);  // seed decorrelates this layer from the others

        let edge = 0.04;  // fraction of the noise range the cloud edge fades over, for a soft boundary
        let inside_cloud = 1.0 - smoothstep(cfg.y - edge, cfg.y + edge, n);
        transmittance *= mix(1.0, 1.0 - clamp(cfg.w, 0.0, 1.0), inside_cloud);
    }
    return transmittance;
}

fn ray_visibility(origin: vec2f, march_dir: vec2f, atlas_origin: vec2f, atlas_size: vec2f, pass_count: u32) -> f32 {
    return terrain_visible(origin, march_dir, atlas_origin, atlas_size) * cloud_transmittance(origin, march_dir, pass_count);
}

fn depth_from_top(world_pos: vec2f, atlas_origin: vec2f, atlas_size: vec2f) -> f32 {
    return max((atlas_origin.y + atlas_size.y) - world_pos.y, 0.0);
}

fn depth_fade(world_pos: vec2f, atlas_origin: vec2f, atlas_size: vec2f, pass_count: u32) -> f32 {
    let depth = depth_from_top(world_pos, atlas_origin, atlas_size);
    var strongest: f32 = 0.0;
    for (var p: u32 = 0u; p < pass_count; p = p + 1u) {
        let fade = params[17u + p];  // x=fade_start, y=fade_length (world units below the top)
        if (fade.y <= 0.0) {
            strongest = 1.0;  // this layer never fades -- shaft stays fully alive
            continue;
        }
        let f = 1.0 - smoothstep(fade.x, fade.x + fade.y, depth);
        strongest = max(strongest, f);
    }
    return strongest;
}

fn sun_visibility(world_pos: vec2f, march_dir: vec2f, atlas_origin: vec2f, atlas_size: vec2f,
                   hard: bool, penumbra_width: f32, probe_count: u32, pass_count: u32) -> f32 {
    if (hard || probe_count <= 1u) {
        return ray_visibility(world_pos, march_dir, atlas_origin, atlas_size, pass_count);
    }
    let perp = vec2f(-march_dir.y, march_dir.x);
    var total: f32 = 0.0;
    for (var i: u32 = 0u; i < probe_count; i = i + 1u) {
        let fi = f32(i) / f32(probe_count - 1u) - 0.5;  // -0.5 .. 0.5 across the probes
        let origin = world_pos + perp * (fi * penumbra_width);
        total += ray_visibility(origin, march_dir, atlas_origin, atlas_size, pass_count);
    }
    return total / f32(probe_count);
}

@fragment fn fs_main(in: VSOut) -> @location(0) vec4f {
    let base = textureSample(prevTex, prevSamp, in.uv);
    let sun_dir = params[0].xy;
    let cam_pos = params[0].zw;
    let px_per_unit = params[1].x;
    let viewport = params[1].yz;
    let pass_count = u32(params[1].w);
    let atlas_origin = params[2].xy;
    let atlas_size = params[2].zw;
    let hard_shadows = params[3].x > 0.5;
    let penumbra_width = params[3].y;
    let probe_count = max(u32(params[3].z), 1u);

    let ndc = in.uv * 2.0 - vec2f(1.0);
    let world_pos = cam_pos + vec2f(ndc.x, -ndc.y) * (viewport / (2.0 * px_per_unit));
    let march_dir = -sun_dir;  // toward the sun -- same convention as before

    let shaft = sun_visibility(world_pos, march_dir, atlas_origin, atlas_size, hard_shadows, penumbra_width, probe_count, pass_count)
              * depth_fade(world_pos, atlas_origin, atlas_size, pass_count);

    let shaft_colour = vec3f(0.96, 0.94, 0.58);
    let intensity = 0.3;
    return vec4f(base.rgb + shaft_colour * (shaft * intensity), base.a);
}
)";

    void ensure_atlas(Base::Renderer& r, uint32_t w, uint32_t h) {
        if (atlas_id != 0 && atlas_dims.x == w && atlas_dims.y == h) return;
        std::vector<std::byte> blank((size_t)w * h, std::byte{0});
        atlas_id = r.createTexture(w, h, Base::Renderer::PixelFormat::R8Unorm, blank);
        atlas_dims = {w, h};
    }

    void bake_level(Base::Renderer& r, const Game::TileMap& tm) {
        ensure_atlas(r, tm.size.x, tm.size.y);
        std::vector<std::byte> buf((size_t)tm.size.x * tm.size.y);
        for (uint32_t y = 0; y < tm.size.y; ++y)
            for (uint32_t x = 0; x < tm.size.x; ++x) buf[y * tm.size.x + x] = (std::byte)(tm.tilemap[x][y] != 0 ? 0xFF : 0x00);
        r.updateTextureRegion(atlas_id, 0, 0, tm.size.x, tm.size.y, Base::Renderer::PixelFormat::R8Unorm, buf);

        atlas_world_origin = {(float)tm.offset.x, -(float)tm.offset.y - tm.size.y * (float)tm.scale};
        atlas_world_size = {tm.size.x * (float)tm.scale, tm.size.y * (float)tm.scale};
    }

    void register_post_effect(Base::Renderer& r) {
        r.post_queue.push_back(Base::Renderer::PostEffect{
            .pixel_shader = kPixelShader,
            .params = std::as_bytes(std::span{&params, 1}),
            .extra_textures = std::span{&atlas_id, 1},
        });
    }

    void update(Base::Renderer& r, float sun_angle_deg) {
        auto vp = r.get_viewport_size();
        float minDim = (float)std::min(vp.x, vp.y);
        float pxPerUnit = r.camera._real_zoom > 0 ? minDim / (float)r.camera._real_zoom : 0.0f;
        float a = (float)(sun_angle_deg * 3.14159265 / 180.0);

        params.sun_and_cam = {std::cos(a), std::sin(a), r.camera.x, r.camera.y};
        params.px_viewport_count = {pxPerUnit, (float)vp.x, (float)vp.y, (float)passes.size()};
        params.atlas_rect = {atlas_world_origin.x, atlas_world_origin.y, atlas_world_size.x, atlas_world_size.y};
        params.shadow_flags = {hard_shadows ? 1.0f : 0.0f, penumbra_width, (float)soft_probe_count, 0.0f};

        float t = (float)SDL_GetTicks() / 1000.0f;
        for (size_t i = 0; i < passes.size() && i < 13; ++i) {
            float speed = 0.005f * passes[i].speed_multiplier;
            float animated_phase = t * speed;
            params.pass_data[i] = {passes[i].noise_scale, passes[i].noise_threshold, animated_phase, passes[i].weight};
            params.pass_fade[i] = {passes[i].fade_start, passes[i].fade_length, 0.0f, 0.0f};
        }
    }
};
