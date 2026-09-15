#pragma once
#ifndef __SHADOW_SYSTEM_H__
#define __SHADOW_SYSTEM_H__

#include "Framework/Common/NonCopyable.h"
#include "Render/RenderingData.h"
#include "Render/Camera.h"

namespace Ailu::SceneManagement
{
    class Scene;
}

namespace Ailu::ECS
{
    struct LightComponent;
}

namespace Ailu::Render
{
    enum class EShadowViewType : u8
    {
        kDirectional,
        kSpot,
        kArea,
        kPoint
    };

    struct ShadowView
    {
        Camera _camera;
        CullResult _cull_result;
        EShadowViewType _type = EShadowViewType::kDirectional;
        u16 _light_index = 0u;
        u16 _target_slice = 0u;
        u16 _shadow_map_index = 0u;
        Matrix4x4f _shadow_matrix;
        Vector3f _light_world_position = Vector3f::kZero;
        f32 _constant_bias = 0.0f;
        f32 _slope_bias = 0.0f;
        f32 _camera_near = 0.01f;
        f32 _camera_far = 1.0f;
    };

    struct DirectionalShadowSet
    {
        EDirectionalShadowMode _mode = EDirectionalShadowMode::kNone;
        u16 _view_begin = 0u;
        u16 _view_count = 0u;
        f32 _shadow_distance = 0.0f;
        Array<Vector4f, RenderConstants::kMaxCascadeShadowMapSplitNum> _splits{};
    };

    struct ShadowSet
    {
        DirectionalShadowSet _directional;
        Vector<ShadowView> _views;
        u8 _additional_shadow_num = 0u;
        u8 _point_shadow_num = 0u;
        bool _valid = false;
    };

    class ShadowSystem final : public NonCopyable
    {
    public:
        ShadowSystem();
        ~ShadowSystem() = default;

        void Prepare(const Camera &consumer_camera, const SceneManagement::Scene &scene,
                     const RenderRequest &request, ShadowSet &out_shadow_set);

        RenderTexture *GetDirectionalShadowMap(EDirectionalShadowMode mode) const;
        RenderTexture *GetAdditionalShadowMap() const { return _additional_shadow_map.get(); }
        RenderTexture *GetPointShadowMap() const { return _point_shadow_map.get(); }

    private:
        void PrepareDirectional(const Camera &consumer_camera, const SceneManagement::Scene &scene,
                                const RenderRequest &request, ShadowSet &shadow_set);
        void PrepareSpot(const SceneManagement::Scene &scene, ShadowSet &shadow_set);
        void PrepareArea(const SceneManagement::Scene &scene, ShadowSet &shadow_set);
        void PreparePoint(const SceneManagement::Scene &scene, ShadowSet &shadow_set);

        void BuildCameraDirectionalShadow(const Camera &camera, const ECS::LightComponent &light,
                                          ShadowSet &shadow_set);
        void BuildProbeDirectionalShadow(const Camera &probe_camera, const ECS::LightComponent &light,
                                         const RenderRequest &request, ShadowSet &shadow_set);

        Ref<RenderTexture> _camera_directional_shadow_map;
        Ref<RenderTexture> _probe_directional_shadow_map;
        Ref<RenderTexture> _additional_shadow_map;
        Ref<RenderTexture> _point_shadow_map;
    };
}

#endif // __SHADOW_SYSTEM_H__
