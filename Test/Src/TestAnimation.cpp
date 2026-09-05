#include "Animation/AnimationController.h"
#include "Animation/AnimationEvent.h"
#include "Animation/AnimationKeyReducer.h"
#include "Animation/BlendSpace.h"
#include "Animation/Clip.h"
#include "Animation/SpriteAnimationTrack.h"
#include "Animation/Skeleton/SkeletonAnimationBinding.h"
#include "Animation/TransformTrack.h"
#include "Assets/AnimationClipArtifact.h"
#include "Assets/AssetDocument.h"
#include "Objects/JsonArchive.h"

#include <cmath>
#include <utility>

namespace Ailu::AnimationTests
{
    namespace
    {
        Guid MakeClipId(const char *value)
        {
            return Guid(value);
        }

        AnimationState MakeState(const char *name, const char *clip_id)
        {
            AnimationState state;
            state._name = name;
            state._motion._asset = MakeClipId(clip_id);
            return state;
        }

        bool NearlyEqual(f32 lhs, f32 rhs)
        {
            return std::abs(lhs - rhs) < 0.0001f;
        }

        AnimationClip MakeRootMotionClip()
        {
            AnimationClip clip;
            clip.Duration(1.0f);
            clip.StartTime(0.0f);
            clip.EndTime(1.0f);
            clip.IsLooping(true);
            TransformTrack &track = clip[0u];
            track.GetPositionTrack().Resize(2u);
            track.GetPositionTrack()[0]._time = 0.0f;
            track.GetPositionTrack()[0]._value[0] = 0.0f;
            track.GetPositionTrack()[0]._value[1] = 1.0f;
            track.GetPositionTrack()[0]._value[2] = 0.0f;
            track.GetPositionTrack()[1]._time = 1.0f;
            track.GetPositionTrack()[1]._value[0] = 2.0f;
            track.GetPositionTrack()[1]._value[1] = 3.0f;
            track.GetPositionTrack()[1]._value[2] = 4.0f;
            return clip;
        }

        Skeleton MakeRootSkeleton()
        {
            Skeleton skeleton;
            Joint root;
            root._name = "Root";
            root._parent = Joint::kInvalidJointIndex;
            skeleton.AddJoint(root);
            skeleton.SetBindPoseLocalTransform(0u, Transform(Vector3f::kZero, Quaternion::Identity(),
                                                             Vector3f::kOne));
            return skeleton;
        }
    }

    bool TestRootMotionExtraction()
    {
        AnimationClip clip = MakeRootMotionClip();
        RootMotionSettings settings;
        settings._enabled = true;
        settings._root_bone_index = 0;
        settings._translation_mode = ERootMotionTranslationMode::kXZ;
        settings._rotation_mode = ERootMotionRotationMode::kNone;

        const RootMotionDelta delta = clip.ExtractRootMotion(0.0f, 0.5f, settings, false);
        return NearlyEqual(delta._translation.x, 1.0f) && NearlyEqual(delta._translation.y, 0.0f) &&
               NearlyEqual(delta._translation.z, 2.0f) && delta._rotation == Quaternion::Identity();
    }

    bool TestRootMotionLoopWrap()
    {
        AnimationClip clip = MakeRootMotionClip();
        RootMotionSettings settings;
        settings._enabled = true;
        settings._root_bone_index = 0;
        settings._translation_mode = ERootMotionTranslationMode::kXYZ;
        settings._rotation_mode = ERootMotionRotationMode::kNone;

        const RootMotionDelta delta = clip.ExtractRootMotion(0.98f, 0.02f, settings, true);
        const bool matches = NearlyEqual(delta._translation.x, 0.08f) && NearlyEqual(delta._translation.y, 0.08f) &&
                             NearlyEqual(delta._translation.z, 0.16f);
        return matches;
    }

