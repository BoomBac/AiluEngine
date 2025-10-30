#ifndef __RENDER_VIEW__
#define __RENDER_VIEW__
#include "Dock/DockWindow.h"
#include "Framework/Math/Transform.h"
#include "generated/RenderView.gen.h"
namespace Ailu
{
    namespace Render
    {
        class Mesh;
        class Camera;
    }// namespace Render
    namespace UI
    {
        class Image;
        class VerticalBox;
    }
    namespace Editor
    {
        class FirstPersonCameraController
        {
        public:
            static FirstPersonCameraController s_inst;
            FirstPersonCameraController();
            explicit FirstPersonCameraController(Render::Camera *camera);
            void Attach(Render::Camera *camera);
            void SetTargetPosition(const Vector3f &position, bool is_force = false);
            void SetTargetRotation(f32 x, f32 y, bool is_force = false);
            void Move(const Vector3f &d);
            void Interpolate(float speed);
            void Accelerate(bool faster) { _cur_move_speed = faster ? _fast_camera_move_speed : _base_camera_move_speed; };
            Vector2f _rotation;
            Vector3f _target_pos;
            bool _is_receive_input = true;
            f32 _base_camera_move_speed = 0.6f;
            f32 _fast_camera_move_speed = _base_camera_move_speed * 3.0f;
            f32 _cur_move_speed = _base_camera_move_speed;
            f32 _camera_wander_speed = 0.6f;
            f32 _lerp_speed_multifactor = 0.75f;
            f32 _camera_fov_h = 60.0f;
            f32 _camera_near = 0.01f;
            f32 _camera_far = 1000.0f;

        private:
            Render::Camera *_p_camera;
            Quaternion _rot_object_x;
            Quaternion _rot_world_y;
        };

        ACLASS()
        class RenderView : public DockWindow
        {
            GENERATED_BODY()
        public:
            RenderView();
            void Update(f32 dt) override;
            void SetSource(Render::Texture *tex);
        protected:
            UI::VerticalBox *_vb = nullptr;
            UI::Image *_source = nullptr;
        };

        class TransformGizmo;
        ACLASS()
        class SceneView : public RenderView
        {
            GENERATED_BODY()
        public:
            SceneView();
            void Update(f32 dt) final;
        private:
            void ProcessCameraInput(f32 dt);
        private:
            Scope<TransformGizmo> _transform_gizmo;
            Vector2f _mouse_pos;
            Vector3f _drag_preview_pos;
            Ref<Render::Mesh> _drag_preview_mesh = nullptr;
        };
    }
}
#endif// !__RENDER_VIEW__
