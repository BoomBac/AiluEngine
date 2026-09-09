#pragma once

#include "Objects/Object.h"
#include "Assets/AssetRef.h"
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
        void SetTexture(const Guid &guid, Ref<Texture2D> texture) { _texture.Set(guid, std::move(texture)); }
        Ref<Texture2D> Texture() const { return _texture.Get(); }
        const AssetRef<Texture2D> &TextureRef() const { return _texture; }

    private:
        AssetRef<Texture2D> _texture;
        Vector<Ref<Sprite>> _sprites;
    };
}
