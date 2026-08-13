#pragma once
#ifndef __PROJECT_SETTINGS_H__
#define __PROJECT_SETTINGS_H__

#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "generated/ProjectSettings.gen.h"

namespace Ailu
{
    ASTRUCT()
    struct AILU_API ProjectSettings
    {
        GENERATED_BODY()

        static constexpr u32 kPhysics2DLayerCount = 32u;
        static constexpr u32 kLayerCount = 32u;

        ProjectSettings();

        void ResetLayerAndTagDefaults();
        void EnsureValid();
        const String &GetLayerName(u8 layer) const;
        bool SetLayerName(u8 layer, const String &name);
        i32 FindLayer(const String &name) const;
        u32 GetLayerMask(const String &name) const;
        bool HasTag(const String &tag) const;
        bool AddTag(const String &tag);
        bool RemoveTag(const String &tag);

        void ResetPhysics2DLayerCollisionMatrix();
        void SetPhysics2DLayerCollision(u8 layer_a, u8 layer_b, bool enabled);
        bool CanPhysics2DLayersCollide(u8 layer_a, u8 layer_b) const;

        APROPERTY()
        Vector<String> _layers;

        APROPERTY()
        Vector<String> _tags;

        APROPERTY()
        Vector<u32> _physics_2d_collision_masks;
    };
}

#endif
