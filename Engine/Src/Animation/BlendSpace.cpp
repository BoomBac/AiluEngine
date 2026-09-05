#include "Animation/BlendSpace.h"

#include <algorithm>

namespace Ailu
{
    BlendSpaceAsset::BlendSpaceAsset() : Object("BlendSpaceAsset")
    {
    }

    BlendSpaceAsset::BlendSpaceAsset(const String &name) : Object(name)
    {
    }

    void BlendSpaceAsset::AddSample(BlendSpaceSample sample)
    {
        const auto iter = std::lower_bound(_samples.begin(), _samples.end(), sample._position.x,
                                           [](const BlendSpaceSample &item, f32 position)
                                           { return item._position.x < position; });
        _samples.insert(iter, std::move(sample));
    }

    void BlendSpaceAsset::AddSamples(f32 position, f32 time, f32 weight, bool loop,
                                     AnimationEvaluation &evaluation, bool normalized_time) const
    {
        if (_samples.empty() || weight <= 0.0f)
            return;

        const auto add_sample = [&](const BlendSpaceSample &sample, f32 sample_weight)
        {
            if (sample_weight <= 0.0f || sample._clip.IsEmpty())
                return;
            evaluation.AddSample(AnimationSample{sample._clip, time, weight * sample_weight, loop, normalized_time});
        };

        if (_samples.size() == 1u || position <= _samples.front()._position.x)
        {
            add_sample(_samples.front(), 1.0f);
            return;
        }
        if (position >= _samples.back()._position.x)
        {
            add_sample(_samples.back(), 1.0f);
            return;
        }

        for (u32 index = 1u; index < _samples.size(); ++index)
        {
            const auto &left = _samples[index - 1u];
            const auto &right = _samples[index];
            if (position > right._position.x)
                continue;

            const f32 range = right._position.x - left._position.x;
            const f32 right_weight = range > 0.0f ? (position - left._position.x) / range : 1.0f;
            add_sample(left, 1.0f - right_weight);
            add_sample(right, right_weight);
            return;
        }
    }

    void BlendSpaceAsset::AddSamples(Vector2f position, f32 time, f32 weight, bool loop,
                                     AnimationEvaluation &evaluation, bool normalized_time) const
    {
        if (!_is_2d)
        {
            AddSamples(position.x, time, weight, loop, evaluation, normalized_time);
            return;
        }
        if (_samples.empty() || weight <= 0.0f)
            return;

        struct Candidate
        {
            u32 _index = 0u;
            f32 _distance_squared = 0.0f;
        };
        Vector<Candidate> candidates;
        candidates.reserve(_samples.size());
        for (u32 index = 0u; index < _samples.size(); ++index)
        {
            const Vector2f delta = _samples[index]._position - position;
            candidates.push_back(Candidate{index, delta.x * delta.x + delta.y * delta.y});
        }
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const Candidate &lhs, const Candidate &rhs)
                         { return lhs._distance_squared < rhs._distance_squared; });

        const u32 candidate_count = std::min<u32>(4u, static_cast<u32>(candidates.size()));
        if (candidates[0]._distance_squared <= Math::kFloatEpsilon)
        {
            const auto &sample = _samples[candidates[0]._index];
            if (!sample._clip.IsEmpty())
                evaluation.AddSample(AnimationSample{sample._clip, time, weight, loop, normalized_time});
            return;
        }

        f32 inverse_distance_sum = 0.0f;
        for (u32 index = 0u; index < candidate_count; ++index)
            inverse_distance_sum += 1.0f / std::max(std::sqrt(candidates[index]._distance_squared), 0.0001f);
        if (inverse_distance_sum <= Math::kFloatEpsilon)
            return;

        for (u32 index = 0u; index < candidate_count; ++index)
        {
            const auto &sample = _samples[candidates[index]._index];
            if (sample._clip.IsEmpty())
                continue;
            const f32 inverse_distance =
                1.0f / std::max(std::sqrt(candidates[index]._distance_squared), 0.0001f);
            evaluation.AddSample(AnimationSample{sample._clip, time,
                                                  weight * inverse_distance / inverse_distance_sum, loop,
                                                  normalized_time});
        }
    }
}
