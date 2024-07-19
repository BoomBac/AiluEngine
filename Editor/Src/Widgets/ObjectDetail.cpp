#include "Widgets/ObjectDetail.h"
#include "Common/Selection.h"
#include "Ext/imgui/imgui.h"
#include "Ext/imgui/imgui_internal.h"
#include "Widgets/CommonTextureWidget.h"

#include "Framework/Common/Asset.h"
#include "Framework/Common/ResourceMgr.h"
#include "Objects/CameraComponent.h"
#include "Objects/StaticMeshComponent.h"
#include "Objects/TransformComponent.h"
#include "Render/CommandBuffer.h"
#include "Render/Renderer.h"

namespace Ailu
{
    namespace Editor
    {
        static u16 s_mini_tex_size = 64;

        namespace TreeStats
        {
            static i16 s_cur_opened_texture_selector = -1;
            static bool s_is_texture_selector_open = true;
        }// namespace TreeStats
        static void DrawVec3Control(const String &label, Vector3f &v, f32 reset_value = 0.f, f32 column_width = 80.0f)
        {
            ImGui::PushID(label.c_str());
            ImGui::Columns(2);
            ImGui::SetColumnWidth(0, column_width * 2.0f);
            ImGui::Text("%s", label.c_str());
            ImGui::NextColumn();

            ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            f32 line_height = ImGui::GetCurrentContext()->FontSize + ImGui::GetCurrentContext()->Style.FramePadding.y * 2.0f;
            ImVec2 btn_size = {line_height + 3.0f, line_height};

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 1.0f, 0.15f, 1.0f));
            if (ImGui::Button("X", btn_size))
                v.x = reset_value;
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            ImGui::DragFloat("##X", &v.x, 0.1f, 0.f, 0.f, "%.2f");
            ImGui::PopItemWidth();
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
            if (ImGui::Button("Y", btn_size))
                v.y = reset_value;
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            ImGui::DragFloat("##Y", &v.y, 0.1f, 0.f, 0.f, "%.2f");
            ImGui::PopItemWidth();
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.25f, 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.35f, 0.9f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.25f, 0.8f, 1.0f));
            if (ImGui::Button("Z", btn_size))
                v.z = reset_value;
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            ImGui::DragFloat("##Z", &v.z, 0.1f, 0.f, 0.f, "%.2f");
            ImGui::PopItemWidth();

            ImGui::Columns(1);
            ImGui::PopStyleVar();
            ImGui::PopID();
        }

        template<typename T, typename Drawer>
        static void DrawComponent(const String &name, SceneActor *actor, Drawer drawer)
        {
            static const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap;
            auto comp = actor->GetComponent<T>();
            if (comp)
            {
                ImVec2 content_region_available = ImGui::GetContentRegionAvail();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
                f32 line_height = ImGui::GetFrameHeight();
                ImGui::Separator();
                bool open = ImGui::TreeNodeEx(name.c_str(), flags, name.c_str());
                ImGui::PopStyleVar();
                ImGui::SameLine(content_region_available.x - line_height * 1.5f);
                if (ImGui::Button("+", ImVec2(line_height, line_height)))
                {
                    ImGui::OpenPopup("ComponentSetting");
                }
                bool remove_comp = false;
                if (ImGui::BeginPopup("ComponentSetting"))
                {
                    if (ImGui::MenuItem("Remove"))
                        remove_comp = true;
                    ImGui::EndPopup();
                }
                if (open)
                {
                    drawer(comp);
                    ImGui::TreePop();
                }
                if (remove_comp)
                {
                    actor->RemoveComponent(comp);
                    g_pSceneMgr->MarkCurSceneDirty();
                }
                ImGui::Spacing();
            }
        }

        static void DrawProperty(u32 &property_handle, SerializableProperty &prop, Object *obj, TextureSelector &selector_window)
        {
            ++property_handle;
            switch (prop._type)
            {
                case Ailu::ESerializablePropertyType::kString:
                    break;
                case Ailu::ESerializablePropertyType::kFloat:
                    ImGui::DragFloat(prop._name.c_str(), static_cast<float *>(prop._value_ptr));
                    break;
                case Ailu::ESerializablePropertyType::kBool:
                {
                    auto prop_value = SerializableProperty::GetProppertyValue<float>(prop);
                    if (prop_value.has_value())
                    {
                        bool checked = prop_value.value() == 1.0f;
                        ImGui::Checkbox(prop._name.c_str(), &checked);
                        prop.SetProppertyValue(checked ? 1.0f : 0.0f);
                    }
                    else
                        ImGui::Text("Toogle %s value missing", prop._name.c_str());
                }
                break;
                case Ailu::ESerializablePropertyType::kEnum:
                {
                    auto mat = dynamic_cast<Material *>(obj);
                    if (mat != nullptr)
                    {
                        //auto keywords = mat->GetShader()->GetKeywordGroups();
                        //auto it = keywords.find(prop._value_name);
                        //if (it != keywords.end())
                        //{
                        //	auto prop_value = static_cast<int>(prop.GetProppertyValue<float>().value_or(0.0));
                        //	if (ImGui::BeginCombo(prop._name.c_str(), it->second[prop_value].substr(it->second[prop_value].rfind("_") + 1).c_str()))
                        //	{
                        //		static int s_selected_index = -1;
                        //		for (int i = 0; i < it->second.size(); i++)
                        //		{
                        //			auto x = it->second[i].rfind("_");
                        //			auto enum_str = it->second[i].substr(x + 1);
                        //			if (ImGui::Selectable(enum_str.c_str(), i == s_selected_index))
                        //				s_selected_index = i;
                        //			if (s_selected_index == i)
                        //			{
                        //				mat->EnableKeyword(it->second[i]);
                        //				prop.SetProppertyValue(static_cast<float>(i));
                        //			}
                        //		}
                        //		ImGui::EndCombo();
                        //	}
                        //}
                        //else
                        //	ImGui::Text("Eunm %s value missing", prop._name.c_str());
                    }
                    else
                        ImGui::Text("Non-Material Eunm %s value missing", prop._name.c_str());
                }
                break;
                case Ailu::ESerializablePropertyType::kVector3f:
                    ImGui::DragFloat3(prop._name.c_str(), static_cast<Vector3f *>(prop._value_ptr)->data);
                    break;
                case Ailu::ESerializablePropertyType::kVector4f:
                    break;
                case Ailu::ESerializablePropertyType::kColor:
                    ImGui::ColorEdit3(prop._name.c_str(), static_cast<Vector4f *>(prop._value_ptr)->data);
                    break;
                case Ailu::ESerializablePropertyType::kRange:
                {
                    ImGui::Text(prop._name.c_str());
                    ImGui::SameLine();
                    ImGui::SliderFloat(std::format("##{}", prop._name).c_str(), static_cast<float *>(prop._value_ptr), prop._param[0], prop._param[1]);
                }
                break;
                case Ailu::ESerializablePropertyType::kTransform:
                    break;
                case Ailu::ESerializablePropertyType::kTexture2D:
                {
                    ImGui::Text("Texture2D : %s", prop._name.c_str());
                    auto tex_prop_value = SerializableProperty::GetProppertyValue<std::tuple<u8, Texture *>>(prop);
                    auto tex = tex_prop_value.has_value() ? std::get<1>(tex_prop_value.value()) : nullptr;
                    u32 cur_tex_id = tex ? tex->ID() : Texture::s_p_default_white->ID();
                    //auto& desc = std::static_pointer_cast<D3DTexture2D>(tex == nullptr ? TexturePool::GetDefaultWhite() : tex)->GetGPUHandle();
                    ImVec2 vMin = ImGui::GetWindowContentRegionMin();
                    ImVec2 vMax = ImGui::GetWindowContentRegionMax();
                    float contentWidth = (vMax.x - vMin.x);
                    float centerX = (contentWidth - s_mini_tex_size) * 0.5f;
                    ImGui::SetCursorPosX(centerX);
                    if (selector_window.IsCaller(property_handle))
                    {
                        u32 new_tex_id = selector_window.GetSelectedTexture(property_handle);
                        if (TextureSelector::IsValidTextureID(new_tex_id) && new_tex_id != cur_tex_id)
                        {
                            auto mat = dynamic_cast<Material *>(obj);
                            if (mat)
                                mat->SetTexture(prop._value_name, g_pResourceMgr->Get<Texture2D>(new_tex_id));
                        }
                    }
                    if (ImGui::ImageButton(TEXTURE_HANDLE_TO_IMGUI_TEXID(g_pResourceMgr->Get<Texture2D>(cur_tex_id)->GetNativeTextureHandle()), ImVec2(s_mini_tex_size, s_mini_tex_size)))
                    {
                        selector_window.Open(property_handle);
                    }
                    if (selector_window.IsCaller(property_handle))
                    {
                        ImGuiContext *context = ImGui::GetCurrentContext();
                        auto drawList = context->CurrentWindow->DrawList;
                        ImVec2 cur_img_pos = ImGui::GetCursorPos();
                        ImVec2 imgMin = ImGui::GetItemRectMin();
                        ImVec2 imgMax = ImGui::GetItemRectMax();
                        drawList->AddRect(imgMin, imgMax, IM_COL32(255, 255, 0, 255), 0.0f, 0, 2.0f);
                    }
                }
                break;
                case Ailu::ESerializablePropertyType::kStaticMesh:
                    break;

                default:
                    LOG_WARNING("prop: {} value type havn't be handle!", prop._name)
                    break;
            }
        }

        static void DrawMaterialDetailPanel(Material *mat)
        {
        }

        static void DrawInternalStandardMaterial(StandardMaterial *mat)
        {
            u16 mat_prop_index = 0;
            static auto func_show_prop = [&](StandardMaterial *mat, ETextureUsage tex_usage,f32 width)
            {
                ImGui::PushID(mat_prop_index++);
                const auto& standard_prop_info = StandardMaterial::StandardPropertyName::GetInfoByUsage(tex_usage);
                auto tex = const_cast<Texture*>(mat->MainTex(tex_usage));
                bool use_tex = tex != nullptr;
                ImGui::TableSetColumnIndex(0);
                ImGui::Checkbox("##cb",&use_tex);
                ImGui::SameLine();
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(ImGuiWidget::kDragAssetTexture2D.c_str()))
                    {
                        auto asset = *(Asset **) (payload->Data);
                        auto tex_ref = asset->AsRef<Texture2D>();
                        mat->SetTexture(tex_usage,tex_ref.get());
                        use_tex = true;
                        LOG_INFO("Set new texture");
                    }
                    ImGui::EndDragDropTarget();
                }
                if (mat->IsTextureUsed(tex_usage) != use_tex && !use_tex)
                {
                    mat->SetTexture(standard_prop_info._tex_name,nullptr);
                }

                ImGui::Text("%s", standard_prop_info._group_name.c_str());
                ImGui::TableSetColumnIndex(1);

                ImGui::PushMultiItemsWidths(1,  width);
                const ImVec2 preview_tex_size = ImVec2(s_mini_tex_size, s_mini_tex_size);
                if (tex == nullptr)
                {
                    auto& prop = mat->GetProperty(standard_prop_info._value_name);
                    switch (prop._type)
                    {
                        case Ailu::ESerializablePropertyType::kVector3f:
                            ImGui::DragFloat3("##f3", static_cast<Vector3f *>(prop._value_ptr)->data);
                            break;
                        case Ailu::ESerializablePropertyType::kVector4f:
                            ImGui::DragFloat4("##f4", static_cast<Vector4f *>(prop._value_ptr)->data);
                            break;
                        case Ailu::ESerializablePropertyType::kColor:
                            ImGui::ColorEdit4("##c4", static_cast<Vector4f *>(prop._value_ptr)->data);
                            break;
                        case Ailu::ESerializablePropertyType::kRange:
                            ImGui::SliderFloat("##r", static_cast<float *>(prop._value_ptr), prop._param[0], prop._param[1]);
                        break;
                    }
                }
                else
                {
                    ImGui::ImageButton(TEXTURE_HANDLE_TO_IMGUI_TEXID(tex->GetNativeTextureHandle()), preview_tex_size);
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(ImGuiWidget::kDragAssetTexture2D.c_str()))
                        {
                            auto asset = *(Asset **) (payload->Data);
                            auto tex_ref = asset->AsRef<Texture2D>();
                            mat->SetTexture(tex_usage,tex_ref.get());
                            LOG_INFO("Set new texture");
                        }
                        ImGui::EndDragDropTarget();
                    }
                }
                ImGui::PopID();
            };

            static ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersInnerV;
            if (ImGui::BeginTable(mat->Name().c_str(), 2, flags))
            {
                ImGui::TableSetupColumn("key");
                ImGui::TableSetupColumn("value");
                //ImGui::TableHeadersRow();
                ImGui::TableNextRow();
                //Material Type
                {
                    ImGui::TableSetColumnIndex(0);
                    auto mat_type = mat->MaterialID();
                    u32 mat_id = mat_type;
                    ImGui::Text("Material Type");
                    ImGui::TableSetColumnIndex(1);
                    if (ImGui::BeginCombo("##MaterialID", EMaterialID::ToString(mat_type)))
                    {
                        for (u32 i = 0; i < EMaterialID::COUNT; i++)
                        {
                            const bool is_selected = (mat_id == i);
                            if (ImGui::Selectable(EMaterialID::ToString((EMaterialID::EMaterialID) i), is_selected))
                                mat_id = i;
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    mat->MaterialID((EMaterialID::EMaterialID) mat_id);
                }
                //Surface Type
                ImGui::TableNextRow();
                {
                    ImGui::TableSetColumnIndex(0);
                    auto surface_type = mat->SurfaceType();
                    u16 surface_type_value = surface_type;
                    ImGui::Text("Surface Type");
                    ImGui::TableSetColumnIndex(1);
                    if (ImGui::BeginCombo("##SurfaceType", ESurfaceType::ToString(surface_type)))
                    {
                        for (u32 i = 0; i < ESurfaceType::COUNT; i++)
                        {
                            const bool is_selected = (surface_type_value == i);
                            if (ImGui::Selectable(ESurfaceType::ToString((ESurfaceType::ESurfaceType) i), is_selected))
                                surface_type_value = i;
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    mat->SurfaceType((ESurfaceType::ESurfaceType) surface_type_value);
                }
                //alpha cull off
                if (mat->SurfaceType() == ESurfaceType::ESurfaceType::kAlphaTest)
                {
                    ImGui::TableNextRow();
                    auto prop = mat->GetProperty("_AlphaCulloff");
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", prop._name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SliderFloat(std::format("##{}", prop._name).c_str(), static_cast<float *>(prop._value_ptr), prop._param[0], prop._param[1]);
                }
                //albedo scope
                {
                    f32 c2_w = ImGui::CalcItemWidth() * 1.35f;
                    ImGui::TableNextRow();
                    func_show_prop(mat, ETextureUsage::kAlbedo,c2_w);
                    ImGui::TableNextRow();
                    func_show_prop(mat, ETextureUsage::kNormal,c2_w);
                    ImGui::TableNextRow();
                    func_show_prop(mat, ETextureUsage::kMetallic,c2_w);
                    ImGui::TableNextRow();
                    func_show_prop(mat, ETextureUsage::kRoughness,c2_w);
                    ImGui::TableNextRow();
                    func_show_prop(mat, ETextureUsage::kSpecular,c2_w);
                    ImGui::TableNextRow();
                    func_show_prop(mat, ETextureUsage::kEmission,c2_w);
                }
                ImGui::EndTable();
            }
        }

        ObjectDetail::ObjectDetail() : ImGuiWidget("ObjectDetail")
        {
            _p_texture_selector = new TextureSelector();
        }
        ObjectDetail::~ObjectDetail()
        {
            DESTORY_PTR(_p_texture_selector);
        }
        void ObjectDetail::Open(const i32 &handle)
        {
            ImGuiWidget::Open(handle);
        }
        void ObjectDetail::Close(i32 handle)
        {
            ImGuiWidget::Close(handle);
        }
        void ObjectDetail::ShowImpl()
        {
            auto s = Selection::First<Shader>();
            auto actor = Selection::First<SceneActor>();
            if (s)
            {
                ShowShaderPannel(s);
            }
            else if (actor)
            {
                ShowSceneActorPannel(actor);
            }
            else
            {
            }
        }
        void ObjectDetail::ShowSceneActorPannel(SceneActor *cur_selected_actor)
        {
            if (cur_selected_actor != nullptr)
            {
                ImGui::BulletText(cur_selected_actor->Name().c_str());
                ImGui::SameLine();
                i32 new_comp_count = 0;
                i32 selected_new_comp_index = -1;
                if (ImGui::BeginCombo(" ", "+ Add Component"))
                {
                    auto &type_str = Component::GetAllComponentTypeStr();
                    for (auto &comp_type: type_str)
                    {
                        if (ImGui::Selectable(comp_type.c_str(), new_comp_count == selected_new_comp_index))
                            selected_new_comp_index = new_comp_count;
                        if (selected_new_comp_index == new_comp_count)
                        {
                            if (comp_type == Component::GetTypeName(StaticMeshComponent::GetStaticType()))
                                cur_selected_actor->AddComponent<StaticMeshComponent>();
                            else if (comp_type == Component::GetTypeName(SkinedMeshComponent::GetStaticType()))
                                cur_selected_actor->AddComponent<SkinedMeshComponent>();
                            else if (comp_type == Component::GetTypeName(LightComponent::GetStaticType()))
                            {
                                cur_selected_actor->AddComponent<LightComponent>();
                            }
                            else if (comp_type == Component::GetTypeName(CameraComponent::GetStaticType()))
                            {
                                cur_selected_actor->AddComponent<CameraComponent>();
                            }
                        }
                        ++new_comp_count;
                    }
                    ImGui::EndCombo();
                }
                u32 comp_index = 0;
                Component *comp_will_remove = nullptr;

                if (cur_selected_actor->HasComponent<TransformComponent>())
                {
                    DrawComponent<TransformComponent>("TransformComponent", cur_selected_actor, [this](TransformComponent *comp)
                                                      {
                                                              auto pos = comp->GetPosition();
                                                              auto rot = comp->GetEuler();
                                                              auto scale = comp->GetScale();
                                                              f32 column_widget = Size().x * 0.2f;
                                                              DrawVec3Control("Position", pos, 0.f, column_widget);
                                                              DrawVec3Control("Rotation", rot, 0.f, column_widget);
                                                              DrawVec3Control("Scale", scale, 1.f, column_widget);
                                                              comp->SetPosition(pos);
                                                              comp->SetRotation(rot);
                                                              comp->SetScale(scale); });
                }
                if (cur_selected_actor->HasComponent<LightComponent>())
                {
                    DrawComponent<LightComponent>("LightComponent", cur_selected_actor, [this](LightComponent *comp)
                                                  {
                                                          auto light = static_cast<LightComponent *>(comp);
                                                          static const char *items[] = {"Directional", "Point", "Spot"};
                                                          int item_current_idx = 0;
                                                          switch (light->LightType())
                                                          {
                                                              case Ailu::ELightType::kDirectional:
                                                                  item_current_idx = 0;
                                                                  break;
                                                              case Ailu::ELightType::kPoint:
                                                                  item_current_idx = 1;
                                                                  break;
                                                              case Ailu::ELightType::kSpot:
                                                                  item_current_idx = 2;
                                                                  break;
                                                          }
                                                          const char *combo_preview_value = items[item_current_idx];
                                                          if (ImGui::BeginCombo("LightType", combo_preview_value, 0))
                                                          {
                                                              for (int n = 0; n < IM_ARRAYSIZE(items); n++)
                                                              {
                                                                  const bool is_selected = (item_current_idx == n);
                                                                  if (ImGui::Selectable(items[n], is_selected))
                                                                      item_current_idx = n;
                                                                  if (is_selected)
                                                                      ImGui::SetItemDefaultFocus();
                                                              }
                                                              ImGui::EndCombo();
                                                          }
                                                          ImGui::SliderFloat("Intensity", &light->_intensity, 0.0f, 16.0f);
                                                          ImGui::ColorEdit3("Color", static_cast<float *>(light->_light._light_color.data));
                                                          if (item_current_idx == 0) light->LightType(ELightType::kDirectional);
                                                          else if (item_current_idx == 1)
                                                              light->LightType(ELightType::kPoint);
                                                          else
                                                              light->LightType(ELightType::kSpot);

                                                          if (light->LightType() == ELightType::kPoint)
                                                          {
                                                              auto &light_data = light->_light;
                                                              ImGui::DragFloat("Radius", &light_data._light_param.x, 1.0, 0.0, 5000.0f);
                                                          }
                                                          else if (light->LightType() == ELightType::kSpot)
                                                          {
                                                              auto &light_data = light->_light;
                                                              ImGui::DragFloat("Radius", &light_data._light_param.x);
                                                              ImGui::SliderFloat("InnerAngle", &light_data._light_param.y, 0.0f, light_data._light_param.z);
                                                              ImGui::SliderFloat("OuterAngle", &light_data._light_param.z, 1.0f, 180.0f);
                                                          }
                                                          bool b_cast_shadow = light->CastShadow();
                                                          ImGui::Checkbox("CastShadow", &b_cast_shadow);
                                                          if (b_cast_shadow != light->CastShadow())
                                                              light->CastShadow(b_cast_shadow);
                                                          if (b_cast_shadow)
                                                          {
                                                              auto &shadow_data = light->_shadow;
                                                              ImGui::SliderFloat("ConstantBias", &shadow_data._constant_bias, 0.0f, 0.4f);
                                                              ImGui::SliderFloat("SlopeBias", &shadow_data._slope_bias, 0.0f, 0.4f);
                                                          } });
                }
                if (cur_selected_actor->HasComponent<StaticMeshComponent>())
                {
                    DrawComponent<StaticMeshComponent>("StaticMeshComponent", cur_selected_actor, [this](StaticMeshComponent *comp)
                                                       {
                            bool is_skined_comp = comp->GetType() == SkinedMeshComponent::GetStaticType();
                            const static ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
                            if(ImGui::BeginTable("##static_mesh_comp",2,flags))
                            {
                                const static ImGuiComboFlags combo_flags = ImGuiComboFlags_NoArrowButton;
                                // Setup columns
                                ImGui::TableSetupColumn("##key");
                                ImGui::TableSetupColumn("##value");
                                //mesh scope
                                {
                                    ImGui::TableNextRow();
                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::Text("StaticMesh");
                                    ImGui::TableSetColumnIndex(1);
                                    u16 mesh_index = 0;
                                    static int s_mesh_selected_index = -1;
                                    if (ImGui::BeginCombo("##mesh",comp->GetMesh()->Name().c_str(),combo_flags))
                                    {
                                        for (auto it = g_pResourceMgr->ResourceBegin<Mesh>(); it != g_pResourceMgr->ResourceEnd<Mesh>(); it++)
                                        {
                                            auto mesh = ResourceMgr::IterToRefPtr<Mesh>(it);
                                            if (is_skined_comp && !dynamic_cast<SkinedMesh *>(mesh.get()))
                                                continue;
                                            if (ImGui::Selectable(mesh->Name().c_str(), s_mesh_selected_index == mesh_index))
                                                s_mesh_selected_index = mesh_index;
                                            if (s_mesh_selected_index == mesh_index)
                                            {
                                                comp->SetMesh(mesh);
                                                g_pSceneMgr->MarkCurSceneDirty();
                                            }
                                            ++mesh_index;
                                        }
                                        ImGui::EndCombo();
                                    }
                                    if (ImGui::BeginDragDropTarget())
                                    {
                                        const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kDragAssetMesh.c_str());
                                        if (payload)
                                        {
                                            auto new_mesn = (*(Asset **) (payload->Data))->AsRef<Mesh>();
                                            comp->SetMesh(new_mesn);
                                        }
                                        ImGui::EndDragDropTarget();
                                    }
                                }
                                ImGui::TableNextRow();
                                ImGui::Separator();
                                auto& materials = comp->GetMaterials();
                                //material scope
                                {
                                    for (int i = 0; i < comp->GetMesh()->SubmeshCount(); i++)
                                    {
                                        ImGui::TableSetColumnIndex(0);
                                        ImGui::Text("Material_%d",i);
                                        ImGui::TableSetColumnIndex(1);

                                        Ref<Material> mat = i < materials.size() && materials[i] != nullptr? materials[i] : nullptr;
                                        u16 mesh_index = 0;
                                        static int s_mat_selected_index = -1;
                                        if (ImGui::BeginCombo("##Select Material: ", mat ? mat->Name().c_str() : "Missing",combo_flags))
                                        {
                                            for (auto it = g_pResourceMgr->ResourceBegin<Material>(); it != g_pResourceMgr->ResourceEnd<Material>(); it++)
                                            {
                                                auto new_mat = ResourceMgr::IterToRefPtr<Material>(it);
                                                if (ImGui::Selectable(new_mat->Name().c_str(), s_mat_selected_index == i))
                                                    s_mat_selected_index = i;
                                                if (s_mat_selected_index == i)
                                                {
                                                    comp->SetMaterial(new_mat, i);
                                                    g_pSceneMgr->MarkCurSceneDirty();
                                                    s_mat_selected_index = -1;
                                                }
                                            }
                                            ImGui::EndCombo();
                                        }
                                        if (ImGui::BeginDragDropTarget())
                                        {
                                            const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(ImGuiWidget::kDragAssetMaterial.c_str());
                                            if (payload)
                                            {
                                                auto material = *(Asset **) (payload->Data);
                                                auto mat_ref = material->AsRef<Material>();
                                                comp->SetMaterial(mat_ref, i);
                                                g_pSceneMgr->MarkCurSceneDirty();
                                                LOG_INFO("Set new material");
                                            }
                                            ImGui::EndDragDropTarget();
                                        }
                                        ImGui::TableNextRow();
                                    }
                                }
                                ImGui::EndTable();
                                for(auto mat : materials)
                                {
                                    if (mat != nullptr)
                                    {
                                        bool is_open = ImGui::TreeNodeEx(mat->Name().c_str());
                                        if (is_open)
                                        {
                                            auto standard_mat = dynamic_cast<StandardMaterial*>(mat.get());
                                            if (standard_mat)
                                                DrawInternalStandardMaterial(standard_mat);
                                            ImGui::TreePop();
                                        }
                                    }
                                }
                            }

//                            auto &materials = comp->GetMaterials();
//                            ImGui::Text("Material: %d", materials.size());
//                            if (ImGui::Button("Add Material"))
//                            {
//                                comp->AddMaterial(nullptr);
//                                g_pSceneMgr->MarkCurSceneDirty();
//                            };
//                            ImGui::SameLine();
//                            if (ImGui::Button("Remove Material"))
//                            {
//                                comp->RemoveMaterial(static_cast<u16>(materials.size() - 1));
//                                g_pSceneMgr->MarkCurSceneDirty();
//                            };
//                            static int s_mat_selected_index = -1;
//                            for (int i = 0; i < comp->GetMesh()->SubmeshCount(); i++)
//                            {
//                                ImGui::PushID(i);
//                                if (i < materials.size() && materials[i] != nullptr)
//                                {
//
//                                    auto mat = materials[i];
//                                    String slot_label = std::format("{}: {}", i, mat ? mat->Name() : "Select Material");
//                                    if (ImGui::CollapsingHeader(slot_label.c_str()))
//                                    {
//                                        static ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersInnerV;
//                                        if (ImGui::BeginTable(mat->Name().c_str(), 2, flags))
//                                        {
//                                            ImGui::TableSetupColumn("PropName");
//                                            ImGui::TableSetupColumn("PropValue");
//                                            //ImGui::TableHeadersRow();
//                                            ImGui::TableNextRow();
//                                            {
//                                                ImGui::TableSetColumnIndex(0);
//                                                ImGui::Text("Material");
//                                                ImGui::TableSetColumnIndex(1);
//                                                ImGuiStyle &style = ImGui::GetStyle();
//                                                float w = ImGui::CalcItemWidth();
//                                                float spacing = style.ItemInnerSpacing.x;
//                                                float button_sz = ImGui::GetFrameHeight();
//                                                ImGui::PushItemWidth((w - spacing * 2.0f - button_sz * 2.0f) * 2.0f);
//                                                if (ImGui::BeginCombo("##Select Material: ", mat ? mat->Name().c_str() : "missing"))
//                                                {
//                                                    for (auto it = g_pResourceMgr->ResourceBegin<Material>(); it != g_pResourceMgr->ResourceEnd<Material>(); it++)
//                                                    {
//                                                        auto mat = ResourceMgr::IterToRefPtr<Material>(it);
//                                                        if (ImGui::Selectable(mat->Name().c_str(), s_mat_selected_index == i))
//                                                            s_mat_selected_index = i;
//                                                        if (s_mat_selected_index == i)
//                                                        {
//                                                            comp->SetMaterial(mat, i);
//                                                            g_pSceneMgr->MarkCurSceneDirty();
//                                                            s_mat_selected_index = -1;
//                                                        }
//                                                    }
//                                                    ImGui::EndCombo();
//                                                }
//                                                ImGui::PopItemWidth();
//                                            }
//                                            if (mat)
//                                            {
//                                                ImGui::TableNextRow();
//                                                int shader_count = 0;
//                                                static int s_shader_selected_index = -1;
//                                                if (ImGui::BeginCombo("Select Shader: ", mat->GetShader()->Name().c_str()))
//                                                {
//
//                                                    for (auto it = g_pResourceMgr->ResourceBegin<Shader>(); it != g_pResourceMgr->ResourceEnd<Shader>(); it++)
//                                                    {
//                                                        auto shader = ResourceMgr::IterToRefPtr<Shader>(it);
//                                                        if (ImGui::Selectable(shader->Name().c_str(), s_shader_selected_index == shader_count))
//                                                            s_shader_selected_index = shader_count;
//                                                        if (s_shader_selected_index == shader_count)
//                                                            mat->ChangeShader(shader.get());
//                                                        ++shader_count;
//                                                    }
//                                                    ImGui::EndCombo();
//                                                }
//                                            }
//                                            ImGui::EndTable();
//                                        }
//                                        if (mat)
//                                        {
////                                            auto standard_mat = dynamic_cast<StandardMaterial *>(mat.get());
////                                            if (standard_mat)
////                                            {
////                                                DrawInternalStandardMaterial(property_handle, standard_mat, selector_window);
////                                            }
////                                            else
////                                            {
////                                                for (auto &prop: mat->GetAllProperties())
////                                                {
////                                                    DrawProperty(property_handle, prop.second, mat.get(), selector_window);
////                                                }
////                                            }
//                                        }
//                                    }
//                                }
//                                else
//                                {
//                                    ImGui::Button("Null");
//                                }
//                                if (ImGui::BeginDragDropTarget())
//                                {
//                                    const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(ImGuiWidget::kDragAssetMaterial.c_str());
//                                    if (payload)
//                                    {
//                                        auto material = *(Asset **) (payload->Data);
//                                        auto mat_ref = material->AsRef<Material>();
//                                        comp->SetMaterial(mat_ref, i);
//                                    }
//                                    ImGui::EndDragDropTarget();
//                                }
//                                ImGui::PopID();
//                            };
                                                       });
                }
                if (cur_selected_actor->HasComponent<CameraComponent>())
                {
                    DrawComponent<CameraComponent>("CameraComponent", cur_selected_actor, [this](CameraComponent *comp)
                                                   {
                                                       auto &cam = comp->_camera;
                                                       const char *items[]{"Orthographic", "Perspective"};
                                                       int item_current_idx = 0;
                                                       if (cam.Type() == ECameraType::kPerspective) item_current_idx = 1;
                                                       const char *combo_preview_value = items[item_current_idx];
                                                       if (ImGui::BeginCombo("CameraType", combo_preview_value, 0))
                                                       {
                                                           for (int n = 0; n < IM_ARRAYSIZE(items); n++)
                                                           {
                                                               const bool is_selected = (item_current_idx == n);
                                                               if (ImGui::Selectable(items[n], is_selected))
                                                                   item_current_idx = n;
                                                               if (is_selected)
                                                                   ImGui::SetItemDefaultFocus();
                                                           }
                                                           ImGui::EndCombo();
                                                       }
                                                       float cam_near = cam.Near(), cam_far = cam.Far(), aspect = cam.Aspect();
                                                       if (item_current_idx == 0) cam.Type(ECameraType::kOrthographic);
                                                       else
                                                           cam.Type(ECameraType::kPerspective);
                                                       ImGui::DragFloat("Near", &cam_near, 1.0f, 0, 1000.0f);
                                                       ImGui::DragFloat("Far", &cam_far, 1.0f, cam_near, 500000.f);
                                                       ImGui::SliderFloat("Aspect", &aspect, 0.01f, 5.f);
                                                       if (cam.Type() == ECameraType::kPerspective)
                                                       {
                                                           float fov_h = cam.FovH();
                                                           ImGui::SliderFloat("Horizontal Fov", &fov_h, 0.0f, 135.0f);
                                                           cam.FovH(fov_h);
                                                       }
                                                       else
                                                       {
                                                           float size = cam.Size();
                                                           ImGui::SliderFloat("Size", &size, 0.0f, 1000.0f);
                                                           cam.Size(size);
                                                       }
                                                       cam.Near(cam_near);
                                                       cam.Far(cam_far);
                                                       cam.Aspect(aspect); });
                }
                _p_texture_selector->Show();
            }
        }

        void ObjectDetail::ShowShaderPannel(Shader *s)
        {
            f32 indent = 10.0f;
            ImGui::Text("Name: %s", s->Name().c_str());
            for (u16 i = 0; i < s->PassCount(); i++)
            {
                auto &pass = s->GetPassInfo(i);
                ImGui::Indent(indent);
                ImGui::Text("Pass_%d: %s", i, pass._name.c_str());
                {
                    ImGui::Indent(indent);
                    ImGui::Text("vertex entry: %s", pass._vert_entry.c_str());
                    ImGui::Text("pixel entry: %s", pass._pixel_entry.c_str());
                    {
                        ImGui::Text("local keywords: ");
                        for (auto &kw_group: pass._keywords)
                        {
                            String kws;
                            for (auto &kw: kw_group)
                            {
                                kws.append(kw);
                                kws.append(" ");
                            }
                            ImGui::Text("group: %s", kws.c_str());
                        }
                        ImGui::Text("variants: ");
                        for (auto &it: pass._variants)
                        {
                            auto &[hash, variant] = it;
                            String kws;
                            for (auto &kw: variant._active_keywords)
                            {
                                kws.append(kw);
                                kws.append(" ");
                            }
                            ImGui::Text(": %s", kws.c_str());
                        }
                    }
                    ImGui::Unindent(indent);
                }
                ImGui::Unindent(indent);
            }
        }
    }// namespace Editor
}// namespace Ailu
