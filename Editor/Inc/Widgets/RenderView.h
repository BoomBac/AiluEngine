#ifndef __RENDER_VIEW__
#define __RENDER_VIEW__
#include "Dock/DockWindow.h"
#include "Common/CameraControllers.h"
#include "generated/RenderView.gen.h"

namespace Ailu
{
    class PropertyInfo;
    namespace Render
    {
        enum class ECameraType;
        class Mesh;
        class Camera;
        class VolumeTexturePreviewPass;
    }// namespace Render
    namespace UI
    {
        class Border;
        class Button;
        class Canvas;
        class Dropdown;
        class HorizontalBox;
        class Image;
        class VerticalBox;
        class SplitView;
    }
    namespace Editor
    {
        ACLASS()
        class RenderView : public DockWindow
        {
            GENERATED_BODY()
        public:
            RenderView();
            void Update(f32 dt) override;
            void SetSource(Render::Texture *tex);
        protected:
            UI::Canvas *_view_canvas = nullptr;
            UI::Image *_source = nullptr;
        };

        class TransformGizmo;
        enum class ESceneView2DOrientation
        {
            kXY,
            kNegX,
            kNegY,
            kNegZ
        };

        ACLASS()
        class SceneView : public RenderView
        {
            GENERATED_BODY()
        public:
            SceneView();
            void Update(f32 dt) final;
        private:
            void UpdateCameraOutputSize(f32 dt);
            void BuildSceneToolbar();
            void UpdateSceneToolbarLayout();
            void UpdateSceneToolbarBackdrop();
            void UpdateSceneToolbarState();
            void SetSceneCamera2DOrthographic();
            void SetSceneCamera2DOrthographic(ESceneView2DOrientation orientation);
            void SetSceneCameraPerspective();
            bool IsSceneCamera2D() const;
            Vector2f ViewToRenderPosition(const Vector2f &view_pos) const;
            void ProcessCameraInput(f32 dt);
        private:
            FirstPersonCameraController *_camera_controller;
            CanvasCameraController _canvas_camera_controller;
            Scope<TransformGizmo> _transform_gizmo;
            UI::Border *_scene_toolbar_bg = nullptr;
            UI::HorizontalBox *_scene_toolbar = nullptr;
            UI::Button *_btn_2d = nullptr;
            UI::Dropdown *_dropdown_2d_orientation = nullptr;
            UI::Button *_btn_perspective = nullptr;
            UI::Button *_btn_snap = nullptr;
            Render::ECameraType _stored_camera_type;
            Vector3f _stored_camera_position = Vector3f::kZero;
            Vector2f _stored_camera_rotation = Vector2f::kZero;
            f32 _stored_camera_size = 10.0f;
            ESceneView2DOrientation _scene_2d_orientation = ESceneView2DOrientation::kXY;
            bool _has_stored_perspective_camera = false;
            bool _is_scene_toolbar_ortho_cached = false;
            bool _is_scene_toolbar_snap_cached = false;
            bool _is_scene_toolbar_state_dirty = true;
            Vector2f _mouse_pos;
            Vector2f _view_size = Vector2f::kZero;
            Vector2UInt _pending_output_size = Vector2UInt::kZero;
            Vector2UInt _committed_output_size = Vector2UInt::kZero;
            f32 _resize_stable_time = 0.0f;
            bool _is_camera_input_active = false;
            Vector2f _camera_input_last_mouse_pos = Vector2f::kZero;
            bool _has_camera_input_last_mouse_pos = false;
            Vector3f _drag_preview_pos = Vector3f::kZero;
            Ref<Render::Mesh> _drag_preview_mesh = nullptr;
        };
        ACLASS()
        class Texture3DView : public DockWindow
        {
            GENERATED_BODY()
        public:
            Texture3DView();
            ~Texture3DView();
            void Update(f32 dt) final;
            void SetSource3D(Render::Texture* tex); // 3D 纹理源
        private:
            UI::SplitView* _split_view = nullptr;
            UI::Image* _left_preview = nullptr;          // 2D 预览
            UI::VerticalBox* _right_menu = nullptr;
            Render::Texture* _source_3d = nullptr;
            Render::Texture* _preview_2d = nullptr; // 2D 预览 RT
            f32 _slice = 0.0f;                      // [0,1]
            i32 _axis = 2;                          // 0=X,1=Y,2=Z
            i32 _mip = 0;
            Render::VolumeTexturePreviewPass *_pass;
            OrbitCameraController _orbit_controller;
        };
    }
}
#endif// !__RENDER_VIEW__
