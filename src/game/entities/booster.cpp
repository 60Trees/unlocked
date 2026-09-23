/**
 * @file src/game/entities/booster.cpp
 * @author 60Trees_ (github.com/60Trees)
 */

#include <game/base/entity.hpp>
#include <game/base/entity_list.hpp>
#include "LDtkLoader/DataTypes.hpp"
#include "LDtkLoader/Entity.hpp"
#include "base/app.hpp"

using namespace Game;
using namespace Base;
using namespace std;

struct Booster : Entity {
    std::string name() const override { return "Booster"; }
    Hitbox get_defaults() const override { return {.size = {12, 12}}; }
    glm::vec<2, double> boost_dir;
    bool invisible = false;

    bool does_render() const override { return !invisible; }
    AnimationFrame get_anim_frame(Base::Application&) const override {
        const auto top_left = [&] -> glm::vec<2, uint> {
            if (boost_dir.y < 0) return {64, 12};
            if (boost_dir.y > 0) return {64, 0};
            if (boost_dir.x < 0) return {76, 0};
            if (boost_dir.x > 0) return {76, 12};
            // in between the four textures so i know
            // if its buggy
            return {70, 5};
        }();
        return {.top_left = top_left, .size = {12, 12}, .tileset = "assets/buttons_n_shi.png", .direction = RIGHT};
    }

    void spawn(Application& app, const ldtk::Entity* e) override {
        Entity::spawn(app, e);
        if (!e) return;

        invisible = e->getField<bool>("Invisible").value();

        const auto& dirname = e->getField<ldtk::FieldType::Enum>("Direction").value().name;
        float boost_amount = 2000;
        if (dirname == "Right") boost_dir = {boost_amount, 0};
        if (dirname == "Left") boost_dir = {-boost_amount, 0};
        if (dirname == "Up") boost_dir = {0, boost_amount};
        if (dirname == "Down") boost_dir = {0, -boost_amount};
    }

    void tick(Application& app) override {
        Entity::tick(app);
        for (auto& [i, e] : app.get<EntityList>()) {
            if (!e) continue;

            bool immediate;
            if (e->has_attribute("pushed2"))
                immediate = true;
            else if (e->has_attribute("pushed1"))
                immediate = false;
            else
                continue;

            if (!colliding_with(e.get())) continue;
            const auto new_vel = boost_dir * app.get<FpsCounter>().deltaTime;
            if (immediate)
                e->data.vel = new_vel * 10.0;
            else
                e->data.vel += new_vel;
        }
    }
} _REGISTER_FOR(new Booster(), "Booster", Entity);
