#pragma once

#include <utils.hpp>

namespace Game {
    struct Entity;

    struct EntityMovement {
        virtual ~EntityMovement() = default;

        virtual void tick(Entity* e, double deltaTime) = 0;

        _REGISTERABLE(EntityMovement);
    };

    struct EntityAbility {
        virtual ~EntityAbility() = default;

        virtual bool can_trigger(const Entity* e, double deltaTime) = 0;
        virtual void trigger(Entity* e, double deltaTime) = 0;

        virtual bool does_override(EntityAbility* other) { return false; }

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
    };
}  // namespace Game
