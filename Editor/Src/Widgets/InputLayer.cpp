#include "Widgets/InputLayer.h"
#include "Ext/imgui/imgui.h"
#include "Framework/Common/Input.h"
#include "Framework/Events/KeyEvent.h"
#include "Framework/Events/MouseEvent.h"
#include "Render/Camera.h"

namespace Ailu
{
    namespace Editor
    {
        //---------------------------------------------------------FirstPersonCameraController--------------------------------------------------------
        FirstPersonCameraController FirstPersonCameraController::s_inst;

        FirstPersonCameraController::FirstPersonCameraController()
            : FirstPersonCameraController(nullptr) {}

        FirstPersonCameraController::FirstPersonCameraController(Camera *camera)
            : _p_camera(camera) {}
        void FirstPersonCameraController::Attach(Camera *camera)
        {
            _p_camera = camera;
            _rot_world_y = Quaternion::AngleAxis(_rotation.y, Vector3f::kUp);
            Vector3f new_camera_right = _rot_world_y * Vector3f::kRight;
            _rot_object_x = Quaternion::AngleAxis(_rotation.x, new_camera_right);
            _target_pos = camera->Position();
        }

        void FirstPersonCameraController::Move(const Vector3f &d)
        {
            if(!_is_receive_input)
                return ;
            _p_camera->Position(d);
        }
        void FirstPersonCameraController::SetTargetRotation(f32 x, f32 y,bool is_force)
        {
            if(!_is_receive_input && !is_force)
                return ;
            _rotation.x = x;
            _rotation.y = y;
        }
        void FirstPersonCameraController::Interpolate(float speed)
        {
            _fast_camera_move_speed = _base_camera_move_speed * 3.0f;
            Quaternion new_quat_y = Quaternion::AngleAxis(_rotation.y, Vector3f::kUp);
            Vector3f new_camera_right = new_quat_y * Vector3f::kRight;
            auto new_quat_x = Quaternion::AngleAxis(_rotation.x, new_camera_right);
            _rot_object_x = Quaternion::NLerp(_rot_object_x, new_quat_x, speed);
            _rot_world_y = Quaternion::NLerp(_rot_world_y, new_quat_y, speed);
            _p_camera->Position(Lerp(_p_camera->Position(), _target_pos, speed));
            auto r = _rot_world_y * _rot_object_x;
            _p_camera->Rotation(r);
            _p_camera->RecalculateMatrix(true);
        }
        void FirstPersonCameraController::SetTargetPosition(const Vector3f &position,bool is_force)
        {
            if(!_is_receive_input && !is_force)
                return ;
            _target_pos = position;
        }
        //---------------------------------------------------------FirstPersonCameraController--------------------------------------------------------

