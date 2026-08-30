#include "Animation/AnimationKeyReducer.h"

#include "Animation/Clip.h"
#include "Animation/Pose.h"
#include "Framework/Math/VectorMath.hpp"
#include "pch.h"

namespace Ailu
{
    namespace
    {
        f32 VectorError(const VectorFrame &original, const VectorFrame &predicted)
        {
            return Math::Distance(TrackHelpers::ToVector(original), TrackHelpers::ToVector(predicted));
        }

        f32 RotationError(const QuaternionFrame &original, const QuaternionFrame &predicted)
        {
            const Quaternion original_value = TrackHelpers::ToQuaternion(original);
            const Quaternion predicted_value = TrackHelpers::ToQuaternion(predicted);
            const f32 dot = std::clamp(std::abs(Quaternion::Dot(original_value, predicted_value)), 0.0f, 1.0f);
            return 2.0f * std::acos(dot);
        }

        template<u32 N, typename ErrorFunction>
        void ReduceRange(const Vector<Frame<N>> &source, u32 first, u32 last, f32 tolerance,
                         ErrorFunction error_function, Vector<u32> &kept_indices)
        {
            if (last <= first + 1u)
                return;

            const f32 time_delta = source[last]._time - source[first]._time;
            f32 max_error = -1.0f;
            u32 max_error_index = first;
            for (u32 index = first + 1u; index < last; ++index)
            {
                const f32 t = time_delta > 0.0f ?
                    std::clamp((source[index]._time - source[first]._time) / time_delta, 0.0f, 1.0f) : 0.0f;
                const Frame<N> predicted = TrackHelpers::Lerp(source[first], source[last], t);
                const f32 error = error_function(source[index], predicted);
                if (error > max_error)
                {
                    max_error = error;
                    max_error_index = index;
                }
            }

            if (max_error <= tolerance)
                return;

            ReduceRange(source, first, max_error_index, tolerance, error_function, kept_indices);
            kept_indices.push_back(max_error_index);
            ReduceRange(source, max_error_index, last, tolerance, error_function, kept_indices);
        }

        template<u32 N, typename ErrorFunction>
        void ReduceTrack(Track<Frame<N>, N> &track, const Frame<N> &reference, f32 tolerance,
                         ErrorFunction error_function)
        {
            const u32 source_size = track.Size();
            if (source_size == 0u)
                return;

            Vector<Frame<N>> source;
            source.resize(source_size);
            for (u32 index = 0u; index < source_size; ++index)
                source[index] = track[index];

            bool is_default = true;
            for (const Frame<N> &frame : source)
            {
                if (error_function(frame, reference) > tolerance)
                {
                    is_default = false;
                    break;
                }
            }
            if (is_default)
            {
                track.Resize(0u);
                return;
            }

            if (source_size == 1u)
                return;

            bool is_constant = true;
            for (u32 index = 1u; index < source_size; ++index)
            {
                if (error_function(source[index], source[0]) > tolerance)
                {
                    is_constant = false;
                    break;
                }
            }
            if (is_constant)
            {
                track.Resize(1u);
                track[0] = source[0];
                return;
            }

            Vector<u32> kept_indices;
            kept_indices.reserve(source_size);
            kept_indices.push_back(0u);
            ReduceRange(source, 0u, source_size - 1u, tolerance, error_function, kept_indices);
            kept_indices.push_back(source_size - 1u);

            track.Resize(static_cast<u32>(kept_indices.size()));
            for (u32 index = 0u; index < kept_indices.size(); ++index)
                track[index] = source[kept_indices[index]];
        }

        Frame<3> MakeVectorReference(const Vector3f &value)
        {
            return TrackHelpers::FromVector(value);
        }

        Frame<4> MakeQuaternionReference(const Quaternion &value)
        {
            return TrackHelpers::FromQuaternion(value);
        }

        void ReduceTrack(TransformTrack &track, const Transform &reference,
                         const AnimationReductionSettings &settings)
        {
            ReduceTrack(track.GetPositionTrack(), MakeVectorReference(reference._position),
                        settings._position_tolerance, VectorError);
            ReduceTrack(track.GetRotationTrack(), MakeQuaternionReference(reference._rotation),
                        settings._rotation_tolerance, RotationError);
            ReduceTrack(track.GetScaleTrack(), MakeVectorReference(reference._scale),
                        settings._scale_tolerance, VectorError);
        }
    }

    void AnimationKeyReducer::Reduce(AnimationClip &clip, const AnimationReductionSettings &settings)
    {
        const Transform reference;
        for (u32 index = 0u; index < clip.Size(); ++index)
            ReduceTrack(clip[clip.GetIdAtIndex(index)], reference, settings);
    }

    void AnimationKeyReducer::Reduce(AnimationClip &clip, const Pose &reference_pose,
                                     const AnimationReductionSettings &settings)
    {
        for (u32 index = 0u; index < clip.Size(); ++index)
        {
            const u16 joint_index = clip.GetIdAtIndex(index);
            const Transform reference = joint_index < reference_pose.Size() ?
                reference_pose.GetLocalTransform(joint_index) : Transform();
            ReduceTrack(clip[joint_index], reference, settings);
        }
    }
}
