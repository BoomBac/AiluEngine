#include "Animation/AssetPreviewViewport3D.h"

#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/ResourcePool.h"
#include "Render/Shader.h"
#include "Render/Texture.h"

#include <algorithm>
#include <cmath>

namespace Ailu::Editor
{
    namespace
    {
        constexpr f32 kPreviewFov = 60.0f;
        constexpr f32 kPreviewNearPlane = 0.001f;
        constexpr f32 kPreviewFarPlane = 1000000.0f;
        constexpr f32 kPreviewGridAlpha = 2.0f;
        constexpr f32 kPreviewGridFadeScale = 10.0f;

        bool GetMeshBounds(Render::Mesh *mesh, Vector3f &center, Vector3f &size)
        {
            if (mesh == nullptr || mesh->BoundBox().empty())
                return false;
            const AABB &bounds = mesh->BoundBox()[0];
            center = (bounds._min + bounds._max) * 0.5f;
            size = bounds._max - bounds._min;
            size.x = std::max(size.x, 0.001f);
            size.y = std::max(size.y, 0.001f);
            size.z = std::max(size.z, 0.001f);
            return true;
        }

        bool IsDrawableMesh(Render::Mesh *mesh)
        {
            return mesh != nullptr && mesh->GetVertexBuffer() != nullptr && mesh->SubmeshCount() > 0u &&
                   !mesh->GetIndices(0u).empty();
        }
    }

    bool AssetPreviewViewport3D::SetViewportSize(Vector2f size)
    {
        const u16 width = static_cast<u16>(std::clamp(size.x, 64.0f, 2048.0f));
        const u16 height = static_cast<u16>(std::clamp(size.y, 64.0f, 2048.0f));
        if (_render_width == width && _render_height == height)
            return false;
        _viewport_size = {static_cast<f32>(width), static_cast<f32>(height)};
        _render_width = width;
        _render_height = height;
        _render_texture.reset();
        _render_pending = true;
        return true;
    }

    void AssetPreviewViewport3D::SetMaterial(Render::Material *material)
    {
        _material = material;
        _preview_material.reset();
        if (_material != nullptr)
        {
            _preview_material = _material->CreateInstance();
            auto forward_shader = ResourceMgr::Get().Get<Render::Shader>(L"Shaders/hlsl/forwardlit.alasset");
            if (_preview_material != nullptr && _material->IsStandardLit() && forward_shader != nullptr)
            {
                _preview_material->SetActiveShader(forward_shader);
                _preview_material->SetCullMode(_material->GetCullMode());
            }
        }
        _render_pending = true;
    }

    void AssetPreviewViewport3D::SetMesh(Render::Mesh *mesh)
    {
        if (_mesh == mesh)
        {
            if (mesh == nullptr)
                _has_preview_bounds = false;
            return;
        }
        _mesh = mesh;
        if (_mesh == nullptr)
            _has_preview_bounds = false;
        _camera_initialized = false;
        _render_pending = true;
    }

    void AssetPreviewViewport3D::SetPreviewBounds(const AABB &bounds)
    {
        _preview_bounds = bounds;
        _has_preview_bounds = true;
        _camera_initialized = false;
        _render_pending = true;
    }

    void AssetPreviewViewport3D::SetShowGrid(bool show_grid)
    {
        if (_show_grid == show_grid)
            return;
        _show_grid = show_grid;
        _render_pending = true;
    }

    void AssetPreviewViewport3D::SetWireframe(bool wireframe)
    {
        if (_wireframe == wireframe)
            return;
        _wireframe = wireframe;
        _render_pending = true;
    }

    void AssetPreviewViewport3D::BeginCameraDrag(Vector2f local_position)
    {
        _orbit_controller.BeginOrbit(local_position);
    }

    void AssetPreviewViewport3D::EndCameraDrag()
    {
        _orbit_controller.EndOrbit();
    }

    void AssetPreviewViewport3D::DragCamera(Vector2f local_position)
    {
        const Vector3f old_position = _orbit_controller.GetCameraPosition();
        _orbit_controller.Orbit(local_position);
        if (!(_orbit_controller.GetCameraPosition() == old_position))
            _render_pending = true;
    }

    void AssetPreviewViewport3D::BeginCameraPan(Vector2f local_position)
    {
        _orbit_controller.BeginPan(local_position);
    }

    void AssetPreviewViewport3D::EndCameraPan()
    {
        _orbit_controller.EndPan();
    }

    void AssetPreviewViewport3D::PanCamera(Vector2f local_position)
    {
        const Vector3f old_target = _orbit_controller.GetTarget();
        _orbit_controller.Pan(local_position, _viewport_size, kPreviewFov * k2Radius);
        if (!(_orbit_controller.GetTarget() == old_target))
            _render_pending = true;
    }

