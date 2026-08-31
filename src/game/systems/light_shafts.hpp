#pragma once
#include <base/renderer.hpp>
#include <game/base/world_handler.hpp>
#include <vector>
#include <span>
#include <cmath>
#include <glm/vec4.hpp>

struct LightShaftSystem {
    ushort atlas_id = 0;
    glm::vec<2, uint32_t> atlas_dims{0, 0};
    glm::vec2 atlas_world_origin{0, 0};  // bottom-left of coverage, world units
    glm::vec2 atlas_world_size{0, 0};

    // --- terrain shadow edge (geometry only, nothing to do with clouds) ---
    bool hard_shadows = true;      // false = soft penumbra via jittered probes, true = single crisp ray
    float penumbra_width = 6.0f;    // world units; perpendicular spread of the soft-shadow probes
    uint32_t soft_probe_count = 5;  // rays averaged for the penumbra when !hard_shadows (odd looks best)

    // --- cloud/atmosphere layering (never affects whether terrain blocks light) ---
    struct Pass {
        float noise_scale;   // spatial frequency of this layer (0 = "clear sky" baseline, always fully lit)
        float coverage_pct;  // 0..1 fraction of this layer that reads as cloud and dims the shaft
        float reach;         // world units this layer is sampled over along the sun ray (0 = default 128)
        float weight;        // blend weight against the other passes
    };
    std::vector<Pass> passes = {
        {0.4f, 0.50f, 0.0f, 9.0f},
        {0.9f, 0.50f, 0.0f, 9.0f},
        {9.5f, 0.50f, 0.0f, 9.0f},
    };

    struct ParamsCPU {
        glm::vec4 sun_and_cam;
        glm::vec4 px_viewport_count;
        glm::vec4 atlas_rect;
        glm::vec4 shadow_flags;   // x=hard_shadows(0/1), y=penumbra_width, z=soft_probe_count, w=reserved
        glm::vec4 pass_data[13];
    } params{};

