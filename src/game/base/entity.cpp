#include "entity.hpp"
#include <cmath>
#include <game/base/world_handler.hpp>
#include <optional>
#include "game/anim.hpp"
#include "game/anim.hpp"
#include "utils.hpp"

using namespace std;
using namespace Base;

Game::Entity::vec2_t Game::Entity::get_gravity() { return {0.0, -9}; };

void Game::Entity::tick_all(double deltaTime) {
    tick_position(deltaTime);
    tick(deltaTime);
}

void Game::Entity::tick_position(double deltaTime) {
    constexpr bool X_AXIS = 0, Y_AXIS = 1;
    constexpr bool LEFT = 0, RIGHT = 1;

    constexpr double tick_speed_multiplier = 1.0;
    deltaTime *= tick_speed_multiplier;

    WorldHandler& handler = *GetWorldHandler(false);

    {
        vector<EntityAbility*> triggered{};
        for (auto& ability : current_abilities) {
            if (ability->can_trigger(this, deltaTime)) triggered.push_back(ability.get());
        }

        // <AI>
        std::vector<EntityAbility*> final{};

        for (auto* ability : triggered) {
            bool overridden = false;

            for (auto* other : triggered) {
                if (ability != other && other->does_override(ability)) {
                    overridden = true;
                    break;
                }
            }

            if (!overridden) final.push_back(ability);
        }

        for (auto* ability : final) {
            ability->trigger(this, deltaTime);
        }
        // </AI>
    }

    if (current_movement) current_movement->tick(this, deltaTime);

    const auto sweep_axis = [&](bool axis, double edge_before, double edge_after, double range_lo, double range_hi,
                                double& hit_boundary) -> bool {
        const bool is_y = axis == Y_AXIS;
        const bool is_x = axis == X_AXIS;

        for (const auto& level : handler.placed_levels) {
            const auto& cm = handler.collisions[&level.level];

            const auto to_local = [&](double point, bool target_axis) {
                if (target_axis == X_AXIS)
                    return (point - cm.offset.x) / cm.scale;
                else
                    return -(point + cm.offset.y) / cm.scale;
            };
            const auto tile_solid = [&](int ix, int iy) -> bool {
                if (ix < 0 || iy < 0 || ix >= cm.size.x || iy >= cm.size.y) return false;
                return cm.map[ix][iy] == CollisionMap::CollisionType::SOLID;
            };

            const double local_before = to_local(edge_before, axis);
            const double local_after = to_local(edge_after, axis);

            const bool moving_positive_local = local_after > local_before;
            const int i_before = moving_positive_local ? mth::ceil(local_before) - 1 : mth::floor(local_before);
            const int i_after = static_cast<int>(mth::floor(local_after));
            if (i_before == i_after) continue;

            const double local_lo = to_local(range_lo, !axis);
            const double local_hi = to_local(range_hi, !axis);

            const double local_min = mth::min(local_lo, local_hi);
            const double local_max = mth::max(local_lo, local_hi);

            const int perp_lo = mth::floor(local_min);
            const int perp_hi = mth::ceil(local_max) - 1;

            const int step = (i_after > i_before) ? 1 : -1;

            for (int i = i_before + step; i != i_after + step; i += step)
                for (int p = perp_lo; p <= perp_hi; p++) {
                    const bool solid = (axis == 0) ? tile_solid(i, p) : tile_solid(p, i);
                    if (!solid) continue;

                    hit_boundary = cm.offset[axis] + i * cm.scale;
                    if (step < 0) hit_boundary += cm.scale;
                    if (is_y) hit_boundary *= -1;
                    return true;
                }
        }
        return false;
    };

    // ------------------------------------------------------------
    // X movement
    // ------------------------------------------------------------

    constexpr double vel_snap_distance = 0.1;

    bool colliding_left = false, colliding_right = false;

    if (abs(data.vel.x) > vel_snap_distance) {
        const double edge_before = (data.vel.x > 0.0) ? data.right_edge() : data.left_edge();
        const double edge_offset = edge_before - data.pos.x;
        const double new_pos_x = data.pos.x + data.vel.x * deltaTime;
        const double edge_after = edge_before + (new_pos_x - data.pos.x);

        double hit_boundary;
        if (sweep_axis(0, edge_before, edge_after, data.top_edge(), data.bottom_edge(), hit_boundary)) {
            bool log = data.pos.x != hit_boundary - edge_offset;
            data.pos.x = hit_boundary - edge_offset;
            if (data.vel.x > 0)
                colliding_right = true;
            else
                colliding_left = true;
            data.vel.x = 0;
        } else {
            data.pos.x = new_pos_x;
        }
    } else if (data.vel.x != 0)
        data.vel.x = 0;

    data.colliding_with.left.update(deltaTime, colliding_left);
    data.colliding_with.right.update(deltaTime, colliding_right);

    // ------------------------------------------------------------
    // Y movement
    // ------------------------------------------------------------

    bool colliding_up = false, colliding_down = false;

    if (abs(data.vel.y) > vel_snap_distance) {
        const double edge_before = (data.vel.y > 0.0) ? data.top_edge() : data.bottom_edge();
        const double edge_offset = edge_before - data.pos.y;
        const double new_pos_y = data.pos.y + data.vel.y * deltaTime;
        const double edge_after = edge_before + (new_pos_y - data.pos.y);

        double hit_boundary;
        if (sweep_axis(1, edge_before, edge_after, data.left_edge(), data.right_edge(), hit_boundary)) {
            data.pos.y = hit_boundary - edge_offset;
            if (data.vel.y > 0)
                colliding_up = true;
            else
                colliding_down = true;
            data.vel.y = 0.0;
        } else {
            data.pos.y = new_pos_y;
        }
    } else if (data.vel.y != 0)
        data.vel.y = 0;

    // data.vel *= 1.0 - mat.drag * deltaTime;

    data.colliding_with.up.update(deltaTime, colliding_up);
    data.colliding_with.down.update(deltaTime, colliding_down);
}

