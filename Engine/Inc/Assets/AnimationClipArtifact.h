#pragma once

#include "Animation/AnimationEvent.h"
#include "Animation/Clip.h"
#include "Assets/AssetArtifact.h"
#include "Framework/Math/Guid.h"
#include "Framework/Math/Quaternion.h"
#include "Framework/Math/Vector.hpp"

#include <span>

namespace Ailu
{
    struct AILU_API AnimationClipArtifactDesc
    {
        String _clip_name;
        Guid _preview_mesh_guid = Guid::EmptyGuid();
        u32 _frame_count = 0u;
        f32 _duration = 0.0f;
        f32 _frame_rate = 0.0f;
        f32 _frame_duration = 0.0f;
        bool _is_looping = true;
        u32 _track_count = 0u;
        u32 _sprite_frame_count = 0u;
        u32 _event_count = 0u;
    };

    struct AILU_API AnimationClipTrackArtifact
    {
        u16 _joint_index = 0u;
        u32 _position_offset = 0u;
        u32 _position_count = 0u;
        u32 _rotation_offset = 0u;
        u32 _rotation_count = 0u;
        u32 _scale_offset = 0u;
        u32 _scale_count = 0u;
    };

    struct AILU_API AnimationVectorKeyArtifact
    {
        f32 _time = 0.0f;
        Vector3f _value = Vector3f::kZero;
    };

    struct AILU_API AnimationQuaternionKeyArtifact
    {
        f32 _time = 0.0f;
        Quaternion _value = Quaternion();
    };

    struct AILU_API AnimationSpriteFrameArtifact
    {
        f32 _time = 0.0f;
        Guid _sprite = Guid::EmptyGuid();
    };

    struct AILU_API AnimationEventArtifact
    {
        f32 _time = 0.0f;
        u32 _event_id = 0u;
        EAnimationEventKind _kind = EAnimationEventKind::kCosmetic;
    };

    struct AILU_API AnimationClipArtifact
    {
        AnimationClipArtifactDesc _desc;
        Vector<AnimationClipTrackArtifact> _tracks;
        Vector<AnimationVectorKeyArtifact> _position_keys;
        Vector<AnimationQuaternionKeyArtifact> _rotation_keys;
        Vector<AnimationVectorKeyArtifact> _scale_keys;
        Vector<AnimationSpriteFrameArtifact> _sprite_frames;
        Vector<AnimationEventArtifact> _events;
    };

    AILU_API bool BuildAnimationClipArtifact(const AnimationClip &clip, AnimationClipArtifact &out_artifact);
    AILU_API bool SerializeAnimationClipArtifact(const AnimationClipArtifact &artifact, const AssetArtifactKey &key,
                                                 Vector<u8> &out_data);
    AILU_API bool DeserializeAnimationClipArtifact(std::span<const u8> data, const AssetArtifactKey &key,
                                                   AnimationClipArtifact &out_artifact);
    AILU_API Ref<AnimationClip> CreateAnimationClipFromArtifact(const AnimationClipArtifact &artifact);
}
