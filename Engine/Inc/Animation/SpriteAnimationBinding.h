#pragma once
#ifndef __SPRITE_ANIMATION_BINDING_H__
#define __SPRITE_ANIMATION_BINDING_H__

#include "Animation/AnimationEvaluation.h"
#include "Animation/Clip.h"
#include "Framework/Core/Containers/Vector.h"

namespace Ailu::ECS
{
    struct SpriteRendererComponent;
}

namespace Ailu
{
    namespace Render
    {
        class Sprite;
    }

    class AILU_API SpriteAnimationBinding
    {
    public:
        void Resolve(const Guid &clip_id, Ref<const AnimationClip> clip);
        const AnimationClip *FindClip(const Guid &clip_id) const;
        void Evaluate(const AnimationEvaluation &evaluation, ECS::SpriteRendererComponent &renderer);
        void Clear();

    private:
        struct ClipBinding
        {
            Guid _clip_id = Guid::EmptyGuid();
            Ref<const AnimationClip> _clip;
        };

        struct SpriteBinding
        {
            Guid _sprite_id = Guid::EmptyGuid();
            Render::Sprite *_sprite = nullptr;
        };

        const ClipBinding *FindClipBinding(const Guid &clip_id) const;
        Render::Sprite *ResolveSprite(const Guid &sprite_id);

        Vector<ClipBinding> _clips;
        Vector<SpriteBinding> _sprites;
    };
}

#endif // __SPRITE_ANIMATION_BINDING_H__