    bool TestRootMotionBindingModes()
    {
        AnimationClip clip = MakeRootMotionClip();
        RootMotionSettings settings;
        settings._enabled = true;
        settings._root_bone_name = "Root";
        settings._translation_mode = ERootMotionTranslationMode::kXZ;
        settings._rotation_mode = ERootMotionRotationMode::kNone;
        clip.SetRootMotionSettings(settings);
        Skeleton skeleton = MakeRootSkeleton();
        SkeletonAnimationBinding binding;
        binding.Resolve(clip, skeleton);

        AnimationEvaluation evaluation;
        evaluation._root_motion_mode = ERootMotionMode::kApply;
        AnimationSample sample;
        sample._clip = Guid::EmptyGuid();
        sample._time = 0.5f;
        sample._previous_time = 0.0f;
        sample._loop = false;
        sample._has_previous_time = true;
        evaluation.AddSample(sample);
        const AnimationEvaluateResult result = binding.Evaluate(evaluation, skeleton);
        const Transform root = result._pose.GetLocalTransform(0u);
        if (!NearlyEqual(root._position.x, 0.0f) || !NearlyEqual(root._position.y, 2.0f) ||
            !NearlyEqual(root._position.z, 0.0f) || !NearlyEqual(result._root_motion._translation.x, 1.0f) ||
            !NearlyEqual(result._root_motion._translation.y, 0.0f) ||
            !NearlyEqual(result._root_motion._translation.z, 2.0f))
            return false;

        evaluation._root_motion_mode = ERootMotionMode::kExtractOnly;
        const AnimationEvaluateResult extract_only_result = binding.Evaluate(evaluation, skeleton);
        const Transform extract_only_root = extract_only_result._pose.GetLocalTransform(0u);
        return NearlyEqual(extract_only_root._position.x, 1.0f) && NearlyEqual(extract_only_root._position.y, 2.0f) &&
               NearlyEqual(extract_only_root._position.z, 2.0f) &&
               NearlyEqual(extract_only_result._root_motion._translation.x, 1.0f) &&
               NearlyEqual(extract_only_result._root_motion._translation.y, 0.0f) &&
               NearlyEqual(extract_only_result._root_motion._translation.z, 2.0f);
    }

    bool TestBlendSpace2DSampling()
    {
        BlendSpaceAsset blend_space;
        blend_space.Is2D(true);
        blend_space.Samples() = {
            {MakeClipId("00000000-0000-0000-0000-000000000031"), {0.0f, 0.0f}},
            {MakeClipId("00000000-0000-0000-0000-000000000032"), {1.0f, 0.0f}},
            {MakeClipId("00000000-0000-0000-0000-000000000033"), {0.0f, 1.0f}},
            {MakeClipId("00000000-0000-0000-0000-000000000034"), {1.0f, 1.0f}},
        };

        AnimationEvaluation evaluation;
        blend_space.AddSamples({0.5f, 0.5f}, 0.25f, 1.0f, true, evaluation);
        if (evaluation._sample_count != 4u)
            return false;
        for (u8 index = 0u; index < evaluation._sample_count; ++index)
            if (!NearlyEqual(evaluation._samples[index]._weight, 0.25f))
                return false;

        evaluation.Clear();
        blend_space.AddSamples({1.0f, 1.0f}, 0.25f, 1.0f, true, evaluation);
        return evaluation._sample_count == 1u &&
               evaluation._samples[0]._clip == MakeClipId("00000000-0000-0000-0000-000000000034");
    }

    bool TestControllerEntryAndClipEvaluation()
    {
        AnimationControllerAsset asset;
        const u16 idle_state = asset.AddState(MakeState("Idle", "00000000-0000-0000-0000-000000000001"));
        asset.EntryState(idle_state);

        AnimationInstance instance;
        instance.Initialize(asset);
        AnimationController controller(&asset);
        controller.Update(instance, 0.25f);
        const AnimationEvaluation evaluation = controller.Evaluate(instance);

        return evaluation._sample_count == 1u &&
               evaluation._samples[0]._clip == MakeClipId("00000000-0000-0000-0000-000000000001") &&
               NearlyEqual(evaluation._samples[0]._time, 0.25f) &&
               NearlyEqual(evaluation._samples[0]._weight, 1.0f);
    }

