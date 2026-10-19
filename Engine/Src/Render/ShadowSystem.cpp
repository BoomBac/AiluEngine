#include "Render/ShadowSystem.h"
#include "Framework/Common/Application.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"

#include <array>
#include <cfloat>

namespace
{
    using namespace Ailu;
    using namespace Ailu::Render;

    struct CascadeShadowInfo
    {
        Matrix4x4f _view;
        Matrix4x4f _projection;
        Vector3f _center = Vector3f::kZero;
        f32 _radius = 0.0f;
    };

    void BuildCascadeFrustumCorners(const Camera &camera, f32 cascade_near, f32 cascade_far,
                                    std::array<Vector3f, 8> &corners)
    {
        const f32 tan_half_fov = tanf(camera.FovH() * k2Radius * 0.5f);
        const f32 near_half_height = tan_half_fov * cascade_near;
        const f32 near_half_width = near_half_height * camera.Aspect();
        const f32 far_half_height = tan_half_fov * cascade_far;
        const f32 far_half_width = far_half_height * camera.Aspect();
        const Vector3f near_center = camera.Position() + camera.Forward() * cascade_near;
        const Vector3f far_center = camera.Position() + camera.Forward() * cascade_far;

        corners[0] = near_center + camera.Up() * near_half_height - camera.Right() * near_half_width;
        corners[1] = near_center + camera.Up() * near_half_height + camera.Right() * near_half_width;
        corners[2] = near_center - camera.Up() * near_half_height - camera.Right() * near_half_width;
        corners[3] = near_center - camera.Up() * near_half_height + camera.Right() * near_half_width;
        corners[4] = far_center + camera.Up() * far_half_height - camera.Right() * far_half_width;
        corners[5] = far_center + camera.Up() * far_half_height + camera.Right() * far_half_width;
        corners[6] = far_center - camera.Up() * far_half_height - camera.Right() * far_half_width;
        corners[7] = far_center - camera.Up() * far_half_height + camera.Right() * far_half_width;
    }

    CascadeShadowInfo BuildCascadeShadow(const Camera &camera, const Vector3f &light_direction,
                                         f32 cascade_near, f32 cascade_far, u32 shadow_resolution)
    {
        std::array<Vector3f, 8> corners;
        BuildCascadeFrustumCorners(camera, cascade_near, cascade_far, corners);

        Vector3f center = Vector3f::kZero;
        for (const Vector3f &corner : corners)
            center += corner;
        center /= static_cast<f32>(corners.size());

        f32 radius = 0.0f;
        for (const Vector3f &corner : corners)
            radius = std::max(radius, Distance(center, corner));
        constexpr f32 kRadiusQuantization = 1.0f / 16.0f;
        radius = ceilf(radius / kRadiusQuantization) * kRadiusQuantization;

        Vector3f light_up = Vector3f::kUp;
        if (std::abs(DotProduct(light_direction, light_up)) > 0.99f)
            light_up = Vector3f::kForward;
        const Vector3f light_forward = Normalize(light_direction);
        const Vector3f light_right = Normalize(CrossProduct(light_up, light_forward));
        const Vector3f light_corrected_up = CrossProduct(light_forward, light_right);
        Matrix4x4f light_view;
        BuildViewMatrixLookToLH(light_view, Vector3f::kZero, light_forward, light_corrected_up);

        Vector3f center_ls = TransformCoord(light_view, center);
        const f32 world_units_per_texel = radius * 2.0f / static_cast<f32>(shadow_resolution);
        center_ls.x = std::round(center_ls.x / world_units_per_texel) * world_units_per_texel;
        center_ls.y = std::round(center_ls.y / world_units_per_texel) * world_units_per_texel;

        f32 min_z = FLT_MAX;
        f32 max_z = -FLT_MAX;
        for (const Vector3f &corner : corners)
        {
            const Vector3f corner_ls = TransformCoord(light_view, corner);
            min_z = std::min(min_z, corner_ls.z);
            max_z = std::max(max_z, corner_ls.z);
        }
        constexpr f32 kCasterDepthMargin = 20.0f;
        min_z -= kCasterDepthMargin;
        max_z += kCasterDepthMargin;

        Matrix4x4f projection;
        BuildOrthographicMatrix(projection, center_ls.x - radius, center_ls.x + radius, center_ls.y + radius,
                                center_ls.y - radius, min_z, max_z);
        return CascadeShadowInfo{light_view, projection,
                                 light_right * center_ls.x + light_corrected_up * center_ls.y +
                                     light_forward * center_ls.z,
                                 radius};
    }

