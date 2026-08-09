#include "entity.hpp"

using namespace std;
using namespace Base;

void Game::Entity::tick_all(double deltaTime) {
    tick_position(deltaTime);
    tick(deltaTime);
}

void Game::Entity::tick_position(double deltaTime) {
    const MaterialProps& mat = flight_props;
    if (controls.left) data.vel.x -= data.speed * mat.speed * deltaTime;
    if (controls.right) data.vel.x += data.speed * mat.speed * deltaTime;
    if (controls.up) data.vel.y += data.speed * mat.speed * deltaTime;
    if (controls.down) data.vel.y -= data.speed * mat.speed * deltaTime;

    data.pos += data.vel * deltaTime;
    data.vel *= 1.0 - mat.drag * deltaTime;
}
