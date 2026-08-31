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

    struct Pass { float noise_scale, noise_pct, fade_dist, weight; };
    std::vector<Pass> passes = {
        {2.0f, 0.20f, 64.0f, 1.0f},
        {0.5f, 0.50f, 64.0f, 1.0f},
        {0.0f, 0.00f,  64.0f, 1.0f},
    };

    struct ParamsCPU {
        glm::vec4 sun_and_cam;
        glm::vec4 px_viewport_count;
        glm::vec4 atlas_rect;
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

@fragment fn fs_main(in: VSOut) -> @location(0) vec4f {
    let base = textureSample(prevTex, prevSamp, in.uv);
    let sun_dir = params[0].xy;
    let cam_pos = params[0].zw;
    let px_per_unit = params[1].x;
    let viewport = params[1].yz;
    let pass_count = u32(params[1].w);
    let atlas_origin = params[2].xy;
    let atlas_size = params[2].zw;

    let ndc = in.uv * 2.0 - vec2f(1.0);
    let world_pos = cam_pos + vec2f(ndc.x, -ndc.y) * (viewport / (2.0 * px_per_unit));

    var shaft: f32 = 0.0;
    var weight_sum: f32 = 0.0;
    let steps: u32 = 24u;
    for (var p: u32 = 0u; p < pass_count; p = p + 1u) {
        let cfg = params[3u + p];
        var acc: f32 = 0.0;
        for (var s: u32 = 0u; s < steps; s = s + 1u) {
            let t = (f32(s) + 0.5) / f32(steps);
            let dist = select(t * 9999.0, t * cfg.z, cfg.z > 0.0);
            let sample_pos = world_pos - sun_dir * dist;
            if (cfg.x > 0.0 && hash12(sample_pos * cfg.x) < cfg.y) { continue; }
            var uv = (sample_pos - atlas_origin) / atlas_size;
            uv.y = 1.0 - uv.y;  // atlas_origin is the bottom edge, texture row 0 is the level's top
            let in_bounds = all(uv >= vec2f(0.0)) && all(uv <= vec2f(1.0));
            // white (1.0) = solid = blocked, so this is already "occlusion", no inversion needed
            let occluded = select(0.0, textureSample(extraTex0, prevSamp, uv).r, in_bounds);
            let falloff = select(1.0, 1.0 - t, cfg.z > 0.0);
            acc += (1.0 - occluded) * falloff;
        }
        shaft += cfg.w * (acc / f32(steps));
        weight_sum += cfg.w;
    }
    shaft = select(0.0, shaft / weight_sum, weight_sum > 0.0);  // normalize instead of raw-summing

    let intensity = 0.7;  // tune to taste — this is now a 0..1 shaft factor, not raw added light

    return vec4f(base.rgb + vec3f(shaft * intensity), base.a);
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
        for (size_t i = 0; i < passes.size() && i < 13; ++i)
            params.pass_data[i] = {passes[i].noise_scale, passes[i].noise_pct, passes[i].fade_dist, passes[i].weight};
    }
};
