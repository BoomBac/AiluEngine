#include "Animation/SpriteAnimationBinding.h"

#include "Framework/Common/ResourceMgr.h"
#include "Render/2D/Sprite.h"
#include "Scene/Component.h"

namespace Ailu
{
    void SpriteAnimationBinding::Resolve(const Guid &clip_id, const AnimationClip &clip)
    {
        for (auto &binding : _clips)
        {
            if (binding._clip_id == clip_id)
            {
                binding._clip = &clip;
                return;
            }
        }
        _clips.push_back(ClipBinding{clip_id, &clip});
    }

    const AnimationClip *SpriteAnimationBinding::FindClip(const Guid &clip_id) const
    {
        const ClipBinding *binding = FindClipBinding(clip_id);
        return binding != nullptr ? binding->_clip : nullptr;
    }

    void SpriteAnimationBinding::Evaluate(const AnimationEvaluation &evaluation, ECS::SpriteRendererComponent &renderer)
    {
        const ClipBinding *clip_binding = nullptr;
        const AnimationSample *dominant_sample = nullptr;
        f32 dominant_weight = 0.0f;
        for (u8 sample_index = 0u; sample_index < evaluation._sample_count; ++sample_index)
        {
            const auto &sample = evaluation._samples[sample_index];
            if (sample._weight <= dominant_weight)
                continue;
            const ClipBinding *candidate = FindClipBinding(sample._clip);
            if (candidate == nullptr || candidate->_clip == nullptr || candidate->_clip->SpriteTrack().Empty())
                continue;
            clip_binding = candidate;
            dominant_sample = &sample;
            dominant_weight = sample._weight;
        }
        if (clip_binding == nullptr || dominant_sample == nullptr)
            return;

        const AnimationClip &clip = *clip_binding->_clip;
        const Guid sprite_id = clip.SpriteTrack().Sample(dominant_sample->_time, clip.Duration(), clip.IsLooping());
        if (!sprite_id.IsEmpty())
            if (Render::Sprite *sprite = ResolveSprite(sprite_id); sprite != nullptr)
                renderer._sprite = sprite;
    }

    void SpriteAnimationBinding::Clear()
    {
        _clips.clear();
        _sprites.clear();
    }

    const SpriteAnimationBinding::ClipBinding *SpriteAnimationBinding::FindClipBinding(const Guid &clip_id) const
    {
        for (const auto &binding : _clips)
            if (binding._clip_id == clip_id)
                return &binding;
        return nullptr;
    }

    Render::Sprite *SpriteAnimationBinding::ResolveSprite(const Guid &sprite_id)
    {
        for (const auto &binding : _sprites)
            if (binding._sprite_id == sprite_id)
                return binding._sprite;

        ResourceMgr::Get().Load<Render::Sprite>(sprite_id);
        Render::Sprite *sprite = ResourceMgr::Get().Get<Render::Sprite>(sprite_id);
        _sprites.push_back(SpriteBinding{sprite_id, sprite});
        return sprite;
    }
}
