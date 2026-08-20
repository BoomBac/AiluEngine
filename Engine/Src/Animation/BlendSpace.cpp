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
                                     AnimationEvaluation &evaluation) const
    {
        if (_samples.empty() || weight <= 0.0f)
            return;

        const auto add_sample = [&](const BlendSpaceSample &sample, f32 sample_weight)
        {
            if (sample_weight <= 0.0f || sample._clip.IsEmpty())
                return;
            evaluation.AddSample(AnimationSample{sample._clip, time, weight * sample_weight, loop});
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
}
