#include "Framework/Script/ScriptSpriteRenderer.h"

#include "Assets/Asset.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/2D/Sprite.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"

namespace Ailu
{
    namespace
    {
        ECS::SpriteRendererComponent *ResolveSpriteRenderer(SceneManagement::Scene *scene, ECS::Entity entity)
        {
            return scene != nullptr && scene->IsValidEntity(entity) ?
                       scene->GetRegister().GetComponent<ECS::SpriteRendererComponent>(entity) : nullptr;
        }
    }

    bool ScriptSpriteRenderer::IsValid() const { return ResolveSpriteRenderer(_scene, _entity) != nullptr; }
    ScriptAssetValue ScriptSpriteRenderer::GetSprite() const
    {
        const auto *component = ResolveSpriteRenderer(_scene, _entity);
        if (component == nullptr || component->_sprite == nullptr) return ScriptAssetValue::Sprite();
        const Asset *asset = ResourceMgr::Get().GetLinkedAsset(component->_sprite);
        if (asset == nullptr) return ScriptAssetValue::Sprite();
        return {asset->GetGuid(), "Sprite"};
    }
    void ScriptSpriteRenderer::SetSprite(const ScriptAssetValue &sprite) const
    {
        auto *component = ResolveSpriteRenderer(_scene, _entity);
        if (component == nullptr) return;
        if (sprite._guid == Guid::EmptyGuid())
        {
            component->_sprite = nullptr;
            component->_sprite_guid = Guid::EmptyGuid();
            return;
        }
        const Asset *asset = ResourceMgr::Get().GetAsset(ResourceMgr::Get().GuidToAssetPath(sprite._guid));
        if (asset != nullptr)
        {
            component->_sprite = asset->As<Render::Sprite>();
            component->_sprite_guid = sprite._guid;
        }
    }
    bool ScriptSpriteRenderer::IsVisible() const
    {
        const auto *component = ResolveSpriteRenderer(_scene, _entity);
        return component != nullptr && component->_visible;
    }
    void ScriptSpriteRenderer::SetVisible(bool visible) const
    {
        if (auto *component = ResolveSpriteRenderer(_scene, _entity)) component->_visible = visible;
    }
    bool ScriptSpriteRenderer::IsFlipX() const
    {
        const auto *component = ResolveSpriteRenderer(_scene, _entity);
        return component != nullptr && component->_flip_x;
    }
    void ScriptSpriteRenderer::SetFlipX(bool flip_x) const
    {
        if (auto *component = ResolveSpriteRenderer(_scene, _entity)) component->_flip_x = flip_x;
    }
    bool ScriptSpriteRenderer::IsFlipY() const
    {
        const auto *component = ResolveSpriteRenderer(_scene, _entity);
        return component != nullptr && component->_flip_y;
    }
    void ScriptSpriteRenderer::SetFlipY(bool flip_y) const
    {
        if (auto *component = ResolveSpriteRenderer(_scene, _entity)) component->_flip_y = flip_y;
    }
    i32 ScriptSpriteRenderer::GetOrder() const
    {
        const auto *component = ResolveSpriteRenderer(_scene, _entity);
        return component != nullptr ? component->_order_in_layer : 0;
    }
    void ScriptSpriteRenderer::SetOrder(i32 order) const
    {
        if (auto *component = ResolveSpriteRenderer(_scene, _entity)) component->_order_in_layer = order;
    }
    Color ScriptSpriteRenderer::GetColor() const
    {
        const auto *component = ResolveSpriteRenderer(_scene, _entity);
        return component != nullptr ? component->_color : Colors::kWhite;
    }
    void ScriptSpriteRenderer::SetColor(const Color &color) const
    {
        if (auto *component = ResolveSpriteRenderer(_scene, _entity)) component->_color = color;
    }
}
