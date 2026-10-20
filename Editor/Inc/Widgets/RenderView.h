#ifndef __RENDER_VIEW__
#define __RENDER_VIEW__
#include "Dock/DockWindow.h"
#include "Common/CameraControllers.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/String.h"
#include "generated/RenderView.gen.h"

namespace Ailu
{
    class PropertyInfo;
    namespace Render
    {
        enum class ECameraType;
        class Mesh;
        class Material;
        class Camera;
        class RenderTexture;
        class VolumeTexturePreviewPass;
    }// namespace Render
    namespace UI
    {
        class Border;
        class Button;
        class Canvas;
        class CheckBox;
        class Dropdown;
        class HorizontalBox;
        class InputBlock;
        class Image;
        class Text;
        class VerticalBox;
        class SplitView;
    }
    namespace SceneManagement
    {
        class Scene;
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
            void TickBackground(f32 dt) override;
        private:
            struct PendingPlaceActorSpawn
            {
                String _id;
                Vector3f _position;
                SceneManagement::Scene *_scene = nullptr;
            };

            void UpdateCameraOutputSize(f32 dt);
            void ProcessPendingPlaceActorSpawns();
            void BuildSceneToolbar();
            void BuildSceneCameraSettings();
            void SetSuperResolutionControls(bool enabled, bool fsr_selected);
            void UpdateSceneCameraSettings();
            void UpdateSuperResolutionStatus();
            void UpdateSceneToolbarLayout();
            void UpdateSceneToolbarBackdrop();
            void UpdateSceneToolbarState();
            bool IsSceneViewportFocused() const;
            void UpdateDragPreview();
            void UpdateCameraPreview();
            void SetSceneCamera2DOrthographic();
            void SetSceneCamera2DOrthographic(ESceneView2DOrientation orientation);
            void SetSceneCameraPerspective();
            void SetSceneCameraTransform(const Vector3f &position, const Quaternion &rotation);
            void UpdateSceneCameraTransformSettings();
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
            UI::Button *_btn_taa_test = nullptr;
            UI::Button *_btn_motion_vector_debug = nullptr;
            UI::Button *_btn_camera_settings = nullptr;
            UI::Border *_camera_settings_panel = nullptr;
            UI::InputBlock *_camera_position_inputs[3] = {};
            UI::InputBlock *_camera_rotation_inputs[3] = {};
            UI::HorizontalBox *_camera_fov_row = nullptr;
            UI::HorizontalBox *_camera_size_row = nullptr;
            UI::Dropdown *_camera_lens_projection_dropdown = nullptr;
            UI::HorizontalBox *_camera_sensor_width_row = nullptr;
            UI::HorizontalBox *_camera_sensor_height_row = nullptr;
            UI::HorizontalBox *_camera_focal_length_row = nullptr;
            UI::Dropdown *_camera_exposure_mode_dropdown = nullptr;
            UI::HorizontalBox *_camera_aperture_row = nullptr;
            UI::HorizontalBox *_camera_shutter_row = nullptr;
            UI::HorizontalBox *_camera_iso_row = nullptr;
            UI::HorizontalBox *_camera_exposure_compensation_row = nullptr;
            UI::HorizontalBox *_camera_focus_distance_row = nullptr;
            UI::InputBlock *_camera_fov_input = nullptr;
            UI::Text *_camera_vertical_fov_status = nullptr;
            UI::InputBlock *_camera_sensor_width_input = nullptr;
            UI::InputBlock *_camera_sensor_height_input = nullptr;
            UI::InputBlock *_camera_focal_length_input = nullptr;
            UI::InputBlock *_camera_aperture_input = nullptr;
            UI::InputBlock *_camera_shutter_input = nullptr;
            UI::InputBlock *_camera_iso_input = nullptr;
            UI::InputBlock *_camera_exposure_compensation_input = nullptr;
            UI::InputBlock *_camera_focus_distance_input = nullptr;
            UI::Text *_camera_ev100_status = nullptr;
            UI::Text *_camera_exposure_multiplier_status = nullptr;
            UI::InputBlock *_camera_near_input = nullptr;
            UI::InputBlock *_camera_far_input = nullptr;
            UI::InputBlock *_camera_size_input = nullptr;
            UI::Dropdown *_camera_aa_dropdown = nullptr;
            UI::InputBlock *_camera_render_scale_input = nullptr;
            UI::InputBlock *_camera_mip_bias_input = nullptr;
            UI::CheckBox *_camera_auto_mip_bias_checkbox = nullptr;
            UI::Text *_camera_mip_bias_status = nullptr;
            UI::HorizontalBox *_camera_upsampling_row = nullptr;
            UI::Dropdown *_camera_upsampling_dropdown = nullptr;
            UI::HorizontalBox *_camera_sr_row = nullptr;
            UI::Dropdown *_camera_sr_quality_dropdown = nullptr;
            UI::Text *_camera_sr_status = nullptr;
            UI::CheckBox *_camera_postprocess_checkbox = nullptr;
            UI::Border *_camera_preview_frame = nullptr;
            UI::Image *_camera_preview = nullptr;
            Render::ECameraType _stored_camera_type;
            Vector3f _stored_camera_position = Vector3f::kZero;
            Quaternion _stored_camera_rotation = Quaternion::Identity();
            f32 _stored_camera_size = 10.0f;
            ESceneView2DOrientation _scene_2d_orientation = ESceneView2DOrientation::kXY;
            bool _has_stored_perspective_camera = false;
            bool _is_scene_toolbar_ortho_cached = false;
            bool _is_scene_toolbar_snap_cached = false;
            bool _is_scene_toolbar_state_dirty = true;
            bool _is_motion_vector_debug_enabled = false;
            Vector2f _mouse_pos;
            Vector<PendingPlaceActorSpawn> _pending_place_actor_spawns;
            Vector2f _view_size = Vector2f::kZero;
            Vector2UInt _pending_output_size = Vector2UInt::kZero;
            Vector2UInt _committed_output_size = Vector2UInt::kZero;
            f32 _resize_stable_time = 0.0f;
            bool _is_camera_input_active = false;
            Vector2f _camera_input_last_mouse_pos = Vector2f::kZero;
            bool _has_camera_input_last_mouse_pos = false;
            Vector3f _drag_preview_pos = Vector3f::kZero;
            Ref<Render::Mesh> _drag_preview_mesh = nullptr;
            Ref<Render::Material> _drag_preview_material = nullptr;
            Ref<Render::RenderTexture> _camera_preview_texture = nullptr;
        };

        ACLASS()
        class GameView : public RenderView
        {
            GENERATED_BODY()
        public:
            GameView();
            void Update(f32 dt) final;
            void TickBackground(f32 dt) override;
        private:
            UI::Text *_no_main_camera_text = nullptr;
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
