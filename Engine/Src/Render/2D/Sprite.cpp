#include "Render/2D/Sprite.h"

#include "Render/Texture.h"

namespace Ailu::Render
{
    Vector2f Sprite::GetRenderSize() const
    {
        f32 aspect = 1.0f;
        if (_texture && _texture->Width() > 0 && _texture->Height() > 0 && _uv_rect.w > 0.0f)
        {
            const f32 pixel_width = _uv_rect.z * static_cast<f32>(_texture->Width());
            const f32 pixel_height = _uv_rect.w * static_cast<f32>(_texture->Height());
            if (pixel_width > 0.0f && pixel_height > 0.0f)
                aspect = pixel_width / pixel_height;
        }

        return Vector2f(_size * aspect, _size);
    }
}