    CascadeShadowInfo BuildProbeShadow(const Vector3f &probe_position, const Vector3f &light_direction,
                                       f32 shadow_distance, u32 shadow_resolution)
    {
        const f32 radius = std::max(shadow_distance, 0.1f);
        Vector3f light_up = Vector3f::kUp;
        if (std::abs(DotProduct(light_direction, light_up)) > 0.99f)
            light_up = Vector3f::kForward;
        const Vector3f light_forward = Normalize(light_direction);
        const Vector3f light_right = Normalize(CrossProduct(light_up, light_forward));
        const Vector3f corrected_up = CrossProduct(light_forward, light_right);

        Matrix4x4f light_view;
        BuildViewMatrixLookToLH(light_view, Vector3f::kZero, light_forward, corrected_up);
        Vector3f center_ls = TransformCoord(light_view, probe_position);
        const f32 world_units_per_texel = radius * 2.0f / static_cast<f32>(shadow_resolution);
        center_ls.x = std::round(center_ls.x / world_units_per_texel) * world_units_per_texel;
        center_ls.y = std::round(center_ls.y / world_units_per_texel) * world_units_per_texel;

        constexpr f32 kCasterDepthMargin = 20.0f;
        const f32 min_z = center_ls.z - radius - kCasterDepthMargin;
        const f32 max_z = center_ls.z + radius + kCasterDepthMargin;
        Matrix4x4f projection;
        BuildOrthographicMatrix(projection, center_ls.x - radius, center_ls.x + radius, center_ls.y + radius,
                                center_ls.y - radius, min_z, max_z);
        return CascadeShadowInfo{light_view, projection,
                                 light_right * center_ls.x + corrected_up * center_ls.y + light_forward * center_ls.z,
                                 radius};
    }

    u32 ShadowCasterLayer()
    {
        return Application::Get().NameToLayer("ShadowCaster")._value;
    }

    f32 ShadowBias(f32 value)
    {
        constexpr f32 kShadowBiasFactor = 0.01f;
        return value * value * kShadowBiasFactor;
    }
}

namespace Ailu::Render
{
    ShadowSystem::ShadowSystem()
    {
        const u32 camera_shadow_resolution = QuailtySetting::s_cascade_shaodw_map_resolution;
        _camera_directional_shadow_map = RenderTexture::Create(
            static_cast<u16>(camera_shadow_resolution), static_cast<u16>(camera_shadow_resolution),
            RenderConstants::kMaxCascadeShadowMapSplitNum, "_CameraDirectionalShadowMap",
            ERenderTargetFormat::kShadowMap);
        _probe_directional_shadow_map = RenderTexture::Create(
            static_cast<u16>(QuailtySetting::s_probe_directional_shadow_map_resolution),
            static_cast<u16>(QuailtySetting::s_probe_directional_shadow_map_resolution), 1u,
            "_ProbeDirectionalShadowMap", ERenderTargetFormat::kShadowMap);

        const u16 additional_shadow_resolution = static_cast<u16>(camera_shadow_resolution >> 1u);
        _additional_shadow_map = RenderTexture::Create(
            additional_shadow_resolution, additional_shadow_resolution,
            RenderConstants::kMaxSpotLightNum + RenderConstants::kMaxAreaLightNum,
            "_AdditionalShadowMap", ERenderTargetFormat::kShadowMap);
        _point_shadow_map = RenderTexture::Create(
            additional_shadow_resolution, "_PointShadowMap", ERenderTargetFormat::kShadowMap,
            RenderConstants::kMaxPointShadowLightNum);

        if (_camera_directional_shadow_map != nullptr)
            _camera_directional_shadow_map->_store_action = ELoadStoreAction::kClear;
        if (_probe_directional_shadow_map != nullptr)
            _probe_directional_shadow_map->_store_action = ELoadStoreAction::kClear;
        if (_additional_shadow_map != nullptr)
            _additional_shadow_map->_store_action = ELoadStoreAction::kClear;
        if (_point_shadow_map != nullptr)
            _point_shadow_map->_store_action = ELoadStoreAction::kClear;
    }

    RenderTexture *ShadowSystem::GetDirectionalShadowMap(EDirectionalShadowMode mode) const
    {
        return mode == EDirectionalShadowMode::kSingleOrthographic
            ? _probe_directional_shadow_map.get()
            : _camera_directional_shadow_map.get();
    }

    void ShadowSystem::Prepare(const Camera &consumer_camera, const SceneManagement::Scene &scene,
                               const RenderRequest &request, ShadowSet &out_shadow_set)
    {
        out_shadow_set = {};
        if (!request._enable_shadows)
            return;

        PrepareDirectional(consumer_camera, scene, request, out_shadow_set);
        PrepareSpot(scene, out_shadow_set);
        PrepareArea(scene, out_shadow_set);
        PreparePoint(scene, out_shadow_set);
        out_shadow_set._valid = !out_shadow_set._views.empty();
    }

