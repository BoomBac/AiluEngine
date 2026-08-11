#include "pch.h"
#include "Framework/Common/Profiler.h"
#include "Physics/2D/Physics2DSystem.h"

namespace Ailu::ECS
{
    void Physics2DSystem::Update(Register &r, f32 fixed_delta_time)
    {
        PROFILE_BLOCK_CPU("Physics2DSystem::Update")
        _world.Sync(r, _entities);
        _world.Step(fixed_delta_time);
        _world.SyncTransforms(r);
        _world.FlushEvents();
        _world.DebugDraw(r);
    }

    Ref<System> Physics2DSystem::Clone()
    {
        auto copy = MakeRef<Physics2DSystem>();
        copy->_entities = _entities;
        return copy;
    }
}
