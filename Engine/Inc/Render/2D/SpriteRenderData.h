#pragma once
#ifndef __SPRITE_RENDER_DATA_H__
#define __SPRITE_RENDER_DATA_H__

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/ReflectionMacros.h"
#include "Framework/Math/ALMath.hpp"

namespace Ailu::Render
{
    class Texture;

    class Material;

    class Sprite;

    AENUM()
    enum class ESpriteBlendMode
    {
        kAlpha,
        kAdditive,
        kMultiply,
        kOpaque
    };

    inline constexpr u32 kSpriteFlagFlipX = 1u << 0u;
    inline constexpr u32 kSpriteFlagFlipY = 1u << 1u;

    struct SpriteVertex
    {
        Vector2f _position;
        Vector2f _uv;
    };

    static const Vector2f kSpritePositions[] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {0.0f, 1.0f},
        {1.0f, 1.0f}
    };

    static const Vector2f kSpriteUVs[] = {
        {0.0f, 1.0f},
        {1.0f, 1.0f},
        {0.0f, 0.0f},
        {1.0f, 0.0f}
    };

    static u32 kSpriteIndices[] = {
        0u, 1u, 2u,
        1u, 3u, 2u
    };

    struct SpriteRenderData
    {
        Matrix4x4f _local_to_world = Matrix4x4f::Identity();

        Vector4f _uv_rect = Vector4f(0.0f, 0.0f, 1.0f, 1.0f);
        Color _color = Colors::kWhite;

        Vector2f _size = Vector2f::kOne;
        Vector2f _pivot = Vector2f(0.5f, 0.5f);

        Texture *_texture = nullptr;
        // Captured while building the render proxy. The raw pointer above is only a binding key;
        // this snapshot keeps the selected texture version alive through deferred rendering.
        Ref<const Texture> _texture_snapshot;
        Material *_material = nullptr;
        Ref<const Sprite> _sprite_snapshot;
        Ref<const Material> _material_snapshot;

        i16 _sorting_layer = 0;
        i32 _order_in_layer = 0;

        f32 _distance_to_camera = 0.0f;

        u32 _entity_id = 0u;
        ESpriteBlendMode _blend_mode = ESpriteBlendMode::kAlpha;

        bool _flip_x = false;
        bool _flip_y = false;
    };

    struct SpriteInstanceData
    {
        Matrix4x4f _local_to_world;

        Vector4f _uv_rect;
        Vector4f _color;

        Vector4f _size_pivot;

        u32 _texture_index;
        u32 _entity_id;
        u32 _flags;
        u32 _padding;
    };

    struct SpriteBatchKey
    {
        Material *_material = nullptr;
        Texture *_texture = nullptr;
        ESpriteBlendMode _blend_mode = ESpriteBlendMode::kAlpha;

        bool operator==(const SpriteBatchKey &other) const
        {
            return _material == other._material && _texture == other._texture && _blend_mode == other._blend_mode;
        }
    };

    struct SpriteBatch
    {
        SpriteBatchKey _key;

        u32 _instance_offset = 0u;
        u32 _instance_count = 0u;
    };

}// namespace Ailu::Render

#endif// !__SPRITE_RENDER_DATA_H__
