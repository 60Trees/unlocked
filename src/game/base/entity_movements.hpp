#pragma once

#include <functional>
#include <optional>
#include <utils.hpp>
#include <game/anim.hpp>

namespace Game {
    struct Entity;

    struct EntityMovement {
        virtual ~EntityMovement() = default;

        virtual void tick(Entity* e, double deltaTime) = 0;

        virtual AnimationFrame anim_frame(const Entity*) = 0;

        Direction direction;
        std::optional<Direction> forced_direction = std::nullopt;

        virtual EntityMovement* clone() = 0;

        EntityMovement(const EntityMovement& oth) : direction(oth.direction), forced_direction(oth.forced_direction) {}
        EntityMovement() = default;

        _REGISTERABLE(EntityMovement);
    };

    struct EntityAbility {
        virtual ~EntityAbility() = default;

        virtual bool can_trigger(const Entity* e, double deltaTime) = 0;
        virtual void trigger(Entity* e, double deltaTime) = 0;

        virtual bool does_override(EntityAbility* other) { return false; }

        virtual EntityAbility* clone() = 0;

        EntityAbility(const EntityAbility& oth) {}
        EntityAbility() = default;

        _REGISTERABLE(EntityAbility);
    };

    struct ConditionalEntityAbility : EntityAbility {
        virtual bool can_trigger(const Entity*, double) final override { return true; }
        virtual bool is_active(Entity* e, double deltaTime) = 0;
        virtual void when_active(Entity* e, double deltaTime) = 0;
        virtual void when_deactive(Entity* e, double deltaTime) = 0;
        virtual void trigger(Entity* e, double deltaTime) final override {
            if (is_active(e, deltaTime))
                when_active(e, deltaTime);
            else
                when_deactive(e, deltaTime);
        }
        ConditionalEntityAbility(const ConditionalEntityAbility& oth) : EntityAbility(oth) {}
    };
}  // namespace Game