    bool TestControllerCrossFadeWeights()
    {
        AnimationControllerAsset asset;
        const u16 idle_state = asset.AddState(MakeState("Idle", "00000000-0000-0000-0000-000000000001"));
        const u16 run_state = asset.AddState(MakeState("Run", "00000000-0000-0000-0000-000000000002"));
        asset.EntryState(idle_state);

        AnimationTransition transition;
        transition._from_state = idle_state;
        transition._to_state = run_state;
        transition._duration = 0.2f;
        asset.AddTransition(std::move(transition));

        AnimationInstance instance;
        instance.Initialize(asset);
        AnimationController controller(&asset);
        controller.Update(instance, 0.1f);
        controller.Update(instance, 0.1f);
        const AnimationEvaluation evaluation = controller.Evaluate(instance);

        return evaluation._sample_count == 2u &&
               NearlyEqual(evaluation._samples[0]._weight, 0.5f) &&
               NearlyEqual(evaluation._samples[1]._weight, 0.5f) &&
               evaluation._samples[0]._clip == MakeClipId("00000000-0000-0000-0000-000000000001") &&
               evaluation._samples[1]._clip == MakeClipId("00000000-0000-0000-0000-000000000002");
    }

    bool TestControllerTriggerConsumption()
    {
        AnimationControllerAsset asset;
        const AnimationParameterId attack = asset.AddParameter("attack", EAnimationParameterType::kTrigger);
        const u16 idle_state = asset.AddState(MakeState("Idle", "00000000-0000-0000-0000-000000000001"));
        const u16 attack_state = asset.AddState(MakeState("Attack", "00000000-0000-0000-0000-000000000002"));
        asset.EntryState(idle_state);

        AnimationTransition transition;
        transition._from_state = kInvalidAnimationState;
        transition._to_state = attack_state;
        transition._duration = 0.0f;
        AnimationCondition condition;
        condition._parameter_index = attack;
        condition._op = EAnimationConditionOp::kTriggered;
        transition._conditions.push_back(condition);
        asset.AddTransition(std::move(transition));

        AnimationInstance instance;
        instance.Initialize(asset);
        instance.SetTrigger(attack);
        AnimationController controller(&asset);
        controller.Update(instance, 0.0f);

        return instance._current_state == attack_state && instance._triggers[attack] == 0u &&
               !instance._in_transition;
    }

    bool TestAnimationEventLoopWrap()
    {
        AnimationClip clip;
        clip.Duration(1.0f);
        clip.IsLooping(true);

        AnimationEvent event;
        event._time = 0.2f;
        event._event_id = 42u;
        clip.AddEvent(event);

        Vector<AnimationEvent> events;
        CollectAnimationEvents(clip, 0.8f, 1.3f, events);

        return events.size() == 1u && events[0]._event_id == 42u && NearlyEqual(events[0]._time, 0.2f);
    }

    bool TestSpriteAnimationTrackSampling()
    {
        SpriteAnimationTrack track;
        const Guid first = MakeClipId("00000000-0000-0000-0000-000000000011");
        const Guid second = MakeClipId("00000000-0000-0000-0000-000000000012");
        track.AddFrame({0.0f, first});
        track.AddFrame({0.5f, second});

        return track.Sample(0.1f, 1.0f, false) == first && track.Sample(0.6f, 1.0f, false) == second &&
               track.Sample(1.2f, 1.0f, true) == first;
    }

    bool TestTrackEndFrameSampling()
    {
        TransformTrack track;
        VectorTrack &position_track = track.GetPositionTrack();
        position_track.Resize(2u);
        position_track[0]._time = 0.0f;
        position_track[0]._value[0] = 0.0f;
        position_track[1]._time = 1.0f;
        position_track[1]._value[0] = 10.0f;

        const Transform bind_transform;
        const Transform near_end = track.Evaluate(bind_transform, 0.75f, false);
        const Transform at_end = track.Evaluate(bind_transform, 1.0f, false);
        const Transform after_end = track.Evaluate(bind_transform, 1.25f, false);
        return NearlyEqual(near_end._position.x, 7.5f) && NearlyEqual(at_end._position.x, 10.0f) &&
               NearlyEqual(after_end._position.x, 10.0f);
    }

