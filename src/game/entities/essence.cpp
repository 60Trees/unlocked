/*
 * @file src/game/entities/essence.cpp
 * @author 60Trees_ (github.com/60Trees)
 */
#include <cmath>
#include <game/base/entity.hpp>
#include <iostream>
#include "base/app.hpp"
#include "glm/ext/vector_float2.hpp"
#include "utils.hpp"
#include <game/base/entity_list.hpp>

using namespace Game;
using namespace Base;
using namespace std;

extern "C" double gravity_multiplier() { return 1; }

namespace {
    inline void sanitize(double& v, double fallback = 0.0) {
        if (!std::isfinite(v)) v = fallback;
    }

    inline void sanitize(float& v, float fallback = 0.0f) {
        if (!std::isfinite(v)) v = fallback;
    }

    inline void clamp_magnitude(double& v, double max_abs) {
        if (v > max_abs) v = max_abs;
        if (v < -max_abs) v = -max_abs;
    }
}  // namespace

struct Essence : Entity {
    Essence() { std::cout << "Essence " << this << ": " << *this << std::endl; }

    std::string name() const override { return "Essence"; }

    Hitbox get_defaults() const override { return {{4, 4}}; }

    bool does_render() const override { return true; }

    // 0-360
    float visual_rotation = 0;
    float triangle_side_length = 2;
    float visual_y_offset = 0;

    // in seconds
    float bobbing_sin_offset = 0;

    float orbit_around_radius = 0;
    float orbit_rotation = 0;

    glm::vec2 render_pos{};

    // Smoothed angular position of the orbit.
    float render_orbit_rotation = 0;
    bool render_initialized = false;

    Entity* last_seen_parent = nullptr;

    void spawn(Base::Application& app, const ldtk::Entity* e) override {
        Entity::spawn(app, e);
        bobbing_sin_offset = visualRandom(this, 0, 20);
    }

    void look_for_parent(Base::Application& app) {
        if (stage == LEAVING_ORBIT) {
            for (const auto& [i, e] : app.get<EntityList>())
                if (e->colliding_with(this) && previous_parent == e.get()) return;

            previous_parent = nullptr;
            stage = UNOWNED;
            return;
        }

        if (parent) {
            stage = OWNED;
            return;
        }

        if (stage == OWNED) stage = UNOWNED;

        if (stage != UNOWNED) return;

        for (const auto& [i, e] : app.get<EntityList>())
            if (e->has_attribute("pick_up_triangles") && e->colliding_with(this)) {
                e->adopt(this);
                break;
            }
    }

    void update_orbit_rotation(size_t own_index, size_t tri_count, Base::Application& app, Entity& p) {
        data.pos = p.data.hitbox_center();

        data.pos.x -= data.size.x / 2;
        data.pos.y -= data.size.y / 2;

        orbit_around_radius = max(p.data.size.x, p.data.size.y) * 0.5;

        const float rotation_offset = [&] {
            const float spacing = 360.0f / static_cast<float>(tri_count);
            return spacing * static_cast<float>(own_index);
        }();

        const auto& seconds_since_start = app.get<Base::FpsCounter>().seconds_since_start;

        constexpr float rps = -0.2f;

        orbit_rotation = mth::fmod(rotation_offset + seconds_since_start * (360.0f * rps) + 360.0f, 360.0f);
    }

    enum class Stage : char { OWNED = 'o', LEAVING_ORBIT = 'l', UNOWNED = 'u' } stage = UNOWNED;

    constexpr static Stage OWNED = Stage::OWNED;
    constexpr static Stage LEAVING_ORBIT = Stage::LEAVING_ORBIT;
    constexpr static Stage UNOWNED = Stage::UNOWNED;

    constexpr inline std::string stage_str(Stage s) {
        switch (s) {
            case Stage::OWNED:
                return "OWNED";

            case Stage::LEAVING_ORBIT:
                return "LEAVING_ORBIT";

            case Stage::UNOWNED:
                return "UNOWNED";
        }

        return "INVALID (" + std::to_string(static_cast<int>(s)) + ")";
    }

