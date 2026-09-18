#include "renderer.hpp"
void Base::Renderer::Camera::follow_point(glm::vec<2, double> point) {
    // x += (point.x - x) / 2 * dt * camera_speed;
    // y += (point.y - y) / 2 * dt * camera_speed;
    // if (mth::abs(point.x - x) <= camera_snap_distance) x = point.x;
    // if (mth::abs(point.y - y) <= camera_snap_distance) y = point.y;

    target_x = point.x;
    target_y = point.y;

    // double camera_speed_x = mth::abs(target_x - x);
    // double camera_speed_y = mth::abs(target_y - y);
}
void Base::Renderer::Camera::update_camera(double dt) {
    x += (target_x - x) * dt * fmax(std::abs(target_x - x), 1.0);
    y += (target_y - y) * dt * fmax(std::abs(target_y - y), 1.0);

    using namespace std;

    if (isinf(x) || isnan(x) || isinf(y) || isnan(y)) {
        x = target_x;
        y = target_y;
    }
}
void Base::Renderer::Camera::update_zoom(double dt) {
    _real_zoom += (_target_zoom - _real_zoom) / 2 * dt * zoom_speed;
    if (mth::abs(_target_zoom - _real_zoom) <= zoom_snap_distance) _real_zoom = _target_zoom;
}