    void AssetPreviewViewport3D::ZoomCamera(f32 scroll_delta)
    {
        const Vector3f old_position = _orbit_controller.GetCameraPosition();
        _orbit_controller.Zoom(scroll_delta);
        if (!(_orbit_controller.GetCameraPosition() == old_position))
            _render_pending = true;
    }

    void AssetPreviewViewport3D::ResetCamera()
    {
        _camera_initialized = false;
        _render_pending = true;
    }

    bool AssetPreviewViewport3D::GetCameraRay(Vector2f local_position, Vector3f &origin, Vector3f &direction) const
    {
        if (!_camera_initialized || _viewport_size.x <= 0.0f || _viewport_size.y <= 0.0f)
            return false;

        const f32 ndc_x = 2.0f * local_position.x / _viewport_size.x - 1.0f;
        const f32 ndc_y = 1.0f - 2.0f * local_position.y / _viewport_size.y;
        const f32 aspect = _viewport_size.x / _viewport_size.y;
        const f32 tan_half_fov = std::tan(kPreviewFov * k2Radius * 0.5f);
        const Vector3f forward = _orbit_controller.GetCameraForward();
        const Vector3f right = _orbit_controller.GetCameraRight();
        const Vector3f up = _orbit_controller.GetCameraUp();
        origin = _orbit_controller.GetCameraPosition();
        direction = Normalize(forward + right * (ndc_x * tan_half_fov * aspect) +
                              up * (ndc_y * tan_half_fov));
        return Magnitude(direction) > Math::kFloatEpsilon;
    }

    bool AssetPreviewViewport3D::ProjectWorldPoint(const Vector3f &world_position, Vector2f &screen_position,
                                                   f32 &depth) const
    {
        if (!_camera_initialized || _viewport_size.x <= 0.0f || _viewport_size.y <= 0.0f)
            return false;

        const Vector3f camera_to_point = world_position - _orbit_controller.GetCameraPosition();
        const Vector3f forward = _orbit_controller.GetCameraForward();
        depth = DotProduct(camera_to_point, forward);
        if (depth <= Math::kFloatEpsilon)
            return false;

        const f32 aspect = _viewport_size.x / _viewport_size.y;
        const f32 tan_half_fov = std::tan(kPreviewFov * k2Radius * 0.5f);
        const f32 x = DotProduct(camera_to_point, _orbit_controller.GetCameraRight());
        const f32 y = DotProduct(camera_to_point, _orbit_controller.GetCameraUp());
        screen_position = {
            _viewport_size.x * (0.5f + x / (2.0f * depth * tan_half_fov * aspect)),
            _viewport_size.y * (0.5f - y / (2.0f * depth * tan_half_fov))};
        return true;
    }

    f32 AssetPreviewViewport3D::GetScreenRadius(f32 world_radius, f32 depth) const
    {
        if (world_radius <= 0.0f || depth <= Math::kFloatEpsilon || _viewport_size.y <= 0.0f)
            return 0.0f;
        const f32 tan_half_fov = std::tan(kPreviewFov * k2Radius * 0.5f);
        return _viewport_size.y * world_radius / (depth * tan_half_fov);
    }

