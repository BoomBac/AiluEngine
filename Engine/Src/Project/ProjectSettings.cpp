#include "Project/ProjectSettings.h"

#include <algorithm>
#include <format>

namespace Ailu
{
    ProjectSettings::ProjectSettings()
    {
        ResetLayerAndTagDefaults();
        ResetPhysics2DLayerCollisionMatrix();
    }

    void ProjectSettings::ResetLayerAndTagDefaults()
    {
        _layers.resize(kLayerCount);
        for (u32 index = 0u; index < kLayerCount; ++index)
            _layers[index] = std::format("Layer {}", index);
        _layers[0] = "Default";
        _layers[1] = "TransparentFX";
        _layers[2] = "Ignore Raycast";
        _layers[3] = "SkyBox";
        _layers[4] = "Water";
        _layers[5] = "UI";

        _tags = {"Untagged", "MainCamera"};
    }

    void ProjectSettings::EnsureValid()
    {
        if (_layers.size() != kLayerCount)
        {
            Vector<String> old_layers = std::move(_layers);
            Vector<String> old_tags = std::move(_tags);
            ResetLayerAndTagDefaults();
            const u32 copy_count = std::min(static_cast<u32>(old_layers.size()), kLayerCount);
            for (u32 index = 0u; index < copy_count; ++index)
            {
                if (!old_layers[index].empty())
                    _layers[index] = std::move(old_layers[index]);
            }
            if (!old_tags.empty())
                _tags = std::move(old_tags);
        }
        else
        {
            for (u32 index = 0u; index < kLayerCount; ++index)
            {
                if (_layers[index].empty())
                    _layers[index] = std::format("Layer {}", index);
            }
        }

        if (_tags.empty())
            _tags.push_back("Untagged");
        if (!HasTag("Untagged"))
            _tags.insert(_tags.begin(), "Untagged");
        if (_physics_2d_collision_masks.size() != kPhysics2DLayerCount)
            ResetPhysics2DLayerCollisionMatrix();
    }

    const String &ProjectSettings::GetLayerName(u8 layer) const
    {
        static const String s_empty;
        return layer < _layers.size() ? _layers[layer] : s_empty;
    }

    bool ProjectSettings::SetLayerName(u8 layer, const String &name)
    {
        if (layer >= kLayerCount || name.empty())
            return false;
        for (u32 index = 0u; index < _layers.size(); ++index)
        {
            if (index != layer && _layers[index] == name)
                return false;
        }
        if (_layers.size() != kLayerCount)
            EnsureValid();
        _layers[layer] = name;
        return true;
    }

    i32 ProjectSettings::FindLayer(const String &name) const
    {
        for (u32 index = 0u; index < _layers.size(); ++index)
        {
            if (_layers[index] == name)
                return static_cast<i32>(index);
        }
        return -1;
    }

    u32 ProjectSettings::GetLayerMask(const String &name) const
    {
        const i32 layer = FindLayer(name);
        return layer >= 0 ? (1u << static_cast<u32>(layer)) : 0u;
    }

    bool ProjectSettings::HasTag(const String &tag) const
    {
        return std::find(_tags.begin(), _tags.end(), tag) != _tags.end();
    }

    bool ProjectSettings::AddTag(const String &tag)
    {
        if (tag.empty() || HasTag(tag))
            return false;
        _tags.push_back(tag);
        return true;
    }

    bool ProjectSettings::RemoveTag(const String &tag)
    {
        if (tag == "Untagged")
            return false;
        const auto iter = std::find(_tags.begin(), _tags.end(), tag);
        if (iter == _tags.end())
            return false;
        _tags.erase(iter);
        return true;
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
