/**
 * @file src/game/systems/bloom.hpp
 */

#pragma once
#include <base/renderer.hpp>
#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>
#include <glm/vec4.hpp>

struct BloomSystem {
    float threshold = 0.95f;  /// saturation AND brightness must both exceed this
    float softness = 0.01f;   /// half-width of the threshold's soft edge (0.94..0.96 by default)
    float radius = 5.0f;      /// world units (game pixels)
    float intensity = 2.0f;   /// small bright details blur to a low peak, so this wants to be > 1
    uint32_t taps = 4;       /// cost knob: texture reads per pixel

    struct ParamsCPU {
        glm::vec4 view;   // px_per_unit, viewport.xy, taps
        glm::vec4 bloom;  // threshold, softness, radius (screen px), intensity
    } params{};

    static constexpr std::string_view kPixelShader = R"(
fn bright_mask(c: vec3f) -> f32 {
    let v = max(c.r, max(c.g, c.b));                    // HSV value
    let s = (v - min(c.r, min(c.g, c.b))) / max(v, 1e-4);  // HSV saturation
    let t = params[1].x;
    let w = params[1].y;
    return smoothstep(t - w, t + w, s) * smoothstep(t - w, t + w, v);
}

fn interleaved_gradient_noise(p: vec2f) -> f32 {
    return fract(52.9829189 * fract(dot(p, vec2f(0.06711056, 0.00583715))));
}

@fragment fn fs_main(in: VSOut) -> @location(0) vec4f {
    let base = textureSample(prevTex, prevSamp, in.uv);
    let taps = u32(params[0].w);
    let radius_px = params[1].z;
    let intensity = params[1].w;
    if (taps == 0u || radius_px <= 0.0) { return base; }
    let viewport = params[0].yz;

    // Per-pixel rotation of the spiral: trades ghosting from sparse taps for fine grain
    let rot = interleaved_gradient_noise(in.pos.xy) * 6.2831853;

    var glow = vec3f(0.0);
    var weight_sum = 0.0;
    for (var i: u32 = 0u; i < taps; i = i + 1u) {
        let f = (f32(i) + 0.5) / f32(taps);
        let ang = rot + f32(i) * 2.39996323;  // golden angle
        let off = vec2f(cos(ang), sin(ang)) * (radius_px * sqrt(f));  // uniform over the disc
        let c = textureSampleLevel(prevTex, prevSamp, in.uv + off / viewport, 0.0).rgb;
        let w = exp(-2.0 * f);  // gaussian: r^2 = f * R^2
        glow += c * bright_mask(c) * w;
        weight_sum += w;
    }
    glow = glow / weight_sum;

    return vec4f(base.rgb + glow * intensity, base.a);
}
)";

    void refresh_view(const Base::Renderer& r) {
        const auto vp = r.get_viewport_size();
        const float min_dim = (float)std::min(vp.x, vp.y);
        const float ppu = r.camera._real_zoom > 0 ? min_dim / (float)r.camera._real_zoom : 0.0f;

        params.view = {ppu, (float)vp.x, (float)vp.y, (float)taps};
        params.bloom = {threshold, softness, radius * ppu, intensity};
    }

    void register_post_effect(Base::Renderer& r) {
        r.post_queue.push_back(Base::Renderer::PostEffect{
            .pixel_shader = kPixelShader,
            .params = std::as_bytes(std::span{&params, 1}),
            .pre_render = [this](const Base::Renderer& rr) { refresh_view(rr); },
        });
    }
};