        InputLayer::InputLayer() : Layer("InputLayer")
        {
            if (Camera::sCurrent == nullptr)
            {
                LOG_WARNING("Current Camera is null");
                Camera::GetDefaultCamera();
                Camera::sCurrent->_anti_aliasing = EAntiAliasing::kTAA;
            }
            FirstPersonCameraController::s_inst.Attach(Camera::sCurrent);
            FirstPersonCameraController::s_inst._is_receive_input = true;
            FirstPersonCameraController::s_inst._camera_near = Camera::sCurrent->Near();
            FirstPersonCameraController::s_inst._camera_far = Camera::sCurrent->Far();
            Camera::sCurrent->_is_scene_camera = true;
            Camera::sCurrent->_is_gen_voxel = true;
            Camera::sCurrent->_is_enable = true;
        }
        InputLayer::InputLayer(const String &name) : InputLayer()
        {
            _debug_name = name;
        }
        InputLayer::~InputLayer()
        {
        }
        void InputLayer::OnAttach()
        {

        }
        void InputLayer::OnDetach()
        {

        }
        void InputLayer::OnEvent(Event &e)
        {
            EventDispather dispater1(e);
            dispater1.Dispatch<MouseButtonReleasedEvent>(
                    [this](MouseButtonReleasedEvent &e) -> bool
                    {
                        if (e.GetButton() == AL_KEY_RBUTTON)
                        {
                            FirstPersonCameraController::s_inst._is_receive_input = false;
                            while (::ShowCursor(TRUE) < 0)
                                ;
                            return false;
                        }
                        return false;
                    });
            EventDispather dispater(e);
            dispater.Dispatch<MouseScrollEvent>([this](MouseScrollEvent &e) -> bool{
                if(FirstPersonCameraController::s_inst._is_receive_input)
                {
                    FirstPersonCameraController::s_inst._base_camera_move_speed += e.GetOffsetY() > 0 ? 0.01f : -0.01f;
                    FirstPersonCameraController::s_inst._base_camera_move_speed = std::ranges::max(0.0f, FirstPersonCameraController::s_inst._base_camera_move_speed);
                }
                return false; });
            EventDispather dispater0(e);
            dispater0.Dispatch<MouseButtonPressedEvent>(
                    [this](MouseButtonPressedEvent &e) -> bool
                    {
                        if (e.GetButton() == AL_KEY_RBUTTON)
                        {
                            FirstPersonCameraController::s_inst._is_receive_input = true;
                            while (::ShowCursor(FALSE) >= 0)
                                ;// 隐藏鼠标指针，直到它不再可见
                            return false;
                        }
                        return false;
                    });
            dispater0.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& e)->bool{
                if (e.GetKeyCode() == AL_KEY_SHIFT)
                    FirstPersonCameraController::s_inst.Accelerate(true);
                return false;
            });
            dispater0.Dispatch<KeyReleasedEvent>([this](KeyReleasedEvent& e)->bool{
                if (e.GetKeyCode() == AL_KEY_SHIFT)
                    FirstPersonCameraController::s_inst.Accelerate(false);
                return false;
            });
        }

        void InputLayer::OnImguiRender()
        {
            if (Camera::sCurrent)
            {
                auto camera_pos = Camera::sCurrent->Position();
                auto camera_rotation = FirstPersonCameraController::s_inst._rotation;
                FirstPersonCameraController::s_inst._camera_near = Camera::sCurrent->Near();
                FirstPersonCameraController::s_inst._camera_far = Camera::sCurrent->Far();
                ImGui::Begin("CameraDetail");
                ImGui::SliderFloat("MoveSpeed", &FirstPersonCameraController::s_inst._base_camera_move_speed, 0.00f, 10.0f, "%.2f");
                ImGui::SliderFloat("WanderSpeed", &FirstPersonCameraController::s_inst._camera_wander_speed, 0.00f, 2.0f,
                                   "%.2f");
                ImGui::SliderFloat("LerpSpeed", &FirstPersonCameraController::s_inst._lerp_speed_multifactor, 0.0f, 1.0f,
                                   "%.2f");
                ImGui::SliderFloat("FovH", &FirstPersonCameraController::s_inst._camera_fov_h, 0.0f, 120.0f, "%.2f");
                ImGui::SliderFloat("NearClip", &FirstPersonCameraController::s_inst._camera_near, 0.00001f, 100.0f, "%.2f");
                ImGui::SliderFloat("FarClip", &FirstPersonCameraController::s_inst._camera_far, 100.0f, 5000.0f, "%.2f");
                ImGui::InputFloat3("Position: ",camera_pos.data);
                //ImGui::InputFloat2("Rotation: ", camera_rotation.data);
                FirstPersonCameraController::s_inst.SetTargetPosition(camera_pos,true);
                ImGui::SliderFloat2("Rotation: ", camera_rotation.data, -180.0f, 180.0f, "%.2f");
                FirstPersonCameraController::s_inst.SetTargetRotation(camera_rotation.x, camera_rotation.y,true);
                //ImGui::Text("Position:");
                //ImGui::SameLine();
                //ImGui::Text(camera_pos.ToString().c_str());
                //ImGui::Text("Rotation:");
                //ImGui::SameLine();
                //ImGui::Text(camera_rotation.ToString().c_str());
                ImGui::End();
            }
        }
        void InputLayer::OnUpdate(float delta_time)
        {
            Camera::sCurrent->FovH(FirstPersonCameraController::s_inst._camera_fov_h);
            Camera::sCurrent->Near(FirstPersonCameraController::s_inst._camera_near);
            Camera::sCurrent->Far(FirstPersonCameraController::s_inst._camera_far);
            if (Camera::sCurrent && !Input::IsInputBlock())
            {
                static Vector2f target_rotation = {0.f, 0.f};
                static Vector2f pre_mouse_pos;
                target_rotation = FirstPersonCameraController::s_inst._rotation;
                auto cur_mouse_pos = Input::GetMousePos();
                if (Input::IsKeyPressed(AL_KEY_RBUTTON))
                {
                    if (abs(cur_mouse_pos.x - pre_mouse_pos.x) < 100.0f &&
                        abs(cur_mouse_pos.y - pre_mouse_pos.y) < 100.0f)
                    {
                        float angle_offset = FirstPersonCameraController::s_inst._camera_wander_speed * FirstPersonCameraController::s_inst._camera_wander_speed;
                        target_rotation.y += (cur_mouse_pos.x - pre_mouse_pos.x) * angle_offset;
                        target_rotation.x += (cur_mouse_pos.y - pre_mouse_pos.y) * angle_offset;
                    }
                }
                pre_mouse_pos = cur_mouse_pos;
                FirstPersonCameraController::s_inst.SetTargetRotation(target_rotation.x, target_rotation.y);
                FirstPersonCameraController::s_inst.Accelerate(Input::IsKeyPressed(AL_KEY_SHIFT));
                static const f32 move_distance = 1.0f;// 1 m
                f32 final_move_distance = move_distance * FirstPersonCameraController::s_inst._cur_move_speed * FirstPersonCameraController::s_inst._cur_move_speed;
                Vector3f move_dis{0, 0, 0};
                if (Input::IsKeyPressed(AL_KEY_W))
                {
                    move_dis += Camera::sCurrent->Forward();
                }
                if (Input::IsKeyPressed(AL_KEY_S))
                {
                    move_dis -= Camera::sCurrent->Forward();
                }
                if (Input::IsKeyPressed(AL_KEY_D))
                {
                    move_dis += Camera::sCurrent->Right();
                }
                if (Input::IsKeyPressed(AL_KEY_A))
                {
                    move_dis -= Camera::sCurrent->Right();
                }
                if (Input::IsKeyPressed(AL_KEY_E))
                {
                    move_dis += Camera::sCurrent->Up();
                }
                if (Input::IsKeyPressed(AL_KEY_Q))
                {
                    move_dis -= Camera::sCurrent->Up();
                }
                auto target_pos = FirstPersonCameraController::s_inst._target_pos;
                target_pos += move_dis * final_move_distance;
                //LOG_INFO("{}", target_pos.ToString());
                FirstPersonCameraController::s_inst.SetTargetPosition(target_pos);
            }
            f32 lerp_factor = std::clamp(delta_time * FirstPersonCameraController::s_inst._lerp_speed_multifactor, 0.0f, 1.0f);
            lerp_factor = delta_time * FirstPersonCameraController::s_inst._lerp_speed_multifactor * FirstPersonCameraController::s_inst._lerp_speed_multifactor;
            lerp_factor = std::clamp(lerp_factor, 0.0f, 1.5f);
            FirstPersonCameraController::s_inst.Interpolate(lerp_factor);
        }

    }// namespace Editor
}// namespace Ailu