    bool TestSparseTransformTrackSemantics()
    {
        Transform reference(Vector3f(1.0f, 2.0f, 3.0f), Quaternion::Identity(), Vector3f::kOne);
        TransformTrack empty_track;
        if (empty_track.IsValid() || empty_track.GetStartTime() != 0.0f || empty_track.GetEndTime() != 0.0f)
            return false;

        TransformTrack track;
        auto position = TrackHelpers::FromVector(Vector3f(4.0f, 5.0f, 6.0f));
        position._time = 0.25f;
        track.GetPositionTrack().Resize(1u);
        track.GetPositionTrack()[0] = position;

        auto rotation = TrackHelpers::FromQuaternion(Quaternion::AngleAxis(0.5f, Vector3f::kUp));
        rotation._time = 0.5f;
        track.GetRotationTrack().Resize(1u);
        track.GetRotationTrack()[0] = rotation;

        auto scale = TrackHelpers::FromVector(Vector3f(2.0f, 3.0f, 4.0f));
        scale._time = 0.75f;
        track.GetScaleTrack().Resize(1u);
        track.GetScaleTrack()[0] = scale;

        TransformTrack position_only_track;
        position_only_track.GetPositionTrack().Resize(1u);
        position_only_track.GetPositionTrack()[0] = position;
        const Transform position_only_value = position_only_track.Evaluate(reference, 10.0f, false);
        if (position_only_value._rotation != reference._rotation || position_only_value._scale != reference._scale)
            return false;

        const Transform value = track.Evaluate(reference, 10.0f, false);
        return track.IsValid() && NearlyEqual(track.GetStartTime(), 0.25f) &&
               NearlyEqual(track.GetEndTime(), 0.75f) && value._position == Vector3f(4.0f, 5.0f, 6.0f) &&
               value._rotation == TrackHelpers::ToQuaternion(rotation) &&
               value._scale == Vector3f(2.0f, 3.0f, 4.0f);
    }

    bool TestSparseAnimationDocumentRoundtrip()
    {
        AnimationClipAssetDocument document;
        document._clip_name = "sparse_roundtrip";
        document._frame_count = 120u;
        document._duration = 3.5f;
        document._frame_rate = 30.0f;
        document._frame_duration = 1.0f / 30.0f;
        document._root_motion._enabled = true;
        document._root_motion._root_bone_name = "Root";
        document._root_motion._translation_mode = ERootMotionTranslationMode::kXYZ;
        document._root_motion._rotation_mode = ERootMotionRotationMode::kFull;

        AnimationClipTrackDocument track;
        track._joint_index = 12u;

        AnimationVectorKeyDocument position_a;
        position_a._time = 0.125f;
        position_a._value = Vector3f(1.0f, 2.0f, 3.0f);
        track._position_keys.push_back(position_a);
        AnimationVectorKeyDocument position_b;
        position_b._time = 1.75f;
        position_b._value = Vector3f(4.0f, 5.0f, 6.0f);
        track._position_keys.push_back(position_b);

        AnimationQuaternionKeyDocument rotation;
        rotation._time = 0.5f;
        rotation._value = Quaternion::AngleAxis(0.75f, Vector3f::kUp);
        track._rotation_keys.push_back(rotation);
        document._tracks.push_back(track);

        JsonArchive save_archive;
        Enum::InitTypeInfo();
        save_archive << document;
        const String json = save_archive.SaveToString();
        if (json.find("_position_keys") == String::npos || json.find("_rotation_keys") == String::npos ||
            json.find("_scale_keys") == String::npos || json.find("\"_frames\"") != String::npos)
            return false;

        JsonArchive load_archive;
        if (!load_archive.LoadFromString(json))
            return false;
        AnimationClipAssetDocument loaded_document;
        load_archive >> loaded_document;

        if (loaded_document._clip_name != document._clip_name ||
            loaded_document._frame_count != document._frame_count ||
            !NearlyEqual(loaded_document._duration, document._duration) || loaded_document._tracks.size() != 1u ||
            !loaded_document._root_motion._enabled || loaded_document._root_motion._root_bone_name != "Root" ||
            loaded_document._root_motion._translation_mode != ERootMotionTranslationMode::kXYZ ||
            loaded_document._root_motion._rotation_mode != ERootMotionRotationMode::kFull)
        {
            return false;
        }

        const AnimationClipTrackDocument &loaded_track = loaded_document._tracks[0];
        if (loaded_track._joint_index != track._joint_index || loaded_track._position_keys.size() != 2u ||
            loaded_track._rotation_keys.size() != 1u || !loaded_track._scale_keys.empty())
            return false;
        if (!NearlyEqual(loaded_track._position_keys[0]._time, position_a._time) ||
            loaded_track._position_keys[0]._value != position_a._value ||
            !NearlyEqual(loaded_track._position_keys[1]._time, position_b._time) ||
            loaded_track._position_keys[1]._value != position_b._value)
            return false;
        const Quaternion &loaded_rotation = loaded_track._rotation_keys[0]._value;
        const bool rotation_matches = NearlyEqual(loaded_track._rotation_keys[0]._time, rotation._time) &&
            NearlyEqual(loaded_rotation.x, rotation._value.x) && NearlyEqual(loaded_rotation.y, rotation._value.y) &&
            NearlyEqual(loaded_rotation.z, rotation._value.z) && NearlyEqual(loaded_rotation.w, rotation._value.w);
        return rotation_matches;
    }

