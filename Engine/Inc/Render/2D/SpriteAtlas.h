#pragma once

#include "Objects/Object.h"
#include "Render/2D/Sprite.h"
#include "generated/SpriteAtlas.gen.h"

namespace Ailu::Render
{
    ACLASS()
    class AILU_API SpriteAtlas : public Object
    {
        GENERATED_BODY()

    public:
        SpriteAtlas() = default;
        explicit SpriteAtlas(const String& name) : Object(name) {}

        const Vector<Ref<Sprite>>& Sprites() const { return _sprites; }
        Vector<Ref<Sprite>>& Sprites() { return _sprites; }
        void SetTexture(const Ref<Texture2D>& texture) { _texture = texture; }
        const Ref<Texture2D>& Texture() const { return _texture; }

    private:
        Ref<Texture2D> _texture;
        Vector<Ref<Sprite>> _sprites;
    };
}
