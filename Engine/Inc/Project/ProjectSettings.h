#pragma once
#ifndef __PROJECT_SETTINGS_H__
#define __PROJECT_SETTINGS_H__

#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/CoreMinimal.h"
#include "generated/ProjectSettings.gen.h"

namespace Ailu
{
    ASTRUCT()
    struct AILU_API ProjectSettings
    {
        GENERATED_BODY()

        static constexpr u32 kPhysics2DLayerCount = 32u;

        ProjectSettings();

        void ResetPhysics2DLayerCollisionMatrix();
        void SetPhysics2DLayerCollision(u8 layer_a, u8 layer_b, bool enabled);
        bool CanPhysics2DLayersCollide(u8 layer_a, u8 layer_b) const;

        APROPERTY()
        Vector<u32> _physics_2d_collision_masks;
    };
}

#endif
