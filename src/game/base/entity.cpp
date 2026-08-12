#include "entity.hpp"

using namespace std;
using namespace Base;

Game::Entity::vec2_t Game::Entity::get_gravity() { return {0.0, -9}; };

void Game::Entity::tick_all(double deltaTime) {
    tick_position(deltaTime);
    tick(deltaTime);
}

void Game::Entity::tick_position(double deltaTime) {
    const MaterialProps& mat = {
        .drag = {4, 4},
        .speed = 20,
    };
    if (controls.left) data.vel.x -= data.speed * mat.speed * deltaTime;
    if (controls.right) data.vel.x += data.speed * mat.speed * deltaTime;
    if (controls.up) data.vel.y += data.speed * mat.speed * deltaTime;
    if (controls.down) data.vel.y -= data.speed * mat.speed * deltaTime;

    data.pos += data.vel * deltaTime;
    data.vel *= 1.0 - mat.drag * deltaTime;
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
Game::EntityAnim::RenderedOutput Game::EntityAnim::get_rendered(double deltaTime, const Hitbox& hitbox, Base::Renderer& r) const {
    static uint render_data;
    if (render_lambda) return render_lambda(&render_data, deltaTime, hitbox, r);

    const auto worldspace = r.builtin_worldspace_vshader();
    const auto uispace = r.builtin_uispace_vshader();
    const auto textured = r.builtin_textured_pshader();
    const auto coloured = r.builtin_coloured_pshader();

    RenderedOutput output;

    output.material.pixel_shader = coloured;
    output.material.vertex_shader = worldspace;
    output.material.blend_mode = Base::Renderer::Alpha;
    output.vertices.clear();
    Base::Renderer::make_coloured_square(
        {
            .pos = {(float)hitbox.left_edge(), (float)hitbox.top_edge(), (float)hitbox.right_edge(), (float)hitbox.bottom_edge()},
            .colours = {0xffff00ff, 0x00ffffff, 0x00ff00ff, 0x000000ff},
        },
        output.vertices);

    return output;
}

Game::EntityAnim::RenderedOutput Game::EntityAnim::get_rendered(double deltaTime, const Entity& e, Base::Renderer& r) const {
    return get_rendered(deltaTime, e.data, r);
}