void Game::ControlData::update(double deltaTime, bool new_pressed) {
    if (new_pressed != pressed)
        time = 0;
    else {
        if (new_pressed)
            time += deltaTime;
        else
            time -= deltaTime;
    }

    pressed = new_pressed;

    charge += (pressed * 2 - 1) * deltaTime;

    if (charge >= upper_charge_limit) charge = upper_charge_limit;
    if (charge <= lower_charge_limit) charge = lower_charge_limit;
}

void Game::Entity::render(Base::Renderer& r, Base::Renderer::VertexLayer& layer, double deltaTime) const {
    constexpr bool solitaire_mode = false;

    const auto worldspace = r.builtin_worldspace_vshader();
    const auto uispace = r.builtin_uispace_vshader();
    const auto textured = r.builtin_textured_pshader();
    const auto coloured = r.builtin_coloured_pshader();
    layer.material.pixel_shader = textured;
    layer.material.vertex_shader = worldspace;
    layer.material.blend_mode = Renderer::Alpha;
    static map<const Entity*, float> anim_frame_index_fmap{};
    static map<const Entity*, AnimationType> previous_anim_map{};

    const AnimationType anim = current_movement->animation_type();

    bool has_changed = false;
    if (!previous_anim_map.contains(this))
        has_changed = true;
    else if (previous_anim_map[this] != anim)
        has_changed = true;
    else if (!anim_frame_index_fmap.contains(this))
        has_changed = true;

    if (has_changed) {
        anim_frame_index_fmap[this] = 0.0;
        previous_anim_map[this] = anim;
std::print(
    "Entity {:p}: anim={}, frame={}\n",
    static_cast<const void*>(this),
    typeid(current_movement).name(),
    anim_frame_index_fmap[this]
);
        //std::print("MOVEMENT CHANGED Entity {:p}: {}\n", static_cast<const void*>(this), typeid(current_movement).name());
    }

    float& anim_frame_index_f = anim_frame_index_fmap[this];

    CASSERT(anim.frames.size() != 0);
    if (anim.flipped_frames) CASSERT(anim.flipped_frames->size() == anim.frames.size());

    if (!solitaire_mode) layer.vertices.clear();

    anim_frame_index_f += 1 * deltaTime;
    if (!anim.should_stop_on_last_frame)
        while (anim_frame_index_f >= anim.frames.size()) anim_frame_index_f -= anim.frames.size();
    else if (anim_frame_index_f > anim.frames.size() - 1)
        anim_frame_index_f = anim.frames.size() - 1;

    uint anim_frame_index = uint(mth::floor(anim_frame_index_f / anim.seconds_per_frame));
    if (!anim.should_stop_on_last_frame)
        anim_frame_index = anim_frame_index % anim.frames.size();
    else
        anim_frame_index = mth::min(anim_frame_index, anim.frames.size() - 1);

    // `facing` needs to be added to Entity and kept updated during tick;
    // this decides whether we need to mirror the base art at all.
    const bool needs_mirror = this->current_movement->direction != anim.sprite_facing;
    const bool use_flipped_art = needs_mirror && anim.flipped_frames.has_value();

    const AnimationFrame& anim_frame = use_flipped_art ? (*anim.flipped_frames)[anim_frame_index] : anim.frames[anim_frame_index];

    // mirror via UV swap only when we don't have dedicated flipped art
    const bool mirror_uv = needs_mirror && !use_flipped_art;

    const glm::vec<2, uint> bottom_middle = anim_frame.bottom_middle.value_or(glm::vec<2, uint>{anim_frame.size.x / 2, 0});

    Renderer::TexturedRectDescriptor rect{};

    // world position: anchor `bottom_middle` (local to the frame) onto data.pos,
    // matching how Hitbox::pos already means "bottom center" for collision purposes
    rect.pos.l = (float)(data.pos.x - bottom_middle.x);
    rect.pos.r = (float)(rect.pos.l + anim_frame.size.x);
    rect.pos.b = (float)(data.pos.y - (int)bottom_middle.y);
    rect.pos.t = (float)(rect.pos.b + anim_frame.size.y);

    const uint16_t uv_l = anim_frame.top_left.x;
    const uint16_t uv_t = anim_frame.top_left.y;
    const uint16_t uv_r = anim_frame.top_left.x + anim_frame.size.x;
    const uint16_t uv_b = anim_frame.top_left.y + anim_frame.size.y;

    rect.uv.l = mirror_uv ? uv_r : uv_l;
    rect.uv.r = mirror_uv ? uv_l : uv_r;
    rect.uv.t = uv_t;
    rect.uv.b = uv_b;
    // rect.uv.l = 0;
    // rect.uv.t = 0;
    // rect.uv.r = 16;
    // rect.uv.b = 16;

    rect.atlas_index = r.getTextureID(anim.tileset);
    // std::print("Atlas index={}\n", rect.atlas_index);
    rect.layer = 0;

    Renderer::make_textured_square(rect, layer.vertices);
}