    ControlData is_owned;
    Entity* previous_parent = nullptr;

    bool has_attribute(std::string_view to_find) const override {
        if (to_find == "triggers") return !parent;

        return Entity::has_attribute(to_find);
    }
    std::string get_default_attributes() const override { return Entity::get_default_attributes() + ",pushed2,"; }

    void tick(Base::Application& app) override {
        Entity::tick(app);

        sanitize(data.pos.x);
        sanitize(data.pos.y);
        sanitize(data.vel.x);
        sanitize(data.vel.y);

        clamp_magnitude(data.vel.x, 1e5);
        clamp_magnitude(data.vel.y, 1e5);

        orbit_around_radius = 0;

        look_for_parent(app);

        if (parent && last_seen_parent && parent != last_seen_parent) {
            render_initialized = false;
        }

        last_seen_parent = parent;

        if (!parent) {
            is_owned.update(app, false);

            data.vel += get_gravity() * app.get<FpsCounter>().deltaTime * gravity_multiplier();

            sanitize(data.vel.x);
            sanitize(data.vel.y);

            return;
        }

        Entity& p = *parent;

        size_t own_index = 0;
        size_t tri_count = 0;
        bool found_self = false;

        for (Entity* sibling : p.children) {
            if (!sibling) continue;

            auto other_triangle = dynamic_cast<Essence*>(sibling);

            if (!other_triangle) continue;

            if (this == sibling) {
                own_index = tri_count;
                found_self = true;
            }

            tri_count++;
        }

        if (!found_self || tri_count == 0) {
            is_owned.update(app, false);
            return;
        }

        update_orbit_rotation(own_index, tri_count, app, p);

        if (stage == OWNED && p.controls.boost.just_pressed() && own_index == 0) {
            p.controls.boost.time = app.get<FpsCounter>().deltaTime;

            stage = LEAVING_ORBIT;
            previous_parent = parent;

            auto dir = p.controls.focusDegrees;
            sanitize(dir);

            const double speed = 300;

            data.pos = p.data.pos;

            const auto dsin = [](double deg) { return std::sin(deg * M_PI / 180.0); };

            const auto dcos = [](double deg) { return std::cos(deg * M_PI / 180.0); };

            glm::vec<2, double> boost = {dsin(dir) * speed, dcos(dir) * speed};

            sanitize(boost.x);
            sanitize(boost.y);

            data.vel = boost;

            sanitize(data.vel.x);
            sanitize(data.vel.y);

            if ((p.data.vel.x >= 0) == (boost.x >= 0))
                p.data.vel.x = boost.x * -0.5;
            else
                p.data.vel.x += boost.x * -0.5;
            if ((p.data.vel.y >= 0) == (boost.y >= 0))
                p.data.vel.y = boost.y * -0.5;
            else
                p.data.vel.y += boost.y * -0.5;

            sanitize(p.data.vel.x);
            sanitize(p.data.vel.y);

            p.disown(this, true);

            // This disown is intentional, not a silent reparent.
            last_seen_parent = nullptr;
        }

        is_owned.update(app, !(!parent));
    }