    // one pixel shader — hash12 is just a helper the fragment entry point calls, not a second shader
    //*

//creates a REALLY COOL shadowy effect, but not quite what i was looking for.
static constexpr std::string_view kPixelShader = R"(
fn hash12(p: vec2f) -> f32 {
    var p3 = fract(vec3f(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// distance to travel from `pos` along `dir` before leaving the atlas AABB.
// returns 0 if `pos` is already outside the box (nothing to march — open sky).
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

// single ray: 1.0 if it reaches the level edge with no solid tile in the way, else 0.0.
// first hit blocks and stops — this is a real cumulative occlusion test, not a per-sample average.
fn probe_visible(origin: vec2f, march_dir: vec2f, atlas_origin: vec2f, atlas_size: vec2f) -> f32 {
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
        if (textureSample(extraTex0, prevSamp, uv).r > 0.5) { return 0.0; }
    }
    return 1.0;
}

// terrain shadow term. hard = one ray. soft = several rays offset perpendicular to the
// sun direction and averaged, which is what actually produces a penumbra.
fn sun_visibility(world_pos: vec2f, march_dir: vec2f, atlas_origin: vec2f, atlas_size: vec2f,
                   hard: bool, penumbra_width: f32, probe_count: u32) -> f32 {
    if (hard || probe_count <= 1u) {
        return probe_visible(world_pos, march_dir, atlas_origin, atlas_size);
    }
    let perp = vec2f(-march_dir.y, march_dir.x);
    var total: f32 = 0.0;
    for (var i: u32 = 0u; i < probe_count; i = i + 1u) {
        let fi = f32(i) / f32(probe_count - 1u) - 0.5;  // -0.5 .. 0.5 across the probes
        let origin = world_pos + perp * (fi * penumbra_width);
        total += probe_visible(origin, march_dir, atlas_origin, atlas_size);
    }
    return total / f32(probe_count);
}

// atmospheric cloud layering: a pure brightness texture, sampled along the sun ray.
// completely independent of terrain — never treated as occlusion, only ever blended.
// a scale-0 pass is a "clear sky" floor so full cloud cover can't fully black out the shaft.
fn cloud_density(world_pos: vec2f, march_dir: vec2f, pass_count: u32) -> f32 {
    let steps = 16u;
    var density: f32 = 0.0;
    var weight_sum: f32 = 0.0;
    for (var p: u32 = 0u; p < pass_count; p = p + 1u) {
        let cfg = params[4u + p];  // x=noise_scale, y=coverage_pct, z=reach, w=weight
        if (cfg.x <= 0.0) {
            density += cfg.w;
            weight_sum += cfg.w;
            continue;
        }
        let reach = select(128.0, cfg.z, cfg.z > 0.0);  // tune 128.0 to your world scale
        var acc: f32 = 0.0;
        for (var s: u32 = 0u; s < steps; s = s + 1u) {
            let t = (f32(s) + 0.5) / f32(steps);
            let sample_pos = world_pos + march_dir * (t * reach);
            let n = hash12(sample_pos * cfg.x);
            acc += select(0.0, 1.0, n >= cfg.y);  // n < coverage -> in-cloud -> blocked (same sense as before)
        }
        density += cfg.w * (acc / f32(steps));
        weight_sum += cfg.w;
    }
    return select(1.0, density / weight_sum, weight_sum > 0.0);
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
    let march_dir = -sun_dir;  // toward the sun — same convention as before

    let visibility = sun_visibility(world_pos, march_dir, atlas_origin, atlas_size, hard_shadows, penumbra_width, probe_count);
    let clouds = cloud_density(world_pos, march_dir, pass_count);
    let shaft = visibility * clouds;

    let shaft_colour = vec3f(0.96, 0.94, 0.58);
    let intensity = 0.3;
    return vec4f(base.rgb + shaft_colour * (shaft * intensity), base.a);
}
)";
//*/
    void ensure_atlas(Base::Renderer& r, uint32_t w, uint32_t h) {
        if (atlas_id != 0 && atlas_dims.x == w && atlas_dims.y == h) return;
        std::vector<std::byte> blank((size_t)w * h, std::byte{0});
        atlas_id = r.createTexture(w, h, Base::Renderer::PixelFormat::R8Unorm, blank);
        atlas_dims = {w, h};
    }

    // any IntGrid value != 0 blocks light — bake straight from TileMap::tilemap
    void bake_level(Base::Renderer& r, const Game::TileMap& tm) {
        ensure_atlas(r, tm.size.x, tm.size.y);
        std::vector<std::byte> buf((size_t)tm.size.x * tm.size.y);
        for (uint32_t y = 0; y < tm.size.y; ++y)
            for (uint32_t x = 0; x < tm.size.x; ++x)
                buf[y * tm.size.x + x] = (std::byte)(tm.tilemap[x][y] != 0 ? 0xFF : 0x00);
        r.updateTextureRegion(atlas_id, 0, 0, tm.size.x, tm.size.y, Base::Renderer::PixelFormat::R8Unorm, buf);

        // match this to whatever grid<->world formula your ix/iy collision lookup already uses —
        // flip the sign here if the shafts end up vertically mirrored.
        atlas_world_origin = {(float)tm.offset.x, -(float)tm.offset.y - tm.size.y * (float)tm.scale};
        atlas_world_size = {tm.size.x * (float)tm.scale, tm.size.y * (float)tm.scale};
    }

    // call once, after the first bake_level
    void register_post_effect(Base::Renderer& r) {
        r.post_queue.push_back(Base::Renderer::PostEffect{
            .pixel_shader = kPixelShader,
            .params = std::as_bytes(std::span{&params, 1}),
            .extra_textures = std::span{&atlas_id, 1},
        });
    }

    // call every frame — mutates params in place, does NOT touch post_queue
    void update(Base::Renderer& r, float sun_angle_deg) {
        auto vp = r.get_viewport_size();
        float minDim = (float)std::min(vp.x, vp.y);
        float pxPerUnit = r.camera._real_zoom > 0 ? minDim / (float)r.camera._real_zoom : 0.0f;
        float a = (float)(sun_angle_deg * 3.14159265 / 180.0);

        params.sun_and_cam = {std::cos(a), std::sin(a), r.camera.x, r.camera.y};
        params.px_viewport_count = {pxPerUnit, (float)vp.x, (float)vp.y, (float)passes.size()};
        params.atlas_rect = {atlas_world_origin.x, atlas_world_origin.y, atlas_world_size.x, atlas_world_size.y};
        params.shadow_flags = {hard_shadows ? 1.0f : 0.0f, penumbra_width, (float)soft_probe_count, 0.0f};
        for (size_t i = 0; i < passes.size() && i < 13; ++i)
            params.pass_data[i] = {passes[i].noise_scale, passes[i].coverage_pct, passes[i].reach, passes[i].weight};
    }
};