    bool TestSkeletonAssetDocumentRoundtrip()
    {
        SkeletonAssetDocument document;
        SkeletonJointDocument &joint = document._joints.emplace_back();
        joint._name = "Root";
        joint._parent = Joint::kInvalidJointIndex;
        joint._inverse_bind_pose = Matrix4x4f::Identity();
        joint._inverse_bind_pose[0][3] = 1.25f;
        joint._inverse_bind_pose[1][3] = -2.5f;
        joint._bind_local_transform = Transform(
            Vector3f(3.0f, 4.0f, 5.0f), Quaternion::AngleAxis(0.5f, Vector3f::kUp), Vector3f(2.0f, 3.0f, 4.0f));

        JsonArchive save_archive;
        save_archive << document;
        const String json = save_archive.SaveToString();
        if (json.find("\"_inverse_bind_pose\": [") == String::npos ||
            json.find("\"_bind_local_transform\": {") == String::npos)
            return false;

        JsonArchive load_archive;
        if (!load_archive.LoadFromString(json))
            return false;
        SkeletonAssetDocument loaded_document;
        load_archive >> loaded_document;
        if (loaded_document._joints.size() != 1u)
            return false;

        const SkeletonJointDocument &loaded_joint = loaded_document._joints[0];
        if (loaded_joint._name != joint._name || loaded_joint._parent != joint._parent)
            return false;
        for (u32 row = 0u; row < 4u; ++row)
            for (u32 column = 0u; column < 4u; ++column)
                if (!NearlyEqual(loaded_joint._inverse_bind_pose[row][column], joint._inverse_bind_pose[row][column]))
                    return false;
        const Quaternion &loaded_rotation = loaded_joint._bind_local_transform._rotation;
        const Quaternion &rotation = joint._bind_local_transform._rotation;
        return loaded_joint._bind_local_transform._position == joint._bind_local_transform._position &&
               NearlyEqual(loaded_rotation.x, rotation.x) && NearlyEqual(loaded_rotation.y, rotation.y) &&
               NearlyEqual(loaded_rotation.z, rotation.z) && NearlyEqual(loaded_rotation.w, rotation.w) &&
               loaded_joint._bind_local_transform._scale == joint._bind_local_transform._scale;
    }

