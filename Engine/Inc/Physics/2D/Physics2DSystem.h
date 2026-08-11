#pragma once
#ifndef __PHYSICS_2D_SYSTEM_H__
#define __PHYSICS_2D_SYSTEM_H__

#include "Physics/2D/Physics2DWorld.h"
#include "Scene/Entity.h"

namespace Ailu::ECS
{
    class AILU_API Physics2DSystem final : public System
    {
        DECLARE_SYSTEM(Physics2DSystem)

    public:
        void Update(Register &r, f32 fixed_delta_time) final;
        ESystemPhase GetPhase() const final { return ESystemPhase::kPhysics; }

        Ref<System> Clone() final;
        Physics2DWorld &World() { return _world; }

    private:
        Physics2DWorld _world;
    };
}

#endif
