#include "Project/ProjectSettings.h"

namespace Ailu
{
    ProjectSettings::ProjectSettings()
    {
        ResetPhysics2DLayerCollisionMatrix();
    }

    void ProjectSettings::ResetPhysics2DLayerCollisionMatrix()
    {
        _physics_2d_collision_masks.assign(kPhysics2DLayerCount, 0xffffffffu);
    }

    void ProjectSettings::SetPhysics2DLayerCollision(u8 layer_a, u8 layer_b, bool enabled)
    {
        if (layer_a >= kPhysics2DLayerCount || layer_b >= kPhysics2DLayerCount)
            return;
        if (_physics_2d_collision_masks.size() != kPhysics2DLayerCount)
            ResetPhysics2DLayerCollisionMatrix();

        const u32 bit_a = 1u << layer_a;
        const u32 bit_b = 1u << layer_b;
        if (enabled)
        {
            _physics_2d_collision_masks[layer_a] |= bit_b;
            _physics_2d_collision_masks[layer_b] |= bit_a;
        }
        else
        {
            _physics_2d_collision_masks[layer_a] &= ~bit_b;
            _physics_2d_collision_masks[layer_b] &= ~bit_a;
        }
    }

    bool ProjectSettings::CanPhysics2DLayersCollide(u8 layer_a, u8 layer_b) const
    {
        return layer_a < kPhysics2DLayerCount && layer_b < kPhysics2DLayerCount &&
               _physics_2d_collision_masks.size() == kPhysics2DLayerCount &&
               (_physics_2d_collision_masks[layer_a] & (1u << layer_b)) != 0u;
    }
}