    bool TestAnimationKeyReduction()
    {
        constexpr u32 key_count = 300u;
        AnimationReductionSettings settings;

        AnimationClip default_clip;
        TransformTrack &default_track = default_clip[0u];
        default_track.GetPositionTrack().Resize(key_count);
        default_track.GetRotationTrack().Resize(key_count);
        default_track.GetScaleTrack().Resize(key_count);
        for (u32 index = 0u; index < key_count; ++index)
        {
            const f32 time = static_cast<f32>(index) / static_cast<f32>(key_count - 1u);
            auto position = TrackHelpers::FromVector(Vector3f::kZero);
            auto rotation = TrackHelpers::FromQuaternion(Quaternion::Identity());
            auto scale = TrackHelpers::FromVector(Vector3f::kOne);
            position._time = time;
            rotation._time = time;
            scale._time = time;
            default_track.GetPositionTrack()[index] = position;
            default_track.GetRotationTrack()[index] = rotation;
            default_track.GetScaleTrack()[index] = scale;
        }
        AnimationKeyReducer::Reduce(default_clip, settings);
        if (default_track.GetPositionTrack().Size() != 0u || default_track.GetRotationTrack().Size() != 0u ||
            default_track.GetScaleTrack().Size() != 0u)
            return false;

        Pose reference_pose(1u);
        const Transform non_default_reference(Vector3f(0.0f, 0.2f, 0.0f), Quaternion::Identity(), Vector3f::kOne);
        reference_pose.SetLocalTransform(0u, non_default_reference);
        AnimationClip reference_clip;
        TransformTrack &reference_track = reference_clip[0u];
        reference_track.GetPositionTrack().Resize(key_count);
        reference_track.GetRotationTrack().Resize(key_count);
        reference_track.GetScaleTrack().Resize(key_count);
        for (u32 index = 0u; index < key_count; ++index)
        {
            const f32 time = static_cast<f32>(index) / static_cast<f32>(key_count - 1u);
            auto position = TrackHelpers::FromVector(non_default_reference._position);
            auto rotation = TrackHelpers::FromQuaternion(non_default_reference._rotation);
            auto scale = TrackHelpers::FromVector(non_default_reference._scale);
            position._time = time;
            rotation._time = time;
            scale._time = time;
            reference_track.GetPositionTrack()[index] = position;
            reference_track.GetRotationTrack()[index] = rotation;
            reference_track.GetScaleTrack()[index] = scale;
        }
        AnimationKeyReducer::Reduce(reference_clip, reference_pose, settings);
        if (reference_track.GetPositionTrack().Size() != 0u || reference_track.GetRotationTrack().Size() != 0u ||
            reference_track.GetScaleTrack().Size() != 0u)
            return false;

        AnimationClip constant_clip;
        TransformTrack &constant_track = constant_clip[0u];
        constant_track.GetPositionTrack().Resize(key_count);
        for (u32 index = 0u; index < key_count; ++index)
        {
            auto position = TrackHelpers::FromVector(Vector3f(0.0f, 0.2f, 0.0f));
            position._time = static_cast<f32>(index) / static_cast<f32>(key_count - 1u);
            constant_track.GetPositionTrack()[index] = position;
        }
        AnimationKeyReducer::Reduce(constant_clip, settings);
        const Transform constant_value = constant_track.Evaluate(Transform(), 0.75f, false);
        if (constant_track.GetPositionTrack().Size() != 1u || !NearlyEqual(constant_value._position.y, 0.2f))
            return false;

        AnimationClip curved_clip;
        TransformTrack &curved_track = curved_clip[0u];
        curved_track.GetPositionTrack().Resize(key_count);
        for (u32 index = 0u; index < key_count; ++index)
        {
            const f32 time = static_cast<f32>(index) / static_cast<f32>(key_count - 1u);
            auto position = TrackHelpers::FromVector(Vector3f(time * time, 0.0f, 0.0f));
            position._time = time;
            curved_track.GetPositionTrack()[index] = position;
        }
        AnimationKeyReducer::Reduce(curved_clip, settings);
        for (u32 index = 0u; index < key_count; ++index)
        {
            const f32 time = static_cast<f32>(index) / static_cast<f32>(key_count - 1u);
            const f32 expected = time * time;
            const f32 actual = curved_track.Evaluate(Transform(), time, false)._position.x;
            if (std::abs(expected - actual) > settings._position_tolerance + 0.00001f)
                return false;
        }
        return curved_track.GetPositionTrack().Size() > 2u;
    }