    void AssetPreviewViewport3D::Render()
    {
        const bool has_mesh = IsDrawableMesh(_mesh) && !_mesh->BoundBox().empty();
        if (!has_mesh && !_has_preview_bounds)
            return;
        if (_render_width == 0u || _render_height == 0u)
            SetViewportSize(_viewport_size);
        if (_render_texture == nullptr)
            _render_texture = Render::RenderTexture::Create(_render_width, _render_height, "asset_preview_viewport_3d");
        if (_render_texture == nullptr)
            return;

        auto standard_material = Render::Material::s_standard_forward_lit.lock();
        Render::Material *material = _material != nullptr ? _preview_material.get() : standard_material.get();
        if (_wireframe)
        {
            if (_wireframe_material == nullptr)
                _wireframe_material = ResourceMgr::Get().GetRef<Render::Material>(L"Runtime/Material/Wireframe");
            if (_wireframe_material != nullptr)
                material = _wireframe_material.get();
        }
        if (material == nullptr)
            return;

        auto cmd = Render::CommandBufferPool::Get("AssetPreviewViewport3D");
        auto depth = cmd->GetTempRT(_render_width, _render_height, "asset_preview_viewport_3d_depth",
                                    Render::ERenderTargetFormat::kDepth, false, false, false);
        cmd->SetRenderTargetLoadAction(depth, Render::ELoadStoreAction::kClear);
        cmd->SetRenderTargetLoadAction(_render_texture.get(), Render::ELoadStoreAction::kClear);
        cmd->SetRenderTarget(_render_texture.get(), Render::g_pRenderTexturePool->Get(depth));

        const AABB &aabb = has_mesh ? _mesh->BoundBox()[0] : _preview_bounds;
        const Vector3f center = (aabb._min + aabb._max) * 0.5f;
        const Vector3f extents = (aabb._max - aabb._min) * 0.5f;
        const f32 radius = std::max(Magnitude(extents), 0.001f);
        const f32 fov = kPreviewFov * k2Radius;
        if (!_camera_initialized)
        {
            const f32 fit_distance = radius / std::tan(fov * 0.5f) * 1.15f;
            _orbit_controller.SetOrbit(Vector3f::kZero, 0.785398f, 0.35f, fit_distance);
            _camera_initialized = true;
        }

        const f32 aspect = _viewport_size.x / std::max(_viewport_size.y, 1.0f);
        const Vector3f camera_pos = _orbit_controller.GetCameraPosition();
        const Vector3f view_dir = _orbit_controller.GetCameraForward();
        Matrix4x4f view, proj;
        BuildViewMatrixLookToLH(view, camera_pos, view_dir, _orbit_controller.GetCameraUp());
        BuildPerspectiveFovLHMatrix(proj, fov, aspect, kPreviewNearPlane, kPreviewFarPlane);

        Render::CBufferPerCameraData camera_data{};
        camera_data._MatrixV = view;
        camera_data._MatrixP = proj;
        camera_data._MatrixVP = view * proj;
        camera_data._MatrixVP_NoJitter = camera_data._MatrixVP;
        camera_data._CameraPos = Vector4f(camera_pos, 1.0f);
        camera_data._ScreenParams = Vector4f(1.0f / _viewport_size.x, 1.0f / _viewport_size.y,
                                             _viewport_size.x, _viewport_size.y);
        Render::CBufferPerSceneData scene_data{};
        scene_data._DirectionalLights[0]._LightDir = Normalize(Vector3f(-0.45f, -1.0f, -0.65f));
        scene_data._DirectionalLights[0]._LightColor = Vector3f(1.0f, 1.0f, 1.0f);
        scene_data._DirectionalLights[0]._shadowmap_index = -1;
        scene_data._ActiveLightCount.x = 1.0f;
        cmd->SetGlobalBuffer(Render::RenderConstants::kCBufNamePerScene, &scene_data, sizeof(scene_data));
        cmd->SetGlobalTexture("_OcclusionTex", Render::Texture::s_p_default_white);
        cmd->SetGlobalBuffer(Render::RenderConstants::kCBufNamePerCamera, &camera_data, sizeof(camera_data));

        if (_show_grid)
        {
            if (_grid_material == nullptr)
            {
                auto grid_source = ResourceMgr::Get().Get<Render::Material>(L"Runtime/Material/GridPlane");
                if (grid_source != nullptr && grid_source->GetShader() != nullptr)
                {
                    _grid_material = MakeRef<Render::Material>(grid_source->GetShader(), "AssetPreviewViewport3DGrid");
                    _grid_material->SetFloat("_grid_alpha", kPreviewGridAlpha);
                    _grid_material->SetFloat("_grid_fade_scale", kPreviewGridFadeScale);
                }
            }
            auto grid_mesh = Render::Mesh::s_plane.lock();
            if (_grid_material != nullptr && grid_mesh != nullptr)
            {
                _grid_material->SetVector("_grid_axis_mode", Vector4f(0.0f, 0.0f, 0.0f, 0.0f));
                const f32 camera_distance = Magnitude(_orbit_controller.GetCameraPosition() -
                                                      _orbit_controller.GetTarget());
                const f32 grid_size = std::max(1000.0f, std::max(radius * 20.0f, camera_distance * 4.0f));
                const Matrix4x4f grid_matrix = MatrixScale(grid_size, grid_size, grid_size) *
                                                MatrixTranslation(Vector3f(0.0f, -extents.y, 0.0f));
                cmd->DrawMesh(grid_mesh.get(), _grid_material.get(), grid_matrix);
            }
        }

        const Matrix4x4f world_matrix = MatrixTranslation(Vector3f(-center.x, -center.y, -center.z));
        if (has_mesh)
        {
            for (u16 submesh_index = 0u; submesh_index < _mesh->SubmeshCount(); ++submesh_index)
                cmd->DrawMesh(_mesh, material, world_matrix, submesh_index);
        }
        if (_overlay_callback)
            _overlay_callback(cmd.get(), world_matrix, center, extents);

        cmd->ReleaseTempRT(depth);
        Render::GraphicsContext::Get().ExecuteCommandBuffer(cmd);
        Render::CommandBufferPool::Release(cmd);
        _render_pending = false;
    }
}
