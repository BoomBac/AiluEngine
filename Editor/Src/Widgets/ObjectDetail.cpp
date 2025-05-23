#include "Widgets/ObjectDetail.h"
#include "Common/Selection.h"
#include "Ext/imgui/imgui.h"
#include "Ext/imgui/imgui_internal.h"
#include "Widgets/CommonTextureWidget.h"

#include "Framework/Common/Asset.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "Render/Renderer.h"

namespace Ailu
{
    namespace Editor
    {
        static u16 s_mini_tex_size = 64;
        static const String kNullStr = "null";
        namespace TreeStats
        {
            static i16 s_cur_opened_texture_selector = -1;
            static bool s_is_texture_selector_open = true;
        }// namespace TreeStats
        static void DrawVec3Control(const String &label, Vector3f &v, f32 reset_value = 0.f, f32 column_width = 80.0f)
        {
            ImGui::PushID(label.c_str());
            ImGui::Columns(2);
            ImGui::SetColumnWidth(0, column_width * 1.25f);
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
        static void DrawComponent(const String &name, T *comp, ECS::Entity entity, Drawer drawer)
        {
            static const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap;

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
                g_pSceneMgr->ActiveScene()->GetRegister().RemoveComponent<T>(entity);
                g_pSceneMgr->MarkCurSceneDirty();
            }
            ImGui::Spacing();
        }

        template<typename Iter, typename T>
        static bool DrawResourceDropdown(const String &label, Iter begin, Iter end, Ref<T> &out_ptr, String prev_str = kNullStr)
        {
            bool ret = false;
            out_ptr = nullptr;
            static int s_selected_index = -1;
            u16 cur_index = 0u;
            if (ImGui::BeginCombo(label.c_str(), prev_str.c_str()))
            {
                if (ImGui::Selectable(kNullStr.c_str(), s_selected_index == cur_index))
                {
                    s_selected_index = 0u;
                    ret = true;
                }
                ++cur_index;
                for (auto it = begin; it != end; ++it)
                {
                    bool is_selected = s_selected_index == cur_index;
                    Ref<T> obj = ResourceMgr::IterToRefPtr<T>(it);
                    is_selected = ImGui::Selectable(obj->Name().c_str(), &is_selected);
                    if (is_selected)
                    {
                        s_selected_index = cur_index;
                        ret = true;
                        out_ptr = obj;
                    }
                    ++cur_index;
                }
                ImGui::EndCombo();
            }
            return ret;
        }