    bool TestAnimationClipArtifactRoundtrip()
    {
        AnimationClip source;
        source.Name("artifact_roundtrip");
        source.FrameCount(60u);
        source.Duration(2.0f);
        source.FrameRate(30.0f);
        source.FrameDuration(1.0f / 30.0f);
        source.IsLooping(false);
        source.PreviewMeshGuid(MakeClipId("00000000-0000-0000-0000-000000000021"));
        source.GetRootMotionSettings()._enabled = true;
        source.GetRootMotionSettings()._root_bone_name = "Root";
        source.GetRootMotionSettings()._translation_mode = ERootMotionTranslationMode::kXYZ;
        source.GetRootMotionSettings()._rotation_mode = ERootMotionRotationMode::kFull;

        TransformTrack &track = source[7u];
        auto position = TrackHelpers::FromVector(Vector3f(1.0f, 2.0f, 3.0f));
        position._time = 0.25f;
        track.GetPositionTrack().Resize(1u);
        track.GetPositionTrack()[0] = position;
        auto rotation = TrackHelpers::FromQuaternion(Quaternion::AngleAxis(0.5f, Vector3f::kUp));
        rotation._time = 0.5f;
        track.GetRotationTrack().Resize(1u);
        track.GetRotationTrack()[0] = rotation;
        auto scale = TrackHelpers::FromVector(Vector3f(2.0f, 3.0f, 4.0f));
        scale._time = 0.75f;
        track.GetScaleTrack().Resize(1u);
        track.GetScaleTrack()[0] = scale;
        source.SpriteTrack().AddFrame({1.0f, MakeClipId("00000000-0000-0000-0000-000000000022")});
        source.AddEvent({1.5f, 99u, EAnimationEventKind::kCosmetic});

        AssetArtifactKey key;
        key._source_hash = 0x1234u;
        key._importer_version = 1u;
        key._artifact_version = kAnimationClipArtifactVersion;
        AnimationClipArtifact artifact;
        Vector<u8> serialized;
        if (!BuildAnimationClipArtifact(source, artifact) ||
            !SerializeAnimationClipArtifact(artifact, key, serialized))
            return false;

        AnimationClipArtifact loaded_artifact;
        if (!DeserializeAnimationClipArtifact(serialized, key, loaded_artifact))
            return false;
        Ref<AnimationClip> loaded = CreateAnimationClipFromArtifact(loaded_artifact);
        if (loaded == nullptr || loaded->Name() != source.Name() || loaded->Size() != 1u ||
            loaded->GetIdAtIndex(0u) != 7u || loaded->SpriteTrack().Frames().size() != 1u ||
            loaded->Events().size() != 1u || !loaded->GetRootMotionSettings()._enabled ||
            loaded->GetRootMotionSettings()._root_bone_name != "Root" ||
            loaded->GetRootMotionSettings()._translation_mode != ERootMotionTranslationMode::kXYZ ||
            loaded->GetRootMotionSettings()._rotation_mode != ERootMotionRotationMode::kFull)
            return false;

        const TransformTrack &loaded_track = loaded->GetTrackAtIndex(0u);
        const bool matches = NearlyEqual(loaded_track.GetPositionTrack()[0]._time, 0.25f) &&
               NearlyEqual(loaded_track.GetPositionTrack()[0]._value[0], 1.0f) &&
               NearlyEqual(loaded_track.GetRotationTrack()[0]._time, 0.5f) &&
               NearlyEqual(loaded_track.GetScaleTrack()[0]._value[1], 3.0f);
        loaded.reset();
        return matches;
    }
}