    void render(Base::Application& app, Base::Renderer::VertexLayer& layer) override {
        auto& r = app.get<Base::Renderer>();
        auto& fps = app.get<Base::FpsCounter>();

        const auto& seconds_since_start = fps.seconds_since_start;

        using namespace Base;

        visual_y_offset = mth::sin((seconds_since_start + bobbing_sin_offset) * 2.5);

        // revolutions per second
        constexpr double rps = 0.5;

        visual_rotation += fps.deltaTime * (360 * rps) * 0.5;

        if (!parent) {
            visual_rotation += (abs(data.vel.x) + abs(data.vel.y)) * fps.deltaTime * 10;
        }

        visual_rotation = mth::fmod(visual_rotation + 360.f, 360.f);

        if (!does_render()) {
            layer.vertices.clear();
            return;
        }

        layer.material.pixel_shader = r.builtin_coloured_pshader();

        layer.material.vertex_shader = r.builtin_worldspace_vshader();

        layer.material.blend_mode = Renderer::Alpha;

        layer.vertices.clear();

        float s = triangle_side_length;
        float h = s * std::sqrt(3.f) / 2.f;

        struct V2 {
            float x, y;
        };

        V2 A = {-s / 2, -h / 3};
        V2 B = {s / 2, -h / 3};
        V2 C = {0, 2 * h / 3};

        float rad = visual_rotation * (M_PI / 180.f);

        float cs = std::cos(rad);
        float sn = std::sin(rad);

        auto rot = [&](V2 p) { return V2{p.x * cs - p.y * sn, p.x * sn + p.y * cs}; };

        /*
         * Orbit
         *
         * Do NOT smooth the Cartesian X/Y offset.
         *
         * Smoothing X/Y directly causes a rotating vector to lose
         * magnitude, which makes the orbit radius shrink.
         *
         * Instead, smooth the ANGLE and reconstruct the position
         * from sin/cos. This guarantees that the orbit radius remains
         * exactly orbit_around_radius.
         */

        float target_angle = orbit_rotation * (M_PI / 180.f);

        if (!render_initialized) {
            render_orbit_rotation = orbit_rotation;
            render_initialized = true;
        }

        /*
         * Find the shortest angular distance between the current
         * rendered angle and the target angle.
         *
         * This avoids the 359 -> 0 degree wraparound causing the
         * triangle to rotate all the way around the circle.
         */
        float angle_difference = orbit_rotation - render_orbit_rotation;

        while (angle_difference > 180.0f) angle_difference -= 360.0f;

        while (angle_difference < -180.0f) angle_difference += 360.0f;

        /*
         * Frame-rate independent exponential smoothing.
         *
         * Even at very high FPS, this approaches the target correctly.
         */
        float smoothing_t = 1.0f - std::exp(-12.0f * std::max(0.0f, static_cast<float>(fps.deltaTime)));

        render_orbit_rotation += angle_difference * smoothing_t;

        render_orbit_rotation = mth::fmod(render_orbit_rotation + 360.0f, 360.0f);

        float orbit_rad = render_orbit_rotation * (M_PI / 180.f);

        float ocs = std::cos(orbit_rad);
        float osn = std::sin(orbit_rad);

        V2 orbit_offset = {orbit_around_radius * ocs, orbit_around_radius * osn};

        V2 parent_center = {float(data.pos.x + data.size.x / 2.f), float(data.pos.y + data.size.y / 2.f + visual_y_offset)};

        /*
         * The radius is now mathematically constant:
         *
         *     length(orbit_offset) == orbit_around_radius
         *
         * regardless of FPS or deltaTime.
         */
        V2 center = {parent_center.x + orbit_offset.x, parent_center.y + orbit_offset.y};

        A = rot(A);
        A.x += center.x;
        A.y += center.y;

        B = rot(B);
        B.x += center.x;
        B.y += center.y;

        C = rot(C);
        C.x += center.x;
        C.y += center.y;

        std::array<Renderer::Vertex, 3> tris{};

        auto set = [&](Renderer::Vertex& v, V2 p) {
            v.pos.world.x = p.x;
            v.pos.world.y = p.y;

            v.shaderdata.rgba_combined = 0xff00ffff;
        };

        set(tris[0], A);
        set(tris[1], B);
        set(tris[2], C);

        for (auto& t : tris) {
            t.pos.world.depth = 0.0f;

            const auto normalize_float = [](float& f) {
                if (isinf(f) || isnan(f)) f = 0.0f;
            };

            normalize_float(t.pos.world.x);
            normalize_float(t.pos.world.y);

            // That's literally everything in Renderer::Vertex.
            // It's all unions.

            layer.vertices.push_back(t);
        }
    }
} _REGISTER_FOR(new Essence(), "Essence", Entity);