        static void DrawShaderProperty(u32 &property_handle, ShaderPropertyInfo &prop, Material *obj)
        {
            ++property_handle;
            switch (prop._type)
            {
                case Ailu::EShaderPropertyType::kFloat:
                    ImGui::DragFloat(prop._prop_name.c_str(), static_cast<float *>(prop._value_ptr));
                    break;
                case Ailu::EShaderPropertyType::kBool:
                {
                    auto bool_value = (f32 *) prop._value_ptr;
                    bool old_value = *bool_value == 1.0f;
                    bool new_value = old_value;
                    ImGui::Checkbox(prop._prop_name.c_str(), &new_value);
                    if (new_value != old_value)
                    {
                        *bool_value = (f32) new_value;
                        auto mat = dynamic_cast<Material *>(obj);
                        auto keywords = mat->GetShader()->GetKeywordsProps();
                        if (auto it = keywords.find(prop._value_name); it != keywords.end())
                        {
                            i32 cur_kw_idx = (i32) new_value;
                            i32 other_kw_idx = (cur_kw_idx + 1) % 2;
                            obj->EnableKeyword(it->second[cur_kw_idx]);
                            //obj->DisableKeyword(it->second[other_kw_idx]);
                        }
                    }
                }
                break;
                case Ailu::EShaderPropertyType::kEnum:
                {
                    auto mat = dynamic_cast<Material *>(obj);
                    if (mat != nullptr)
                    {
                        auto keywords = mat->GetShader()->GetKeywordsProps();
                        auto it = keywords.find(prop._value_name);
                        if (it != keywords.end())
                        {
                            f32 prop_value = prop._default_value.x;
                            if (ImGui::BeginCombo(prop._prop_name.c_str(), it->second[prop_value].substr(it->second[prop_value].rfind("_") + 1).c_str()))
                            {
                                static int s_selected_index = -1;
                                for (int i = 0; i < it->second.size(); i++)
                                {
                                    auto x = it->second[i].rfind("_");
                                    auto enum_str = it->second[i].substr(x + 1);
                                    if (ImGui::Selectable(enum_str.c_str(), i == s_selected_index))
                                        s_selected_index = i;
                                    if (s_selected_index == i)
                                    {
                                        mat->EnableKeyword(it->second[i]);
                                        prop._default_value.x = (f32) i;
                                    }
                                }
                                ImGui::EndCombo();
                            }
                        }
                        else
                            ImGui::Text("Eunm %s value missing", prop._value_name.c_str());
                    }
                    else
                        ImGui::Text("Non-Material Eunm %s value missing", prop._prop_name.c_str());
                }
                break;
                case Ailu::EShaderPropertyType::kVector:
                    ImGui::DragFloat4(prop._prop_name.c_str(), static_cast<Vector3f *>(prop._value_ptr)->data);
                    break;
                case Ailu::EShaderPropertyType::kColor:
                {
                    ImGuiColorEditFlags_ flag = ImGuiColorEditFlags_None;
                    if (prop.IsHDRProperty())
                    {
                        flag = (ImGuiColorEditFlags_) (ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                        // f32 intensity = prop.GetHDRIntensity();
                        // ImGui::HDRColorEdit4(prop._prop_name.c_str(), static_cast<Vector4f *>(prop._value_ptr)->data, &intensity,flag);
                        // prop.SetHDRIntensity(intensity);
                    }
                    ImGui::ColorEdit4(prop._prop_name.c_str(), static_cast<Vector4f *>(prop._value_ptr)->data,flag);
                }
                break;
                case Ailu::EShaderPropertyType::kRange:
                {
                    ImGui::Text("%s", prop._prop_name.c_str());
                    ImGui::SameLine();
                    f32 old_value = prop.GetValue<f32>();
                    f32 new_value = old_value;
                    ImGui::SliderFloat(std::format("##{}", prop._prop_name).c_str(), &new_value, prop._default_value[0], prop._default_value[1]);
                    new_value = std::clamp(new_value, prop._default_value[0], prop._default_value[1]);
                    if (new_value != old_value)
                        prop.SetValue(new_value);
                }
                break;
                case Ailu::EShaderPropertyType::kTexture2D:
                {
                    ImGui::Text("Texture2D : %s", prop._prop_name.c_str());
                    auto tex = (Texture *) prop._value_ptr;
                    u32 cur_tex_id = tex ? tex->ID() : Texture::s_p_default_white->ID();
                    //auto& desc = std::static_pointer_cast<D3DTexture2D>(tex == nullptr ? TexturePool::GetDefaultWhite() : tex)->GetGPUHandle();
                    ImVec2 vMin = ImGui::GetWindowContentRegionMin();
                    ImVec2 vMax = ImGui::GetWindowContentRegionMax();
                    float contentWidth = (vMax.x - vMin.x);
                    float centerX = (contentWidth - s_mini_tex_size) * 0.5f;
                    ImGui::SetCursorPosX(centerX);
                    if (ImGui::ImageButton(TEXTURE_HANDLE_TO_IMGUI_TEXID(g_pResourceMgr->Get<Texture2D>(cur_tex_id)->GetNativeTextureHandle()), ImVec2(s_mini_tex_size, s_mini_tex_size)))
                    {
                        //selector_window.Open(property_handle);
                    }
                }
                break;
                case Ailu::EShaderPropertyType::kTexture3D:
                {
                    ImGui::Text("Texture3D : %s", prop._prop_name.c_str());
                    auto tex = (Texture *) prop._value_ptr;
                    Ref<Texture3D> ptr;
                    if (DrawResourceDropdown("##animclip", g_pResourceMgr->ResourceBegin<Texture3D>(), g_pResourceMgr->ResourceEnd<Texture3D>(),
                                             ptr, tex ? tex->Name() : kNullStr))
                    {
                        obj->SetTexture(prop._value_name, ptr.get());
                    }
                }
                break;
                default:
                    LOG_WARNING("prop: {} value type havn't be handle!", prop._prop_name)
                    break;
            }
        }

        static void DrawMaterialDetailPanel(Material *mat)
        {
            u32 property_handle = 0;
            for (auto prop: mat->GetShaderProperty())
            {
                DrawShaderProperty(property_handle, *prop, mat);
            }
        }

        static void DrawInternalStandardMaterial(StandardMaterial *mat)
        {
            i32 mat_prop_index = 0;
            static auto func_show_prop = [&](StandardMaterial *mat, ETextureUsage tex_usage, f32 width)
            {
                ImGui::PushID(mat_prop_index++);

                const auto &standard_prop_info = StandardMaterial::StandardPropertyName::GetInfoByUsage(tex_usage);
                Texture *tex = nullptr;
                if (tex_usage != ETextureUsage::kAnisotropy)
                {
                    tex = const_cast<Texture *>(mat->MainTex(tex_usage));
                    bool use_tex = tex != nullptr;
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Checkbox("##cb", &use_tex);
                    ImGui::SameLine();
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(ImGuiWidget::kDragAssetTexture2D.c_str()))
                        {
                            auto asset = *(Asset **) (payload->Data);
                            auto tex_ref = asset->AsRef<Texture2D>();
                            mat->SetTexture(tex_usage, tex_ref.get());
                            use_tex = true;
                            LOG_INFO("Set new texture");
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (mat->IsTextureUsed(tex_usage) != use_tex && !use_tex)
                    {
                        mat->SetTexture(standard_prop_info._tex_name, nullptr);
                    }
                }
                else
                {
                    ImGui::TableSetColumnIndex(0);
                }
                ImGui::Text("%s", standard_prop_info._group_name.c_str());
                ImGui::TableSetColumnIndex(1);

                ImGui::PushMultiItemsWidths(1, width);
                const ImVec2 preview_tex_size = ImVec2(s_mini_tex_size, s_mini_tex_size);
                if (tex == nullptr)
                {
                    auto prop = mat->GetShaderProperty(standard_prop_info._value_name);
                    if (prop)
                    {
                        switch (prop->_type)
                        {
                            case Ailu::EShaderPropertyType::kVector:
                            {
                                ImGui::DragFloat4("##f4", static_cast<Vector4f *>(prop->_value_ptr)->data);
                                mat->SetVector(prop->_value_name, *static_cast<Vector4f *>(prop->_value_ptr));
                            }
                            break;
                            case Ailu::EShaderPropertyType::kColor:
                            {
                                ImGuiColorEditFlags_ flag = ImGuiColorEditFlags_None;
                                if (prop->IsHDRProperty())
                                {
                                    flag = (ImGuiColorEditFlags_) (ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                                    //f32 intensity = prop->GetHDRIntensity();
                                    //ImGui::HDRColorEdit4("##c4", static_cast<Vector4f *>(prop->_value_ptr)->data, &intensity,flag);
                                    //prop->SetHDRIntensity(intensity);
                                }
                                ImGui::ColorEdit4("##c4", static_cast<Vector4f *>(prop->_value_ptr)->data);
                                mat->SetVector(prop->_value_name, *static_cast<Vector4f *>(prop->_value_ptr));
                            }
                            break;
                            case Ailu::EShaderPropertyType::kRange:
                            {
                                ImGui::SliderFloat("##r", static_cast<float *>(prop->_value_ptr), prop->_default_value[0], prop->_default_value[1]);
                                mat->SetFloat(prop->_value_name, *static_cast<f32 *>(prop->_value_ptr));
                            }
                            break;
                        }
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
                            mat->SetTexture(tex_usage, tex_ref.get());
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
                    auto prop = mat->GetShaderProperty("_AlphaCulloff");
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", prop->_prop_name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    auto alpha_cull_off = (f32 *) prop->_value_ptr;
                    ImGui::SliderFloat(std::format("##{}", prop->_prop_name).c_str(), alpha_cull_off, 0.f, 1.f);
                    mat->SetFloat("_AlphaCulloff", *alpha_cull_off);
                }
                //cull mode
                ImGui::TableNextRow();
                {
                    const static String s_cullmode_str[] = {"Off", "Fornt", "Back"};
                    ImGui::TableSetColumnIndex(0);
                    auto cull = mat->GetCullMode();
                    u16 cull_value = (u16) cull;
                    ImGui::Text("Cull Mode");
                    ImGui::TableSetColumnIndex(1);
                    if (ImGui::BeginCombo("##CullMode", s_cullmode_str[(u16) cull].c_str()))
                    {
                        for (u32 i = 0; i < 3; i++)
                        {
                            const bool is_selected = (cull_value == i);
                            if (ImGui::Selectable(s_cullmode_str[i].c_str(), is_selected))
                            {
                                cull_value = i;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    mat->SetCullMode((ECullMode) cull_value);
                }
                //albedo scope
                {
                    f32 c2_w = ImGui::CalcItemWidth() * 1.35f;
                    ImGui::TableNextRow();
                    func_show_prop(mat, ETextureUsage::kAlbedo, c2_w);
                    ImGui::TableNextRow();
                    func_show_prop(mat, ETextureUsage::kNormal, c2_w);
                    ImGui::TableNextRow();
                    func_show_prop(mat, ETextureUsage::kMetallic, c2_w);
                    ImGui::TableNextRow();
                    func_show_prop(mat, ETextureUsage::kRoughness, c2_w);
                    ImGui::TableNextRow();
                    func_show_prop(mat, ETextureUsage::kAnisotropy, c2_w);
                    ImGui::TableNextRow();
                    func_show_prop(mat, ETextureUsage::kSpecular, c2_w);
                    ImGui::TableNextRow();
                    func_show_prop(mat, ETextureUsage::kEmission, c2_w);
                }
                ImGui::EndTable();
            }
        }

        static u16 DrawEnumProperty(const String &title, const String enum_str[], u16 arr_len, u16 default_index = 0u)
        {
            u16 selected_index = default_index;
            if (ImGui::BeginCombo(title.c_str(), enum_str[default_index].c_str(), 0))
            {
                for (int n = 0; n < arr_len; n++)
                {
                    const bool is_selected = (default_index == n);
                    if (ImGui::Selectable(enum_str[n].c_str(), is_selected))
                        selected_index = n;
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            return selected_index;
        }

        template<typename T>
        static void MeshCompDrawer(T *comp)
        {
            constexpr bool is_skined_comp = std::is_same<T, ECS::CSkeletonMesh>::value;
            const static ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
            if (ImGui::BeginTable("##static_mesh_comp", 2, flags))
            {
                const static ImGuiComboFlags combo_flags = ImGuiComboFlags_NoArrowButton;
                // Setup columns
                ImGui::TableSetupColumn("##key");
                ImGui::TableSetupColumn("##value");
                //mesh scope
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text(is_skined_comp ? "SkeletionMesh" : "StaticMesh");
                    ImGui::TableSetColumnIndex(1);
                    u16 mesh_index = 0;
                    static int s_mesh_selected_index = -1;
                    const char *preview_str = comp->_p_mesh ? comp->_p_mesh->Name().c_str() : "null";
                    if (ImGui::BeginCombo("##mesh", preview_str, combo_flags))
                    {
                        if constexpr (is_skined_comp)
                        {
                            for (auto it = g_pResourceMgr->ResourceBegin<SkeletonMesh>(); it != g_pResourceMgr->ResourceEnd<SkeletonMesh>(); it++)
                            {
                                auto mesh = ResourceMgr::IterToRefPtr<SkeletonMesh>(it);
                                if (ImGui::Selectable(mesh->Name().c_str(), s_mesh_selected_index == mesh_index))
                                    s_mesh_selected_index = mesh_index;
                                if (s_mesh_selected_index == mesh_index)
                                {
                                    comp->_p_mesh = mesh;
                                    comp->_transformed_aabbs.resize(comp->_p_mesh->SubmeshCount() + 1);
                                    g_pSceneMgr->MarkCurSceneDirty();
                                }
                                ++mesh_index;
                            }
                        }
                        else
                        {
                            for (auto it = g_pResourceMgr->ResourceBegin<Mesh>(); it != g_pResourceMgr->ResourceEnd<Mesh>(); it++)
                            {
                                auto mesh = ResourceMgr::IterToRefPtr<Mesh>(it);
                                if (ImGui::Selectable(mesh->Name().c_str(), s_mesh_selected_index == mesh_index))
                                    s_mesh_selected_index = mesh_index;
                                if (s_mesh_selected_index == mesh_index)
                                {
                                    comp->_p_mesh = mesh;
                                    comp->_transformed_aabbs.resize(comp->_p_mesh->SubmeshCount() + 1);
                                    g_pSceneMgr->MarkCurSceneDirty();
                                }
                                ++mesh_index;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if constexpr (is_skined_comp)
                    {
                    }
                    else
                    {
                        if (ImGui::BeginDragDropTarget())
                        {
                            const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(ImGuiWidget::kDragAssetMesh.c_str());
                            if (payload)
                            {
                                auto new_mesn = (*(Asset **) (payload->Data))->AsRef<Mesh>();
                                comp->_p_mesh = new_mesn;
                            }
                            ImGui::EndDragDropTarget();
                        }
                    }
                }
                ImGui::TableNextRow();
                ImGui::Separator();
                if constexpr (is_skined_comp)
                {
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("AnimClip");
                    ImGui::TableSetColumnIndex(1);
                    Ref<AnimationClip> ptr;
                    if (DrawResourceDropdown("##animclip", g_pResourceMgr->ResourceBegin<AnimationClip>(), g_pResourceMgr->ResourceEnd<AnimationClip>(),
                                             ptr, comp->_anim_clip ? comp->_anim_clip->Name() : kNullStr))
                    {
                        comp->_anim_clip = ptr;
                        g_pSceneMgr->MarkCurSceneDirty();
                    }
                    if (DrawResourceDropdown("##blend_animclip", g_pResourceMgr->ResourceBegin<AnimationClip>(), g_pResourceMgr->ResourceEnd<AnimationClip>(),
                                             ptr, comp->_blend_anim_clip ? comp->_blend_anim_clip->Name() : kNullStr))
                    {
                        comp->_blend_anim_clip = ptr;
                        g_pSceneMgr->MarkCurSceneDirty();
                    }
                    ImGui::TableNextRow();
                    {
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("AnimType: ");
                        ImGui::TableSetColumnIndex(1);
                        int type = comp->_anim_type;
                        ImGui::SliderInt("##AnimType", &type, 0, 2);
                        comp->_anim_type = type;
                    }
                    if (comp->_anim_clip)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("AnimFrame");
                        ImGui::TableSetColumnIndex(1);
                        ImGui::SliderFloat("##AnimFrame", &comp->_anim_time, -1.0f, comp->_anim_clip ? comp->_anim_clip->Duration() : 1.0f, "%.2f");
                        bool is_loop = comp->_anim_clip->IsLooping();
                        ImGui::Checkbox("Loop", &is_loop);
                        comp->_anim_clip->IsLooping(is_loop);
                    }
                    ImGui::TableNextRow();
                    ImGui::Separator();
                }
                auto &materials = comp->_p_mats;
                //material scope
                {
                    if (comp->_p_mesh)
                    {
                        for (int i = 0; i < comp->_p_mesh->SubmeshCount(); i++)
                        {
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("Material_%d", i);
                            ImGui::TableSetColumnIndex(1);

                            Ref<Material> mat = i < materials.size() && materials[i] != nullptr ? materials[i] : nullptr;
                            u16 mesh_index = 0;
                            static int s_mat_selected_index = -1;
                            if (ImGui::BeginCombo("##Select Material: ", mat ? mat->Name().c_str() : "Missing", combo_flags))
                            {
                                for (auto it = g_pResourceMgr->ResourceBegin<Material>(); it != g_pResourceMgr->ResourceEnd<Material>(); it++)
                                {
                                    auto new_mat = ResourceMgr::IterToRefPtr<Material>(it);
                                    if (ImGui::Selectable(new_mat->Name().c_str(), s_mat_selected_index == i))
                                        s_mat_selected_index = i;
                                    if (s_mat_selected_index == i)
                                    {
                                        //AL_ASSERT(i < comp->_p_mats.size());
                                        comp->_p_mats.resize(i + 1);
                                        comp->_p_mats[i] = new_mat;
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
                                    //AL_ASSERT(i < comp->_p_mats.size());
                                    comp->_p_mats.resize(i + 1);
                                    comp->_p_mats[i] = mat_ref;
                                    g_pSceneMgr->MarkCurSceneDirty();
                                    LOG_INFO("Set new material");
                                }
                                ImGui::EndDragDropTarget();
                            }
                            ImGui::TableNextRow();
                        }
                    }
                }
                ImGui::EndTable();
                //addition
                Enum* enum_type = Enum::GetEnumByName("EMotionVectorType");
                comp->_motion_vector_type = (ECS::EMotionVectorType)DrawEnum(enum_type,(u32)comp->_motion_vector_type);
                for (auto mat: materials)
                {
                    if (mat != nullptr)
                    {
                        bool is_open = ImGui::TreeNodeEx(mat->Name().c_str());
                        if (is_open)
                        {
                            if (auto standard_mat = dynamic_cast<StandardMaterial *>(mat.get()); standard_mat != nullptr)
                                DrawInternalStandardMaterial(standard_mat);
                            else
                                DrawMaterialDetailPanel(mat.get());
                            ImGui::TreePop();
                        }
                    }
                }
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
            auto s = Selection::FirstObject<Shader>();
            if (s)
            {
                ShowShaderPannel(s);
            }
            ShowSceneActorPannel(Selection::FirstEntity());
        }
        void ObjectDetail::ShowSceneActorPannel(ECS::Entity entity)
        {
            if (entity == ECS::kInvalidEntity)
                return;
            auto &scene_register = g_pSceneMgr->ActiveScene()->GetRegister();
            auto tag_comp = scene_register.GetComponent<ECS::TagComponent>(entity);
            if (tag_comp == nullptr)
            {
                Selection::RemoveSlection();
                return;
            }
            bool cur_actor_active = true;
            ImGui::Checkbox(tag_comp->_name.c_str(), &cur_actor_active);
            //if (cur_actor_active != cur_selected_actor->IsActive())
            //{
            //    cur_selected_actor->IsActive(cur_actor_active);
            //    g_pSceneMgr->MarkCurSceneDirty();
            //}
            ImGui::SameLine();
            u16 selected_new_comp_index = (u16) (-1);
            static Vector<String> s_comp_tag = {
                    "LightComponent", "CameraComponent", "StaticMeshComponent", "LightProbeComponent", "RigidComponent", "ColliderComponent", "SkeletonComponent", "VXGIComponent"};
            if (ImGui::BeginCombo(" ", "+ Add Component"))
            {
                for (u16 i = 0; i < s_comp_tag.size(); i++)
                {
                    if (ImGui::Selectable(s_comp_tag[i].c_str(), i == selected_new_comp_index))
                        selected_new_comp_index = i;
                    if (selected_new_comp_index == i)
                    {
                        if (i == 0)
                        {
                            scene_register.AddComponent<ECS::LightComponent>(entity);
                        }
                        else if (i == 1)
                        {
                            scene_register.AddComponent<ECS::CCamera>(entity);
                        }
                        else if (i == 2)
                        {
                            scene_register.AddComponent<ECS::StaticMeshComponent>(entity);
                        }
                        else if (i == 3)
                        {
                            scene_register.AddComponent<ECS::CLightProbe>(entity);
                        }
                        else if (i == 4)
                        {
                            scene_register.AddComponent<ECS::CRigidBody>(entity);
                        }
                        else if (i == 5)
                        {
                            auto &c = scene_register.AddComponent<ECS::CCollider>(entity);
                            c._type = ECS::EColliderType::kBox;
                        }
                        else if (i == 6)
                            scene_register.AddComponent<ECS::CSkeletonMesh>(entity);
                        else if (i == 7)
                            scene_register.AddComponent<ECS::CVXGI>(entity);
                        else {};
                    }
                }
                ImGui::EndCombo();
            }
            u32 comp_index = 0;

            if (scene_register.HasComponent<ECS::TransformComponent>(entity))
            {
                auto comp = scene_register.GetComponent<ECS::TransformComponent>(entity);
                DrawComponent<ECS::TransformComponent>("TransformComponent", comp, entity, [this](ECS::TransformComponent *comp)
                                                  {
                                                              auto& pos = comp->_transform._position;
                                                              Vector3f eulur = Quaternion::EulerAngles(comp->_transform._rotation);
                                                              auto& scale = comp->_transform._scale;
                                                              f32 column_widget = Size().x * 0.2f;
                                                              DrawVec3Control("Position", pos, 0.f, column_widget);
                                                              DrawVec3Control("Rotation", eulur, 0.f, column_widget);
                                                              DrawVec3Control("Scale", scale, 1.f, column_widget);
                                                              comp->_transform._rotation = Quaternion::EulerAngles(eulur); });
            }
            if (scene_register.HasComponent<ECS::LightComponent>(entity))
            {
                auto comp = scene_register.GetComponent<ECS::LightComponent>(entity);
                DrawComponent<ECS::LightComponent>("LightComponent", comp, entity, [this](ECS::LightComponent *comp)
                                              {
                                                          static const char *items[] = {"Directional", "Point", "Spot","Area"};
                                                          int light_type_idx = (i32)comp->_type;
                                                          if (ImGui::BeginCombo("LightType", items[light_type_idx], 0))
                                                          {
                                                              for (int n = 0; n < IM_ARRAYSIZE(items); n++)
                                                              {
                                                                  const bool is_selected = (light_type_idx == n);
                                                                  if (ImGui::Selectable(items[n], is_selected))
                                                                  {
                                                                      comp->_type = (ECS::ELightType::ELightType)n;
                                                                  }
                                                                  if (is_selected)
                                                                      ImGui::SetItemDefaultFocus();
                                                              }
                                                              ImGui::EndCombo();
                                                          }
                                                          ImGui::SliderFloat("Intensity", &comp->_light._light_color.a, 0.0f, 100.0f);
                                                          ImGui::ColorEdit3("Color", static_cast<float *>(comp->_light._light_color.data));
                                                          if (comp->_type == ECS::ELightType::kPoint)
                                                          {
                                                              auto &light_data = comp->_light;
                                                              ImGui::DragFloat("Range", &light_data._light_param.x, 1.0, 0.0, 500.0f);
                                                          }
                                                          else if (comp->_type == ECS::ELightType::kSpot)
                                                          {
                                                              auto &light_data = comp->_light;
                                                              ImGui::DragFloat("Range", &light_data._light_param.x);
                                                              ImGui::SliderFloat("InnerAngle", &light_data._light_param.y, 0.0f, light_data._light_param.z);
                                                              ImGui::SliderFloat("OuterAngle", &light_data._light_param.z, 1.0f, 180.0f);
                                                              Clamp(comp->_light._light_param.z, 0.0f, 180.0f);
                                                              Clamp(comp->_light._light_param.y, 0.0f, comp->_light._light_param.z - 0.1f);
                                                          }
                                                          else if (comp->_type == ECS::ELightType::kArea)
                                                          {
                                                              static const char *shapes[] = {"Rectangle", "Disc"};
                                                              auto &light_data = comp->_light;
                                                              if (ImGui::BeginCombo("Shape", shapes[(u32) light_data._light_param.w], 0))
                                                              {
                                                                  for (int n = 0; n < IM_ARRAYSIZE(shapes); n++)
                                                                  {
                                                                      const bool is_selected = ((int) light_data._light_param.w == n);
                                                                      if (ImGui::Selectable(shapes[n], is_selected))
                                                                          light_data._light_param.w = (f32) n;
                                                                      if (is_selected)
                                                                          ImGui::SetItemDefaultFocus();
                                                                  }
                                                                  ImGui::EndCombo();
                                                              }
                                                              ImGui::DragFloat("Range", &light_data._light_param.x, 1.0, 0.0, 500.0f);
                                                              ImGui::Checkbox("TwoSide", &light_data._is_two_side);
                                                              if (light_data._light_param.w == 0)
                                                              {
                                                                  f32 w = light_data._light_param.y, h = light_data._light_param.z;
                                                                  ImGui::DragFloat("Width", &w, 1.0, 0.0, 500.0f);
                                                                  ImGui::DragFloat("Height", &h, 1.0, 0.0, 500.0f);
                                                                  light_data._light_param.y = w;
                                                                  light_data._light_param.z = h;
                                                              }
                                                              else
                                                              {
                                                                  ImGui::DragFloat("Radius", &light_data._light_param.y, 1.0, 0.0, 500.0f);
                                                              }
                                                          }
                                                          else {};
                                                          bool b_cast_shadow = comp->_shadow._is_cast_shadow;
                                                          ImGui::Checkbox("CastShadow", &b_cast_shadow);
                                                          if (b_cast_shadow != comp->_shadow._is_cast_shadow)
                                                              comp->_shadow._is_cast_shadow = b_cast_shadow;
                                                          if (b_cast_shadow)
                                                          {
                                                              auto &shadow_data = comp->_shadow;
                                                              ImGui::SliderFloat("ConstantBias", &shadow_data._constant_bias, 0.0f, 2.0f);
                                                              ImGui::SliderFloat("SlopeBias", &shadow_data._slope_bias, 0.0f, 2.0f);
                                                          } });
            }
            if (scene_register.HasComponent<ECS::StaticMeshComponent>(entity))
            {
                auto comp = scene_register.GetComponent<ECS::StaticMeshComponent>(entity);
                DrawComponent<ECS::StaticMeshComponent>("StaticMeshComponent", comp, entity, [this](ECS::StaticMeshComponent *comp)
                                                   { 
                                                    MeshCompDrawer(comp);
                                                });
            }
            if (scene_register.HasComponent<ECS::CSkeletonMesh>(entity))
            {
                auto comp = scene_register.GetComponent<ECS::CSkeletonMesh>(entity);
                DrawComponent<ECS::CSkeletonMesh>("SkeletonMeshComponent", comp, entity, [this](ECS::CSkeletonMesh *comp)
                                             { MeshCompDrawer(comp); });
            }
            if (scene_register.HasComponent<ECS::CCamera>(entity))
            {
                auto comp = scene_register.GetComponent<ECS::CCamera>(entity);
                DrawComponent<ECS::CCamera>("CameraComponent", comp, entity, [&](ECS::CCamera *comp)
                                       {
                                                       auto &cam = comp->_camera;
                                                       cam._is_enable = cur_actor_active;
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
                                                       //抗锯齿
                                                       {
                                                           Enum* aa_enum = Enum::GetEnumByName("EAntiAliasing");
                                                           u32 new_value = DrawEnum(aa_enum,(u32)cam._anti_aliasing);
                                                           cam._anti_aliasing = (EAntiAliasing)new_value;
                                                           if (cam._anti_aliasing == EAntiAliasing::kTAA)
                                                           {
                                                               ImGui::SliderFloat("JitterScale", &cam._jitter_scale, 0.0f, 1.0f);
                                                           }
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
            if (scene_register.HasComponent<ECS::CLightProbe>(entity))
            {
                auto comp = scene_register.GetComponent<ECS::CLightProbe>(entity);
                DrawComponent<ECS::CLightProbe>("LightProbeComponent", comp, entity, [this](ECS::CLightProbe *comp)
                                           {
                                                           if(ImGui::Button("Bake"))
                                                           {
                                                               comp->_is_dirty = true;
                                                       };
                                                           ImGui::Checkbox("UpdateEveryTick",&comp->_is_update_every_tick);
                                                           ImGui::SliderFloat("Size",&comp->_size,0.01f,100.0f,"%.2f");
                                                           ImGui::SliderInt("ImgType", &comp->_src_type, 0, 4, "%d");
                                                           ImGui::SliderFloat("Mipmap",&comp->_mipmap,0.0f,10.0f,"%.0f"); });
            }
            if (scene_register.HasComponent<ECS::CRigidBody>(entity))
            {
                auto comp = scene_register.GetComponent<ECS::CRigidBody>(entity);
                DrawComponent<ECS::CRigidBody>("RigidComponent", comp, entity, [this](ECS::CRigidBody *comp)
                                          { 
                                                ImGui::SliderFloat("Mass", &comp->_mass, 0.0, 1000.0, "%.2f");
                                                ImGui::Text("State:");
                                                ImGui::Indent(10.f);
                                                ImGui::SameLine();
                                                ImGui::Text("Velocity %s", comp->_velocity.ToString().c_str());
                                                ImGui::Text("AngularVelocity %s", comp->_angular_velocity.ToString().c_str()); });
            };
            if (scene_register.HasComponent<ECS::CCollider>(entity))
            {
                auto comp = scene_register.GetComponent<ECS::CCollider>(entity);
                DrawComponent<ECS::CCollider>("ColiderComponent", comp, entity, [this](ECS::CCollider *comp)
                                         { 
                                                static const char* items[3] = 
                                                {
                                                    ECS::EColliderType::_Strings[0].c_str(),
                                                    ECS::EColliderType::_Strings[1].c_str(),
                                                    ECS::EColliderType::_Strings[2].c_str()
                                                };
                                                u16 type_index = comp->_type;
                                                auto s = ECS::EColliderType::ToString(comp->_type);
                                                comp->_type = (ECS::EColliderType::EColliderType) DrawEnumProperty("ColiderType", ECS::EColliderType::_Strings, ECS::EColliderType::COUNT, type_index);
                                                //if (ImGui::BeginCombo("ColiderType", items[type_index], 0))
                                                //{
                                                //    for (int n = 0; n < IM_ARRAYSIZE(items); n++)
                                                //    {
                                                //        const bool is_selected = (type_index == n);
                                                //        if (ImGui::Selectable(items[n], is_selected))
                                                //            comp->_type = (EColliderType::EColliderType) n;
                                                //        if (is_selected)
                                                //            ImGui::SetItemDefaultFocus();
                                                //    }
                                                //    ImGui::EndCombo();
                                                //}
                                                ImGui::DragFloat3("Center",comp->_center.data);
                                                ImGui::Checkbox("Is Trigger", &comp->_is_trigger);
                                                if (comp->_type == ECS::EColliderType::kBox)
                                                {
                                                    ImGui::DragFloat3("Size", comp->_param.data,1.0f,0.0f,100.0f);
                                                }
                                                else if (comp->_type == ECS::EColliderType::kSphere)
                                                {
                                                    ImGui::DragFloat("Radius", &comp->_param.x, 1.0f, 0.0f, 100.0f);
                                                }
                                                else if (comp->_type == ECS::EColliderType::kCapsule)
                                                {
                                                    ImGui::DragFloat("Radius", &comp->_param.x, 1.0f, 0.0f, 100.0f);
                                                    ImGui::DragFloat("Height", &comp->_param.y, 1.0f, 0.0f, 100.0f);
                                                    static const char* axis_items[3] ={"X-Axis","Y-Axis","Z-Axis"};
                                                    int axis_index = (int)comp->_param.z;
                                                    if (ImGui::BeginCombo("Axis", axis_items[axis_index], 0))
                                                    {
                                                        for (int n = 0; n < IM_ARRAYSIZE(axis_items); n++)
                                                        {
                                                            const bool is_selected = (axis_index == n);
                                                            if (ImGui::Selectable(axis_items[n], is_selected))
                                                                comp->_param.z = (f32) n;
                                                            if (is_selected)
                                                                ImGui::SetItemDefaultFocus();
                                                        }
                                                        ImGui::EndCombo();
                                                    }
                                                } });
            };
            if (scene_register.HasComponent<ECS::CVXGI>(entity))
            {
                auto comp = scene_register.GetComponent<ECS::CVXGI>(entity);
                DrawComponent<ECS::CVXGI>("VXGIComponent", comp, entity, [this](ECS::CVXGI *comp)
                                     {
                                                ImGui::SliderFloat3("GridSize", comp->_grid_size.data, 1.f, 100.f);
                                                ImGui::SliderInt3("GridNum", comp->_grid_num.data, 1, 128);
                                                ImGui::SliderFloat("Distance",&comp->_distance, 1.f, 100.f);
                                                ImGui::Checkbox("DrawVoxel", &comp->_is_draw_voxel);
                                                u16 max_mip = Texture3D::MaxMipmapCount((u16) (comp->_grid_num.x), (u16) (comp->_grid_num.y), (u16) (comp->_grid_num.z));
                                                i32 debug_mip = comp->_mipmap;
                                                ImGui::SliderInt("DebugMip", &debug_mip, 0, max_mip-1);
                                                comp->_mipmap = (u16) debug_mip;
                                                ImGui::Checkbox("DrawDebugLine",&comp->_is_draw_grid);
                                                ImGui::SliderFloat("MaxDist",&comp->_max_distance,0.f,1.732f);
                                                ImGui::SliderFloat("MinDist",&comp->_min_distance,0.f,1.f);
                                                ImGui::SliderFloat("ConeAngle",&comp->_diffuse_cone_angle,0.f,90.f);
                                                if (ImGui::Button("GI Only"))
                                                {
                                                    Shader::EnableGlobalKeyword("DEBUG_GI");
                                                }
                                                ImGui::SameLine();
                                                if (ImGui::Button("GI + Local"))
                                                {
                                                    Shader::DisableGlobalKeyword("DEBUG_GI");
                                                } });
            }
            _p_texture_selector->Show();
        }

        void ObjectDetail::ShowShaderPannel(Shader *s)
        {
            f32 indent = 10.0f;
            ImGui::Text("Name: %s", s->Name().c_str());
            for (i16 i = 0; i < s->PassCount(); i++)
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
