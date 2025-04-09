#include "Widgets/BlendSpaceEditor.h"
#include "Ext/imgui/imgui.h"
#include "Framework/Common/Asset.h"
#include "Framework/Common/Input.h"
namespace Ailu
{
    namespace Editor
    {
#define VEC2_TO_IMGUI_VEC2(vec) ImVec2(vec[0], vec[1])
        BlendSpaceEditor::BlendSpaceEditor() : ImGuiWidget("BlendSpace Editor")
        {
            _allow_close = true;
            _canvas.AddChild(new UI::Button,UI::Canvas::Slot({400.0f,240.0f}));
        }
        BlendSpaceEditor::~BlendSpaceEditor()
        {
        }
        void BlendSpaceEditor::Open(const i32 &handle)
        {
            ImGuiWidget::Open(handle);
        }
        void BlendSpaceEditor::Close(i32 handle)
        {
            ImGuiWidget::Close(handle);
        }
        void BlendSpaceEditor::SetTarget(BlendSpace *target)
        {
            _target = target;
        }
        void BlendSpaceEditor::ShowImpl()
        {
            _canvas.SetPosition(_content_pos);
            _canvas.Update(1.0f);
            _canvas.Render();
            auto handle = _canvas.GetRenderTexture()->ColorTexture(Texture::kMainSRVIndex);
            Vector2f mpos = Input::GetMousePos() - _content_pos;
            if (handle == 0)
            {
                LOG_WARNING("_canvas.GetRenderTexture()->ColorTexture() is 0");
            }
            else
            {
                //ImGui::Image(TEXTURE_HANDLE_TO_IMGUI_TEXID(handle),VEC2_TO_IMGUI_VEC2(_canvas.GetSize()));
                ImGui::Image(TEXTURE_HANDLE_TO_IMGUI_TEXID(handle),VEC2_TO_IMGUI_VEC2(_content_size));
            }
//            if (_target)
//            {
//                AnimationClip* _delete_clip = nullptr;
//                ImGui::Text("Name: %s", _selected_clip ? _selected_clip->Name().c_str() : "null");
//                bool is_open = true;
//                ImGui::Checkbox("new_clip",&is_open);
//                if (ImGui::BeginDragDropTarget())
//                {
//                    const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(ImGuiWidget::kDragAssetAnimClip.c_str());
//                    if (payload)
//                    {
//                        auto clip = *(Asset **) (payload->Data);
//                        auto clip_ref = clip->AsRef<AnimationClip>();
//                        _selected_clip = clip_ref.get();
//                    }
//                    ImGui::EndDragDropTarget();
//                }
//                Vector2f new_pos;
//                ImGui::InputFloat2("##Pos: ", new_pos.data, "%.0f");
//                ImGui::SameLine();
//                if (ImGui::Button("Add"))
//                {
//                    _target->AddClip(_selected_clip, new_pos.x, new_pos.y);
//                }
//                ImGui::Separator();
//                //range
//                Vector2f x_range,y_range;
//                _target->GetRange(x_range,y_range);
//                ImGui::InputFloat2("X-Axis: ", x_range.data, "%.0f");
//                ImGui::InputFloat2("Y-Axis: ", y_range.data, "%.0f");
//                _target->Resize(x_range, y_range);
//                //pos
//                _target->GetPosition(new_pos.x, new_pos.y);
//                ImGui::SliderFloat("PosX: ", &new_pos.x, x_range.x, x_range.y, "%.0f");
//                ImGui::SliderFloat("PosY: ", &new_pos.y, y_range.x, y_range.y, "%.0f");
//                _target->SetPosition(new_pos.x, new_pos.y);
//                ImGui::Separator();
//                for (auto &c: _target->GetClips())
//                {
//                    ImGui::PushID(c._clip->Name().c_str());
//                    ImGui::Text("name: %s", c._clip->Name().c_str());
//                    Vector2f pos = {c._x, c._y};
//                    ImGui::InputFloat2("##Pos: ", pos.data, "%.0f");
//                    _target->SetClipPosition(c._clip, pos.x, pos.y);
//                    ImGui::SameLine();
//                    if (ImGui::Button("x"))
//                    {
//                        _delete_clip = c._clip;
//                    }
//                    ImGui::PopID();
//                }
//                if (_delete_clip)
//                {
//                    _target->RemoveClip(_delete_clip);
//                }
//            }
        }
        void BlendSpaceEditor::OnEvent(Event &event)
        {
            _canvas.OnEvent(event);
        }
    }// namespace Editor
}
