//
// Created by 22292 on 2024/7/18.
//

#include "Ext/imgui/imgui.h"
#include "Framework/Common/Log.h"
#include "Render/Texture.h"

#include "Inc/Widgets/PlaceActors.h"

namespace Ailu
{
    using namespace Render;
    namespace Editor
    {

        PlaceActors::PlaceActors() : ImGuiWidget("PlaceActors")
        {
            _allow_close = false;
            memset(_input_buffer, 0, 256);
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
                LOG_INFO("Search {}", _input_buffer);
            }
            ImGui::Spacing();
            ImVec2 avail_size = ImGui::GetContentRegionAvail();
            ImVec2 left_size, right_size;
            left_size.x = avail_size.x * 0.3f;
            left_size.y = avail_size.y;
            right_size = left_size;
            right_size.x = avail_size.x * 0.7f;
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

            static auto placement_element_draw = [](EPlaceActorsType::EPlaceActorsType type, f32 size)
            {
                const ImVec2 image_size = {size, size};
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Image(TEXTURE_HANDLE_TO_IMGUI_TEXID(Texture::s_p_default_normal->GetNativeTextureHandle()), image_size);
                ImGui::TableSetColumnIndex(1);
                const char *type_str = EPlaceActorsType::ToString(type);
                ImGui::Text(type_str + 1);
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    ImGui::SetDragDropPayload(kDragPlacementObj.c_str(), type_str, strlen(type_str));
                    ImGui::Text(type_str + 1);
                    ImGui::EndDragDropSource();
                }
            };
            const f32 image_size = 32.f;
            if (ImGui::BeginTable("##GroupItemTabel", 2))
            {
                ImGui::TableSetupColumn("Icon",ImGuiTableColumnFlags_WidthFixed,image_size + ImGui::GetStyle().CellPadding.x);
                ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthFixed,100);
                placement_element_draw(EPlaceActorsType::kCube, image_size);
                placement_element_draw(EPlaceActorsType::kPlane, image_size);
                placement_element_draw(EPlaceActorsType::kSphere, image_size);
                placement_element_draw(EPlaceActorsType::kCapsule, image_size);
                placement_element_draw(EPlaceActorsType::kMonkey, image_size);
                placement_element_draw(EPlaceActorsType::kCone, image_size);
                placement_element_draw(EPlaceActorsType::kCylinder, image_size);
                placement_element_draw(EPlaceActorsType::kTorus, image_size);

                placement_element_draw(EPlaceActorsType::kDirectionalLight, image_size);
                placement_element_draw(EPlaceActorsType::kPointLight, image_size);
                placement_element_draw(EPlaceActorsType::kSpotLight, image_size);
                placement_element_draw(EPlaceActorsType::kAreaLight, image_size);
                placement_element_draw(EPlaceActorsType::kLightProbe, image_size);
                ImGui::EndTable();
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }
    }// namespace Editor
}// namespace Ailu