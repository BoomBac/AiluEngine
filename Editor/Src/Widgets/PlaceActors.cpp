//
// Created by 22292 on 2024/7/18.
//

#include "Ext/imgui/imgui.h"
#include "Framework/Common/Log.h"
#include "Render/Texture.h"

#include "Inc/Widgets/PlaceActors.h"

namespace Ailu
{
    namespace Editor
    {

        PlaceActors::PlaceActors() : ImGuiWidget("PlaceActors")
        {
            _allow_close = false;
        }
        PlaceActors::~PlaceActors() = default;
        void PlaceActors::Open(const i32 &handle)
        {
            ImGuiWidget::Open(handle);
        }
        void PlaceActors::Close(i32 handle)
        {
            ImGuiWidget::Close(handle);
        }
        void PlaceActors::OnEvent(Event &e)
        {
            ImGuiWidget::OnEvent(e);
        }
        void PlaceActors::ShowImpl()
        {
            memset(_input_buffer, 0, 256);
            if (ImGui::InputText("Search", _input_buffer, 256))
            {
                g_pLogMgr->LogFormat("Search {}", _input_buffer);
            }
            ImGui::Spacing();
            ImVec2 avail_size = ImGui::GetContentRegionAvail();
            ImVec2 left_size, right_size;
            left_size.x = avail_size.x * 0.3;
            left_size.y = avail_size.y;
            right_size = left_size;
            right_size.x = avail_size.x * 0.7;
            ImGui::BeginChild("##Group", left_size);
            static i16 s_selected_index = -1;
            for (i16 i = 0; i < _groups.size(); i++)
            {
                if (ImGui::Selectable(_groups[i].c_str(), _group_selected_index == i))
                    _group_selected_index = i;
            }
            ImGui::EndChild();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
            ImGui::SameLine();
            ImVec2 topLeft = ImGui::GetCursorScreenPos();
            ImVec2 bottomRight = ImVec2(topLeft.x + ImGui::GetContentRegionAvail().x, topLeft.y + right_size.y);
            auto header_color = ImGui::GetStyle().Colors[ImGuiCol_Header];
            header_color.x *= 255.0f;
            header_color.y *= 255.0f;
            header_color.z *= 255.0f;
            header_color.w *= 255.0f;
            ImU32 color = IM_COL32(header_color.x, header_color.y, header_color.z, header_color.w);// Solid red color

            ImGui::GetWindowDrawList()->AddRectFilled(topLeft, bottomRight, color);

            ImGui::BeginChild("##GroupItem", right_size);
            const ImVec2 image_size = {32.0f, 32.0f};

            if (ImGui::BeginTable("##GroupItemTabel", 2))
            {
                ImGui::TableSetupColumn("Icon");
                ImGui::TableSetupColumn("Text");
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Image(TEXTURE_HANDLE_TO_IMGUI_TEXID(Texture::s_p_default_normal->GetNativeTextureHandle()), image_size);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("Cube");
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        String cube = "Cube";
                        ImGui::SetDragDropPayload(kDragPlacementObj.c_str(), cube.c_str(), cube.size() + 1);
                        ImGui::Text("Place cube...");
                        ImGui::EndDragDropSource();
                    }
                }
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Image(TEXTURE_HANDLE_TO_IMGUI_TEXID(Texture::s_p_default_normal->GetNativeTextureHandle()), image_size);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("LightProbe");
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        String cube = "LightProbe";
                        ImGui::SetDragDropPayload(kDragPlacementObj.c_str(), cube.c_str(), cube.size() + 1);
                        ImGui::Text("Place LightProbe...");
                        ImGui::EndDragDropSource();
                    }
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }
    }// namespace Editor
}// namespace Ailu