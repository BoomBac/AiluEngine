#include "Framework/Script/ScriptCamera.h"

#include "Framework/Math/MatrixMath.h"
#include "Framework/Math/Transform.h"
#include "Render/Camera.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"

namespace Ailu
{
    Render::Camera *ScriptCamera::Resolve() const
    {
        if (_scene != nullptr && _scene->IsValidEntity(_entity))
        {
            if (auto *camera = _scene->GetRegister().GetComponent<ECS::CCamera>(_entity)) return &camera->_camera;
            return nullptr;
        }
        return Render::Camera::sMain;
    }
    ECS::TransformComponent *ScriptCamera::ResolveTransform() const
    {
        return _scene != nullptr && _scene->IsValidEntity(_entity) ?
                   _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr;
    }
    void ScriptCamera::SetWorldTransform(const Vector3f &position, const Math::Quaternion &rotation) const
    {
        auto *transform = ResolveTransform();
        if (transform == nullptr) return;
        Matrix4x4f local_matrix = Transform::ToMatrix(Transform(position, rotation, transform->_scale));
        const auto *hierarchy = _scene->GetRegister().GetComponent<ECS::CHierarchy>(_entity);
        if (hierarchy != nullptr && hierarchy->_parent != ECS::kInvalidEntity)
        {
            if (const auto *parent = _scene->GetRegister().GetComponent<ECS::TransformComponent>(hierarchy->_parent))
                local_matrix = local_matrix * Math::MatrixInverse(parent->_world_matrix);
        }
        const Transform local_transform = Transform::FromMatrix(local_matrix);
        transform->SetLocalPosition(local_transform._position);
        transform->SetLocalRotation(local_transform._rotation);
        transform->SetLocalScale(local_transform._scale);
    }
    bool ScriptCamera::IsValid() const { return Resolve() != nullptr; }
    Vector3f ScriptCamera::GetPosition() const
    {
        if (const auto *transform = ResolveTransform()) return transform->_world_dirty ? transform->GetLocalPosition() : transform->GetPosition();
        const auto *camera = Resolve();
        return camera != nullptr ? camera->Position() : Vector3f::kZero;
    }
    void ScriptCamera::SetPosition(const Vector3f &position) const
    {
        if (ResolveTransform() != nullptr) SetWorldTransform(position, GetRotation());
    }
    Math::Quaternion ScriptCamera::GetRotation() const
    {
        if (const auto *transform = ResolveTransform()) return transform->_world_dirty ? transform->GetLocalRotation() : transform->GetRotation();
        const auto *camera = Resolve();
        return camera != nullptr ? camera->Rotation() : Math::Quaternion::Identity();
    }
    void ScriptCamera::SetRotation(const Math::Quaternion &rotation) const
    {
        if (ResolveTransform() != nullptr) SetWorldTransform(GetPosition(), rotation);
    }
    Vector3f ScriptCamera::GetForward() const { const auto *camera = Resolve(); return camera != nullptr ? camera->Forward() : Vector3f::kForward; }
    f32 ScriptCamera::GetFov() const { const auto *camera = Resolve(); return camera != nullptr ? camera->FovH() : 0.0f; }
    void ScriptCamera::SetFov(f32 fov) const { if (auto *camera = Resolve()) { camera->FovH(fov); camera->MarkDirty(); camera->RecalculateMatrix(); } }
    f32 ScriptCamera::GetNearClip() const { const auto *camera = Resolve(); return camera != nullptr ? camera->Near() : 0.0f; }
    void ScriptCamera::SetNearClip(f32 near_clip) const { if (auto *camera = Resolve()) { camera->Near(near_clip); camera->MarkDirty(); camera->RecalculateMatrix(); } }
    f32 ScriptCamera::GetFarClip() const { const auto *camera = Resolve(); return camera != nullptr ? camera->Far() : 0.0f; }
    void ScriptCamera::SetFarClip(f32 far_clip) const { if (auto *camera = Resolve()) { camera->Far(far_clip); camera->MarkDirty(); camera->RecalculateMatrix(); } }
    f32 ScriptCamera::GetAspect() const { const auto *camera = Resolve(); return camera != nullptr ? camera->Aspect() : 0.0f; }
    void ScriptCamera::SetAspect(f32 aspect) const { if (auto *camera = Resolve()) { camera->Aspect(aspect); camera->MarkDirty(); camera->RecalculateMatrix(); } }
    bool ScriptCamera::IsOrthographic() const { const auto *camera = Resolve(); return camera != nullptr && camera->Type() == Render::ECameraType::kOrthographic; }
    void ScriptCamera::SetOrthographic(bool enabled) const
    {
        if (auto *camera = Resolve()) { camera->Type(enabled ? Render::ECameraType::kOrthographic : Render::ECameraType::kPerspective); camera->MarkDirty(); camera->RecalculateMatrix(); }
    }
    f32 ScriptCamera::GetOrthographicSize() const { const auto *camera = Resolve(); return camera != nullptr ? camera->Size() : 0.0f; }
    void ScriptCamera::SetOrthographicSize(f32 size) const { if (auto *camera = Resolve()) { camera->Size(size); camera->MarkDirty(); camera->RecalculateMatrix(); } }
    void ScriptCamera::LookTo(const Vector3f &direction, const Vector3f &up) const
    {
        if (auto *camera = Resolve()) { camera->LookTo(direction, up); SetRotation(camera->Rotation()); }
    }
    Vector2f ScriptCamera::WorldToScreen(const Vector3f &world_position) const { const auto *camera = Resolve(); return camera != nullptr ? camera->WorldToScreen(world_position) : Vector2f::kZero; }
    Vector3f ScriptCamera::ScreenToWorld(const Vector2f &screen_position, f32 depth) const { const auto *camera = Resolve(); return camera != nullptr ? camera->ScreenToWorld(screen_position, depth) : Vector3f::kZero; }
}