    void ShadowSystem::PrepareDirectional(const Camera &consumer_camera, const SceneManagement::Scene &scene,
                                          const RenderRequest &request, ShadowSet &shadow_set)
    {
        for (const auto &light : scene.GetRegister().View<ECS::LightComponent>())
        {
            if (light._type != ECS::ELightType::kDirectional)
                continue;
            if (light._shadow._is_cast_shadow)
            {
                if (request._purpose == ERenderPurpose::kCamera)
                    BuildCameraDirectionalShadow(consumer_camera, light, shadow_set);
                else
                    BuildProbeDirectionalShadow(consumer_camera, light, request, shadow_set);
            }
            break;
        }
    }

    void ShadowSystem::PrepareSpot(const SceneManagement::Scene &scene, ShadowSet &shadow_set)
    {
        const u32 shadow_cast_layer = ShadowCasterLayer();
        u16 spot_light_index = 0u;
        for (const auto &light : scene.GetRegister().View<ECS::LightComponent>())
        {
            if (light._type != ECS::ELightType::kSpot)
                continue;
            if (spot_light_index >= RenderConstants::kMaxSpotLightNum)
                break;
            if (light._shadow._is_cast_shadow && shadow_set._additional_shadow_num <
                RenderConstants::kMaxSpotLightNum + RenderConstants::kMaxAreaLightNum)
            {
                ShadowView view;
                view._type = EShadowViewType::kSpot;
                view._light_index = spot_light_index;
                view._target_slice = shadow_set._additional_shadow_num++;
                view._camera_far = light._light._range * 1.5f;
                view._light_world_position = light._light._light_pos.xyz;
                view._constant_bias = ShadowBias(light._shadow._constant_bias);
                view._slope_bias = ShadowBias(light._shadow._slope_bias);
                view._camera.SetLens(90.0f, 1.0f, view._camera_near, view._camera_far);
                view._camera.Position(view._light_world_position);
                view._camera.LookTo(light._light._light_dir.xyz, Vector3f::kUp);
                view._camera._layer_mask = shadow_cast_layer;
                view._shadow_matrix = view._camera.GetView() * view._camera.GetProj();
                shadow_set._views.emplace_back(std::move(view));
            }
            ++spot_light_index;
        }
    }

    void ShadowSystem::PrepareArea(const SceneManagement::Scene &scene, ShadowSet &shadow_set)
    {
        const u32 shadow_cast_layer = ShadowCasterLayer();
        u16 area_light_index = 0u;
        for (const auto &light : scene.GetRegister().View<ECS::LightComponent>())
        {
            if (light._type != ECS::ELightType::kArea)
                continue;
            if (light._shadow._is_cast_shadow && shadow_set._additional_shadow_num <
                RenderConstants::kMaxSpotLightNum + RenderConstants::kMaxAreaLightNum)
            {
                ShadowView view;
                view._type = EShadowViewType::kArea;
                view._light_index = area_light_index;
                view._target_slice = shadow_set._additional_shadow_num++;
                view._camera_far = light._light._range * 1.5f;
                view._light_world_position = light._light._light_pos.xyz;
                view._constant_bias = ShadowBias(light._shadow._constant_bias);
                view._slope_bias = ShadowBias(light._shadow._slope_bias);
                view._camera.SetLens(90.0f, 1.0f, view._camera_near, view._camera_far);
                view._camera.Position(view._light_world_position);
                view._camera.LookTo(light._light._light_dir.xyz, Vector3f::kUp);
                view._camera._layer_mask = shadow_cast_layer;
                view._shadow_matrix = view._camera.GetView() * view._camera.GetProj();
                shadow_set._views.emplace_back(std::move(view));
            }
            ++area_light_index;
        }
    }

