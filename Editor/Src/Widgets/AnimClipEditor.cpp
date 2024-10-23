#include "Widgets/AnimClipEditor.h"
#include "Ext/imgui/imgui.h"
#include "Ext/ImGuizmo/ImSequencer.h"
#include "pch.h"

namespace Ailu
{
    namespace Editor
    {
        AnimClipEditor::AnimClipEditor() : ImGuiWidget("AnimClipEditor")
        {
        }

        AnimClipEditor::~AnimClipEditor()
        {
        }
        void AnimClipEditor::Open(const i32 &handle)
        {
            ImGuiWidget::Open(handle);
        }
        void AnimClipEditor::Close(i32 handle)
        {
            ImGuiWidget::Close(handle);
        }
        void AnimClipEditor::ShowImpl()
        {
            u16 clip_index = 0;
            static int s_clip_selected_index = -1;
            static AnimationClip* s_clip = nullptr;
            if (ImGui::BeginCombo("##animclip", _clip ? _clip->Name().c_str() : "choose clip"))
            {
                //for (auto it = g_pResourceMgr->ResourceBegin<AnimationClip>(); it != g_pResourceMgr->ResourceEnd<AnimationClip>(); it++)
                //auto clip = g_pResourceMgr->IterToRefPtr<AnimationClip>(it);
                for (auto it = AnimationClipLibrary::Begin(); it != AnimationClipLibrary::End(); it++)
                {
                    auto clip = it->second;
                    if (ImGui::Selectable(it->first.c_str(), s_clip_selected_index == clip_index))
                        s_clip_selected_index = clip_index;
                    if (s_clip_selected_index == clip_index)
                    {
                        _clip = clip.get();
                    }
                    ++clip_index;
                }
                ImGui::EndCombo();
            }
            s_clip = AnimationClipLibrary::Begin()->second.get();
            if (_clip)
            {
                // Show Skeleton Information (can be expanded later)
                ImGui::Text("Skeleton Information");
                ImGui::Separator();
                //ImGui::Text("Number of Joints: %d", _clip->_sk.JointNum());// Assuming GetJointCount exists in Skeleton class

                // Display and edit basic properties of the AnimationClip
                ImGui::Text("Animation Properties");
                ImGui::Separator();
                ImGui::Text("Frame Count: %u", _clip->FrameCount());
                ImGui::Text("Duration (seconds): %.2f", _clip->Duration());
                ImGui::Text("Frame Rate: %u", _clip->FrameRate());
                ImGui::Text("Frame Duration: %.2f", _clip->FrameDuration());

                // Optionally allow to modify the frame rate and automatically adjust frame duration
                //static u32 new_frame_rate = _clip->FrameRate();
                //if (ImGui::SliderInt("Frame Rate", (int *) &new_frame_rate, 1, 120))
                //{
                //    _clip->_frame_rate = new_frame_rate;
                //    _clip->_frame_duration = _clip->Duration() / _clip->FrameCount();
                //}

                // Display keyframes and allow adding new keyframes
                ImGui::Text("Keyframes");
                ImGui::Separator();
                static i32 s_view_frame = 0;
                static i32 s_joint_index = 0;
                static f32 s_alpha = 0.f;
                static f32 s_time = 0.f;
                ImGui::SliderInt("ViewFrame", &s_view_frame, 0, _clip->FrameCount() - 1);
                ImGui::SliderInt("JointIndex", &s_joint_index, 0, _clip->Size() - 1);
                ImGui::SliderFloat("LerpToNext", &s_alpha,0.f,1.f);
                i32 joint = s_joint_index;
                TransformTrack& track = (*_clip)[s_joint_index];
                ImGui::SliderFloat("Time", &s_time, track.GetStartTime(), track.GetEndTime());
                Transform joint_pose;
                joint_pose = track.Evaluate(joint_pose,s_time,false);
                //const Transform &joint_pose = _clip->GetLocalPose()[joint][s_view_frame];
                Vector3f euler = Quaternion::EulerAngles(joint_pose._rotation);
                ImGui::Text("  Joint %d:", joint);
                ImGui::Text("    Position (%f, %f, %f)", joint_pose._position.x, joint_pose._position.y, joint_pose._position.z);
                ImGui::Text("    Rotation (%f, %f, %f,%f)", joint_pose._rotation.x, joint_pose._rotation.y, joint_pose._rotation.z, joint_pose._rotation.w);
                ImGui::Text("    Scale (%f, %f, %f)", joint_pose._scale.x, joint_pose._scale.y, joint_pose._scale.z);
                ImGui::Text("    Rotation(Eular) (%.2f, %.2f, %.2f)", euler.x, euler.y, euler.z);
            }
        }
    }// namespace Editor
}// namespace Ailu