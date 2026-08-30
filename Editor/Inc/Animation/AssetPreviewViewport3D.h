#pragma once

#include "Common/CameraControllers.h"
#include "Framework/Core/SmartPtr.h"
#include "Framework/Math/ALMath.hpp"
#include "Framework/Math/Geometry.h"

#include <functional>

namespace Ailu::Render
{
    class CommandBuffer;
    class Material;
    class Mesh;
    class RenderTexture;
}

namespace Ailu::Editor
{
    class AssetPreviewViewport3D
    {
    public:
        using OverlayCallback = std::function<void(Render::CommandBuffer *, const Matrix4x4f &, const Vector3f &,
                                                   const Vector3f &)>;

        bool SetViewportSize(Vector2f size);
        void SetMesh(Render::Mesh *mesh);
        void SetPreviewBounds(const AABB &bounds);
        void SetMaterial(Render::Material *material);
        void SetOverlayCallback(OverlayCallback callback) { _overlay_callback = std::move(callback); }
        void SetShowGrid(bool show_grid);
        void SetWireframe(bool wireframe);

        void BeginCameraDrag(Vector2f local_position);
        void EndCameraDrag();
        void DragCamera(Vector2f local_position);
        void BeginCameraPan(Vector2f local_position);
        void EndCameraPan();
        void PanCamera(Vector2f local_position);
        void ZoomCamera(f32 scroll_delta);
        void ResetCamera();
        bool GetCameraRay(Vector2f local_position, Vector3f &origin, Vector3f &direction) const;
        bool ProjectWorldPoint(const Vector3f &world_position, Vector2f &screen_position, f32 &depth) const;
        f32 GetScreenRadius(f32 world_radius, f32 depth) const;
        void Render();
        bool IsRenderPending() const { return _render_pending; }

        Render::RenderTexture *GetRenderTexture() const { return _render_texture.get(); }

    private:
        Render::Mesh *_mesh = nullptr;
        Render::Material *_material = nullptr;
        Ref<Render::Material> _preview_material;
        Ref<Render::Material> _grid_material;
        Ref<Render::Material> _wireframe_material;
        Ref<Render::RenderTexture> _render_texture;
        OverlayCallback _overlay_callback;
        OrbitCameraController _orbit_controller;
        AABB _preview_bounds;
        Vector2f _viewport_size{360.0f, 300.0f};
        bool _camera_initialized = false;
        bool _show_grid = true;
        bool _wireframe = false;
        bool _has_preview_bounds = false;
        bool _render_pending = false;
        u16 _render_width = 0u;
        u16 _render_height = 0u;
    };
}