    void ShadowSystem::PreparePoint(const SceneManagement::Scene &scene, ShadowSet &shadow_set)
    {
        static const Vector3f kTargets[] =
        {
            {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
            {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}
        };
        static const Vector3f kUps[] =
        {
            {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f},
            {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}
        };
        const u32 shadow_cast_layer = ShadowCasterLayer();
        u16 point_light_index = 0u;
        u16 point_shadow_map_index = 0u;
        for (const auto &light : scene.GetRegister().View<ECS::LightComponent>())
        {
            if (light._type != ECS::ELightType::kPoint)
                continue;
            if (point_light_index >= RenderConstants::kMaxPointLightNum)
                break;
            if (light._shadow._is_cast_shadow && point_shadow_map_index < RenderConstants::kMaxPointShadowLightNum)
            {
                const f32 camera_far = light._light._range * 1.5f;
                for (u16 face = 0u; face < 6u; ++face)
                {
                    ShadowView view;
                    view._type = EShadowViewType::kPoint;
                    view._light_index = point_light_index;
                    view._target_slice = face;
                    view._shadow_map_index = point_shadow_map_index;
                    view._camera_far = camera_far;
                    view._light_world_position = light._light._light_pos.xyz;
                    view._constant_bias = light._shadow._constant_bias;
                    view._slope_bias = light._shadow._slope_bias;
                    view._camera.SetLens(90.0f, 1.0f, view._camera_near, camera_far);
                    view._camera.Position(view._light_world_position);
                    view._camera.LookTo(kTargets[face], kUps[face]);
                    view._camera._layer_mask = shadow_cast_layer;
                    view._shadow_matrix = view._camera.GetView() * view._camera.GetProj();
                    shadow_set._views.emplace_back(std::move(view));
                }
                ++shadow_set._point_shadow_num;
                ++point_shadow_map_index;
            }
            ++point_light_index;
        }
    }

    void ShadowSystem::BuildCameraDirectionalShadow(const Camera &camera, const ECS::LightComponent &light,
                                                     ShadowSet &shadow_set)
    {
        const u16 cascade_count = std::min<u16>(QuailtySetting::s_cascade_shadow_map_count,
                                                 RenderConstants::kMaxCascadeShadowMapSplitNum);
        shadow_set._directional._mode = EDirectionalShadowMode::kCsm;
        shadow_set._directional._view_begin = static_cast<u16>(shadow_set._views.size());
        shadow_set._directional._view_count = cascade_count;
        shadow_set._directional._shadow_distance = QuailtySetting::s_main_light_shaodw_distance;
        f32 cascade_start = 0.0f;
        const u32 shadow_layer = ShadowCasterLayer();
        for (u16 cascade_index = 0u; cascade_index < cascade_count; ++cascade_index)
        {
            const f32 cascade_end = QuailtySetting::s_main_light_shaodw_distance *
                                    QuailtySetting::s_cascade_shadow_map_split[cascade_index];
            const CascadeShadowInfo shadow = BuildCascadeShadow(camera, light._light._light_dir.xyz, cascade_start,
                                                                cascade_end, QuailtySetting::s_cascade_shaodw_map_resolution);
            ShadowView view;
            view._type = EShadowViewType::kDirectional;
            view._light_index = 0u;
            view._target_slice = cascade_index;
            view._constant_bias = ShadowBias(light._shadow._constant_bias);
            view._slope_bias = ShadowBias(light._shadow._slope_bias);
            view._camera.SetViewAndProj(shadow._view, shadow._projection);
            view._camera._layer_mask = shadow_layer;
            view._shadow_matrix = shadow._view * shadow._projection;
            shadow_set._directional._splits[cascade_index] = Vector4f(shadow._center, shadow._radius * shadow._radius);
            shadow_set._views.emplace_back(std::move(view));
            cascade_start = cascade_end;
        }
    }

    void ShadowSystem::BuildProbeDirectionalShadow(const Camera &probe_camera, const ECS::LightComponent &light,
                                                   const RenderRequest &request, ShadowSet &shadow_set)
    {
        const f32 shadow_distance = std::max(request._probe_shadow_distance, 0.1f);
        const CascadeShadowInfo shadow = BuildProbeShadow(probe_camera.Position(), light._light._light_dir.xyz,
                                                          shadow_distance,
                                                          QuailtySetting::s_probe_directional_shadow_map_resolution);
        ShadowView view;
        view._type = EShadowViewType::kDirectional;
        view._light_index = 0u;
        view._target_slice = 0u;
        view._constant_bias = ShadowBias(light._shadow._constant_bias);
        view._slope_bias = ShadowBias(light._shadow._slope_bias);
        view._camera.SetViewAndProj(shadow._view, shadow._projection);
        view._camera._layer_mask = ShadowCasterLayer();
        view._shadow_matrix = shadow._view * shadow._projection;
        shadow_set._directional._mode = EDirectionalShadowMode::kSingleOrthographic;
        shadow_set._directional._view_begin = static_cast<u16>(shadow_set._views.size());
        shadow_set._directional._view_count = 1u;
        shadow_set._directional._shadow_distance = shadow_distance;
        shadow_set._directional._splits[0] = Vector4f(shadow._center, shadow._radius * shadow._radius);
        shadow_set._views.emplace_back(std::move(view));
    }
}
