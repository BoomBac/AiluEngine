#include "Widgets/EditorLayer.h"
#include "Common/Selection.h"
#include "Ext/imgui/imgui.h"
#include "Ext/imgui/imgui_internal.h"

#include "Ext/ImGuizmo/ImGuizmo.h"//必须在imgui之后引入
#include "Ext/imnodes/imnodes.h"
#include "Ext/implot/implot.h"

#include "Framework/Common/EngineConfig.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/TimeMgr.h"
#include "Render/Features/PostprocessPass.h"
#include "Render/Features/VolumetricClouds.h"
#include "Render/Features/VolumetricFog.h"
#include "Render/Gizmo.h"
#include "Render/GraphicsContext.h"
#include "Render/RenderingData.h"
#include "UI/TextRenderer.h"
//#include <Framework/Common/Application.h>
#include <Objects/Type.h>

#include "Inc/EditorApp.h"

#include "Framework/Common/Input.h"
#include "Framework/Events/MouseEvent.h"

#include "Framework/Common/Profiler.h"
#include "Framework/Common/MemoryDebugService.h"
#include "Render/RenderPipeline.h"
#include "Scene/Scene.h"

#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"
#include "UI/UILayer.h"
#include "UI/UIRenderer.h"
#include "UI/Widget.h"

#include "Objects/JsonArchive.h"

#include "Animation/Curve.hpp"
#include "Animation/Solver.h"
#include "Common/Undo.h"
#include "Framework/Common/EngineConfig.h"
#include "Framework/Events/KeyEvent.h"
#include "Render/CommonRenderPipeline.h"
#include "Render/RenderGraph/RenderGraph.h"

#include "Widgets/CommonView.h"
#include "Widgets/AllocatorDebugPanel.h"
#include "Widgets/RenderView.h"
#include "Widgets/ResourceBrowser.h"
#include "Widgets/StyleThemeEditor.h"

#include "Framework/Parser/TextParser.h"
#include "Platform/Process.h"
//todo remove
#include "Render/RenderingStates.h"

#include "Audio/Audio.h"
#include "Audio/AudioClip.h"


namespace Ailu
{
    using namespace Render;
    namespace Editor
    {
        namespace
        {
            const char *ThreadStatusToString(Core::EThreadStatus status)
            {
                switch (status)
                {
                    case Core::EThreadStatus::kNotStarted:
                        return "kNotStarted";
                    case Core::EThreadStatus::kRunning:
                        return "kRunning";
                    case Core::EThreadStatus::kIdle:
                        return "kIdle";
                    default:
                        return "kNotStarted";
                }
            }

            const wchar_t *GetPackagePlayerPresetName()
            {
#ifdef _DEBUG
                return L"build-debug";
#else
                return L"build-release";
#endif
            }

            WString BuildPackagePlayerCommandLine()
            {
                WString command = L"cmd.exe /c \"cmake --build --preset ";
                command += GetPackagePlayerPresetName();
                command += L" --target package_player --parallel 6\"";
                return command;
            }

            bool IsElementInSubtree(const UI::UIElement *root, const UI::UIElement *target)
            {
                if (root == nullptr || target == nullptr)
                    return false;
                if (root == target)
                    return true;
                for (const auto &child: root->GetChildren())
                {
                    if (IsElementInSubtree(child.get(), target))
                        return true;
                }
                return false;
            }

            UI::Widget *FindOwningWidget(const UI::UIElement *element)
            {
                if (element == nullptr)
                    return nullptr;
                auto *ui_mgr = UI::UIManager::Get();
                if (ui_mgr == nullptr)
                    return nullptr;
                for (const auto &widget: ui_mgr->_widgets)
                {
                    if (widget != nullptr && IsElementInSubtree(widget->Root(), element))
                        return widget.get();
                }
                return nullptr;
            }

            String BuildUIReflectorElementLabel(UI::UIElement *element)
            {
                const auto *type = element->GetType();
                const String &type_name = type != nullptr ? type->Name() : String("Unknown");
                const String &name = element->Name().empty() ? type_name : element->Name();
                return std::format("{} <{}>", name, type_name);
            }

            String UIInvalidationReasonToString(UI::EUIInvalidationReason reasons)
            {
                if (reasons == UI::EUIInvalidationReason::kNone)
                    return "None";

                String result;
                auto append_reason = [&](UI::EUIInvalidationReason flag, const char *name)
                {
                    if (!UI::HasInvalidation(reasons, flag))
                        return;
                    if (!result.empty())
                        result += " | ";
                    result += name;
                };
                append_reason(UI::EUIInvalidationReason::kPaint, "Paint");
                append_reason(UI::EUIInvalidationReason::kLayout, "Layout");
                append_reason(UI::EUIInvalidationReason::kTransform, "Transform");
                append_reason(UI::EUIInvalidationReason::kHierarchy, "Hierarchy");
                append_reason(UI::EUIInvalidationReason::kClip, "Clip");
                append_reason(UI::EUIInvalidationReason::kVisibility, "Visibility");
                append_reason(UI::EUIInvalidationReason::kTextLayout, "TextLayout");
                return result.empty() ? String("Unknown") : result;
            }

            bool HasDirtyDescendant(UI::UIElement *element)
            {
                if (element == nullptr)
                    return false;
                for (const auto &child: element->GetChildren())
                {
                    if (child != nullptr && (child->IsDebugPaintDirty() || HasDirtyDescendant(child.get())))
                        return true;
                }
                return false;
            }

            bool HasDirtySubtree(UI::UIElement *element)
            {
                return element != nullptr && (element->IsDebugPaintDirty() || HasDirtyDescendant(element));
            }

            void DrawDirtyDescendantReasons(UI::UIElement *element)
            {
                if (element == nullptr)
                    return;
                for (const auto &child: element->GetChildren())
                {
                    if (child == nullptr)
                        continue;
                    const bool is_child_dirty = child->IsDebugPaintDirty();
                    const bool has_dirty_subtree = HasDirtySubtree(child.get());
                    if (!has_dirty_subtree)
                        continue;
                    const String label = BuildUIReflectorElementLabel(child.get());
                    if (is_child_dirty)
                    {
                        const String reasons = UIInvalidationReasonToString(child->GetDebugInvalidationReasons());
                        ImGui::BulletText("%s: %s", label.c_str(), reasons.c_str());
                    }
                    DrawDirtyDescendantReasons(child.get());
                }
            }

            void DrawUIReflectorElementTree(UI::UIElement *element, UI::UIElement *&selected)
            {
                if (element == nullptr)
                    return;
                const auto &children = element->GetChildren();
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
                if (children.empty())
                    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (element == selected)
                    flags |= ImGuiTreeNodeFlags_Selected;
                if (element->GetHierarchyDepth() <= 1u)
                    flags |= ImGuiTreeNodeFlags_DefaultOpen;
                const bool is_self_dirty = element->IsDebugPaintDirty();
                const bool is_chain_dirty = !is_self_dirty && HasDirtyDescendant(element);
                String label = BuildUIReflectorElementLabel(element);
                if (is_self_dirty)
                    label += " [DIRTY]";
                else if (is_chain_dirty)
                    label += " [CHILD DIRTY]";
                const bool is_open = ImGui::TreeNodeEx(static_cast<void *>(element), flags, "%s", label.c_str());
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                    selected = element;
                if (!children.empty() && is_open)
                {
                    for (const auto &child: children)
                        DrawUIReflectorElementTree(child.get(), selected);
                    ImGui::TreePop();
                }
            }

            void DrawUIReflectorWidgetTree(UI::Widget *widget, UI::UIElement *&selected)
            {
                if (widget == nullptr)
                    return;
                UI::UIElement *root = widget->Root();
                const Vector2f size = widget->GetSize();
                const String label = std::format("{} [sort:{}{}] ({:.0f}x{:.0f})",
                                                 widget->Name(),
                                                 widget->_sort_order,
                                                 widget->_is_external_output ? ", external" : "",
                                                 size.x,
                                                 size.y);
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
                if (root == nullptr)
                    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                const bool is_open = ImGui::TreeNodeEx(static_cast<void *>(widget), flags, "%s", label.c_str());
                if (root != nullptr && is_open)
                {
                    DrawUIReflectorElementTree(root, selected);
                    ImGui::TreePop();
                }
            }

            void DrawUIReflectorRenderStats()
            {
                auto *renderer = UI::UIRenderer::Get();
                if (renderer == nullptr)
                    return;

                const auto &stats = renderer->GetFrameStats();
                if (!ImGui::CollapsingHeader("Render Stats", ImGuiTreeNodeFlags_DefaultOpen))
                    return;

                ImGui::Text("Element Visit: %llu", static_cast<unsigned long long>(stats._ui_element_visit_count));
                ImGui::Text("RenderImpl: %llu", static_cast<unsigned long long>(stats._ui_render_impl_count));
                ImGui::Text("Layout: %llu", static_cast<unsigned long long>(stats._ui_layout_count));
                ImGui::Text("Text Layout: %llu", static_cast<unsigned long long>(stats._ui_text_layout_count));
                ImGui::Separator();
                ImGui::Text("Generated Vertices: %llu", static_cast<unsigned long long>(stats._ui_generated_vertex_count));
                ImGui::Text("Generated Indices: %llu", static_cast<unsigned long long>(stats._ui_generated_index_count));
                ImGui::Text("Uploaded: %llu B (%.2f KB)", static_cast<unsigned long long>(stats._ui_uploaded_bytes), stats._ui_uploaded_bytes / 1024.0f);
                ImGui::Text("Draw Nodes: %llu", static_cast<unsigned long long>(stats._ui_draw_node_count));
                ImGui::Text("Draw Calls: %llu", static_cast<unsigned long long>(stats._ui_draw_call_count));
                ImGui::Separator();
                ImGui::Text("Cache Hit: %llu", static_cast<unsigned long long>(stats._ui_cache_hit_count));
                ImGui::Text("Cache Miss: %llu", static_cast<unsigned long long>(stats._ui_cache_miss_count));
                ImGui::Text("Paint Build: %.3f ms", stats._ui_paint_build_time);
                ImGui::Text("GPU Upload: %.3f ms", stats._ui_gpu_upload_time);
                ImGui::Text("Submit: %.3f ms", stats._ui_submit_time);
                ImGui::Separator();
            }

            bool DrawReflectedPropertiesByType(const Type *type, void *obj, const std::function<bool(const PropertyInfo &)> &filter = {});

            bool static DrawMemberProperty(const PropertyInfo &prop_info, void *obj)
            {
                bool changed = false;
                ImGui::PushID(obj);
                auto &meta_info = prop_info.MetaInfo();
                if (!prop_info.IsConst())
                {
                    if (prop_info.GetType() == StaticClass<bool>())
                    {
                        bool old_value = prop_info.Get<bool>(obj);
                        bool new_value = old_value;
                        if (ImGui::Checkbox(prop_info.Name().c_str(), &new_value))
                        {
                            prop_info.Set<bool>(obj, new_value);
                            changed = true;
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<f32>())
                    {
                        f32 old_value = prop_info.Get<f32>(obj);
                        f32 new_value = old_value;
                        if (meta_info.GetBool("IsRange"))
                        {
                            ImGui::SliderFloat(prop_info.Name().c_str(), &new_value, meta_info.GetFloat("RangeMin"), meta_info.GetFloat("RangeMax"));
                        }
                        else
                            ImGui::InputFloat(prop_info.Name().c_str(), &new_value);
                        if (old_value != new_value)
                        {
                            prop_info.Set<f32>(obj, new_value);
                            changed = true;
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<i8>())
                    {
                        i32 old_value = prop_info.Get<i8>(obj);
                        i32 new_value = old_value;
                        if (meta_info.GetBool("IsRange"))
                        {
                            ImGui::SliderInt(prop_info.Name().c_str(), &new_value, (i32) meta_info.GetInt("RangeMin"), (i32) meta_info.GetInt("RangeMax"));
                        }
                        else
                            ImGui::InputInt(prop_info.Name().c_str(), &new_value);
                        if (old_value != new_value)
                        {
                            prop_info.Set<i8>(obj, (i8) new_value);
                            changed = true;
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<i16>())
                    {
                        i32 old_value = prop_info.Get<i16>(obj);
                        i32 new_value = old_value;
                        if (meta_info.GetBool("IsRange"))
                        {
                            ImGui::SliderInt(prop_info.Name().c_str(), &new_value, (i32) meta_info.GetInt("RangeMin"), (i32) meta_info.GetInt("RangeMax"));
                        }
                        else
                            ImGui::InputInt(prop_info.Name().c_str(), &new_value);
                        if (old_value != new_value)
                        {
                            prop_info.Set<i16>(obj, (i16) new_value);
                            changed = true;
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<i32>())
                    {
                        i32 old_value = prop_info.Get<i32>(obj);
                        i32 new_value = old_value;
                        if (meta_info.GetBool("IsRange"))
                        {
                            ImGui::SliderInt(prop_info.Name().c_str(), &new_value, (i32) meta_info.GetInt("RangeMin"), (i32) meta_info.GetInt("RangeMax"));
                        }
                        else
                            ImGui::InputInt(prop_info.Name().c_str(), &new_value);
                        if (old_value != new_value)
                        {
                            prop_info.Set<i32>(obj, new_value);
                            changed = true;
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<u8>())
                    {
                        i32 old_value = prop_info.Get<u8>(obj);
                        i32 new_value = old_value;
                        if (meta_info.GetBool("IsRange"))
                        {
                            ImGui::SliderInt(prop_info.Name().c_str(), &new_value, meta_info.GetInt("RangeMin") < 0 ? 0 : meta_info.GetInt("RangeMin"), meta_info.GetInt("RangeMax"));
                        }
                        else
                            ImGui::InputInt(prop_info.Name().c_str(), &new_value);
                        if (old_value != new_value)
                        {
                            prop_info.Set<u8>(obj, (u8) new_value);
                            changed = true;
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<u16>())
                    {
                        i32 old_value = prop_info.Get<u16>(obj);
                        i32 new_value = old_value;
                        if (meta_info.GetBool("IsRange"))
                        {
                            ImGui::SliderInt(prop_info.Name().c_str(), &new_value, meta_info.GetInt("RangeMin") < 0 ? 0 : meta_info.GetInt("RangeMin"), meta_info.GetInt("RangeMax"));
                        }
                        else
                            ImGui::InputInt(prop_info.Name().c_str(), &new_value);
                        if (old_value != new_value)
                        {
                            prop_info.Set<u16>(obj, (u16) new_value);
                            changed = true;
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<u32>())
                    {
                        i32 old_value = prop_info.Get<u32>(obj);
                        i32 new_value = old_value;
                        if (meta_info.GetBool("IsRange"))
                        {
                            ImGui::SliderInt(prop_info.Name().c_str(), &new_value, meta_info.GetInt("RangeMin") < 0 ? 0 : meta_info.GetInt("RangeMin"), meta_info.GetInt("RangeMax"));
                        }
                        else
                            ImGui::InputInt(prop_info.Name().c_str(), &new_value);
                        if (old_value != new_value)
                        {
                            prop_info.Set<u32>(obj, (u32) new_value);
                            changed = true;
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<u64>())
                    {
                        u64 old_value = prop_info.Get<u64>(obj);
                        u64 new_value = old_value;
                        if (ImGui::InputScalar(prop_info.Name().c_str(), ImGuiDataType_U64, &new_value))
                        {
                            prop_info.Set<u64>(obj, new_value);
                            changed = true;
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<String>())
                    {
                        String old_value = prop_info.Get<String>(obj);
                        auto str_len = old_value.size();
                        char buf[256];
                        memcpy(buf, old_value.c_str(), str_len);
                        buf[str_len] = '\0';
                        if (ImGui::InputText(prop_info.Name().c_str(), buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            prop_info.Set<String>(obj, buf);
                            changed = true;
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<Vector2f>())
                    {
                        Vector2f old_value = prop_info.Get<Vector2f>(obj);
                        Vector2f new_value = old_value;
                        if (ImGui::InputFloat2(prop_info.Name().c_str(), new_value.data))
                        {
                            prop_info.Set<Vector2f>(obj, new_value);
                            changed = true;
                        }
                    }
                    else if (prop_info.GetType() != nullptr && prop_info.GetType()->IsEnum())
                    {
                        const auto *enum_type = static_cast<const Enum *>(prop_info.GetType());
                        i32 current_value = static_cast<i32>(prop_info.Get<u32>(obj));
                        const String &preview = enum_type->GetNameByIndex(static_cast<u32>(current_value));
                        if (ImGui::BeginCombo(prop_info.Name().c_str(), preview.c_str()))
                        {
                            for (const String *name: enum_type->GetEnumNames())
                            {
                                if (name == nullptr)
                                    continue;
                                const i32 enum_value = enum_type->GetIndexByName(*name);
                                const bool is_selected = (enum_value == current_value);
                                if (ImGui::Selectable(name->c_str(), is_selected))
                                {
                                    prop_info.Set<u32>(obj, static_cast<u32>(enum_value));
                                    changed = true;
                                    current_value = enum_value;
                                }
                                if (is_selected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<UI::Padding>())
                    {
                        UI::Padding old_value = prop_info.Get<UI::Padding>(obj);
                        Vector4f new_value = {old_value._l, old_value._t, old_value._r, old_value._b};
                        if (ImGui::InputFloat4(prop_info.Name().c_str(), new_value.data))
                        {
                            prop_info.Set<UI::Padding>(obj, UI::Padding(new_value));
                            changed = true;
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<Vector3f>())
                    {
                        Vector3f old_value = prop_info.Get<Vector3f>(obj);
                        Vector3f new_value = old_value;
                        if (ImGui::InputFloat3(prop_info.Name().c_str(), new_value.data))
                        {
                            prop_info.Set<Vector3f>(obj, new_value);
                            changed = true;
                        }
                    }
                    else if (prop_info.TypeName() == "Color")
                    {
                        Color old_value = prop_info.Get<Color>(obj);
                        Color new_value = old_value;
                        if (ImGui::ColorEdit4(prop_info.Name().c_str(), new_value.data, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                        {
                            prop_info.Set<Color>(obj, new_value);
                            changed = true;
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<Vector4f>())
                    {
                        Vector4f old_value = prop_info.Get<Vector4f>(obj);
                        Vector4f new_value = old_value;
                        if (meta_info.GetBool("IsColor"))
                        {
                            if (ImGui::ColorEdit4(prop_info.Name().c_str(), new_value.data, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                            {
                                prop_info.Set<Vector4f>(obj, new_value);
                                changed = true;
                            }
                        }
                        else
                        {
                            if (ImGui::InputFloat4(prop_info.Name().c_str(), new_value.data))
                            {
                                prop_info.Set<Vector4f>(obj, new_value);
                                changed = true;
                            }
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<Vector2Int>())
                    {
                        Vector2Int old_value = prop_info.Get<Vector2Int>(obj);
                        Vector2Int new_value = old_value;
                        if (ImGui::InputInt2(prop_info.Name().c_str(), new_value.data))
                        {
                            prop_info.Set<Vector2Int>(obj, new_value);
                            changed = true;
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<Vector3Int>())
                    {
                        Vector3Int old_value = prop_info.Get<Vector3Int>(obj);
                        Vector3Int new_value = old_value;
                        if (ImGui::InputInt3(prop_info.Name().c_str(), new_value.data))
                        {
                            prop_info.Set<Vector3Int>(obj, new_value);
                            changed = true;
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<Vector4Int>())
                    {
                        Vector4Int old_value = prop_info.Get<Vector4Int>(obj);
                        Vector4Int new_value = old_value;
                        if (ImGui::InputInt4(prop_info.Name().c_str(), new_value.data))
                        {
                            prop_info.Set<Vector4Int>(obj, new_value);
                            changed = true;
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<UI::UIControlVisual>())
                    {
                        if (ImGui::TreeNode(prop_info.Name().c_str()))
                        {
                            auto &visual = prop_info.Get<UI::UIControlVisual>(obj);
                            changed = DrawReflectedPropertiesByType(visual.GetType(), &visual);
                            ImGui::TreePop();
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<UI::UIBrush>())
                    {
                        if (ImGui::TreeNode(prop_info.Name().c_str()))
                        {
                            auto &brush = prop_info.Get<UI::UIBrush>(obj);
                            changed = DrawReflectedPropertiesByType(brush.GetType(), &brush);
                            ImGui::TreePop();
                        }
                    }
                    else if (prop_info.GetType() == StaticClass<UI::UIScrollBarStyle>())
                    {
                        if (ImGui::TreeNode(prop_info.Name().c_str()))
                        {
                            auto &bar_style = prop_info.Get<UI::UIScrollBarStyle>(obj);
                            changed = DrawReflectedPropertiesByType(bar_style.GetType(), &bar_style);
                            ImGui::TreePop();
                        }
                    }
                    else
                    {
                        ImGui::Text("Unsupported Type: %s", prop_info.TypeName().c_str());
                    }
                }
                else
                    ImGui::Text("ConstValue %s", prop_info.Name().c_str());
                ImGui::PopID();
                return changed;
            }

            bool DrawReflectedPropertiesByType(const Type *type, void *obj, const std::function<bool(const PropertyInfo &)> &filter)
            {
                bool any_changed = false;
                for (const Type *cur_type = type; cur_type != nullptr; cur_type = cur_type->BaseType())
                {
                    auto &properties = cur_type->GetProperties();
                    if (properties.empty())
                        continue;
                    const bool open = ImGui::CollapsingHeader(cur_type->Name().c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                    if (!open)
                        continue;
                    for (auto &prop: properties)
                    {
                        if (filter && !filter(prop))
                            continue;
                        any_changed |= DrawMemberProperty(prop, obj);
                    }
                }
                return any_changed;
            }

            template<typename TObject>
            bool DrawReflectedPropertiesByType(const Type *type, TObject &obj, const std::function<bool(const PropertyInfo &)> &filter = {})
            {
                return DrawReflectedPropertiesByType(type, static_cast<void *>(&obj), filter);
            }

            void DrawUIReflectorStyleEditor(UI::UIElement *selected)
            {
                if (selected == nullptr)
                    return;

                bool style_changed = false;

                ImGui::Separator();
                ImGui::Spacing();

                // ── Button ──────────────────────────────────────────
                if (auto *btn = dynamic_cast<UI::Button *>(selected))
                {
                    if (ImGui::CollapsingHeader("Style (Button)", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        UI::UIStyleId style_id = btn->GetStyleId();
                        char buf[256];
                        auto len = std::min(style_id.size(), (size_t)255);
                        memcpy(buf, style_id.c_str(), len);
                        buf[len] = '\0';
                        if (ImGui::InputText("Style ID", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            btn->SetStyleId(String(buf));
                            style_changed = true;
                        }
                        auto &ov = btn->GetStyleOverride();
                        if (DrawReflectedPropertiesByType(ov.GetType(), ov))
                        {
                            ov._override_mask = ~0u;
                            style_changed = true;
                        }
                    }
                }
                // ── Slider ──────────────────────────────────────────
                else if (auto *s = dynamic_cast<UI::Slider *>(selected))
                {
                    if (ImGui::CollapsingHeader("Style (Slider)", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        UI::UIStyleId style_id = s->GetStyleId();
                        char buf[256];
                        auto len = std::min(style_id.size(), (size_t)255);
                        memcpy(buf, style_id.c_str(), len);
                        buf[len] = '\0';
                        if (ImGui::InputText("Style ID", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            s->SetStyleId(String(buf));
                            style_changed = true;
                        }
                        auto &ov = s->GetStyleOverride();
                        if (DrawReflectedPropertiesByType(ov.GetType(), ov))
                        {
                            ov._override_mask = ~0u;
                            style_changed = true;
                        }
                    }
                }
                // ── CheckBox ────────────────────────────────────────
                else if (auto *cb = dynamic_cast<UI::CheckBox *>(selected))
                {
                    if (ImGui::CollapsingHeader("Style (CheckBox)", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        UI::UIStyleId style_id = cb->GetStyleId();
                        char buf[256];
                        auto len = std::min(style_id.size(), (size_t)255);
                        memcpy(buf, style_id.c_str(), len);
                        buf[len] = '\0';
                        if (ImGui::InputText("Style ID", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            cb->SetStyleId(String(buf));
                            style_changed = true;
                        }
                        auto &ov = cb->GetStyleOverride();
                        if (DrawReflectedPropertiesByType(ov.GetType(), ov))
                        {
                            ov._override_mask = ~0u;
                            style_changed = true;
                        }
                    }
                }
                // ── InputBlock ──────────────────────────────────────
                else if (auto *ib = dynamic_cast<UI::InputBlock *>(selected))
                {
                    if (ImGui::CollapsingHeader("Style (InputBlock)", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        UI::UIStyleId style_id = ib->GetStyleId();
                        char buf[256];
                        auto len = std::min(style_id.size(), (size_t)255);
                        memcpy(buf, style_id.c_str(), len);
                        buf[len] = '\0';
                        if (ImGui::InputText("Style ID", buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            ib->SetStyleId(String(buf));
                            style_changed = true;
                        }
                        auto &ov = ib->GetStyleOverride();
                        if (DrawReflectedPropertiesByType(ov.GetType(), ov))
                        {
                            ov._override_mask = ~0u;
                            style_changed = true;
                        }
                    }
                }
                // ── ScrollView ──────────────────────────────────────
                else if (auto *sv = dynamic_cast<UI::ScrollView *>(selected))
                {
                    if (ImGui::CollapsingHeader("Style (ScrollView)", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        auto &ov = sv->GetStyleOverride();
                        if (DrawReflectedPropertiesByType(ov.GetType(), ov))
                        {
                            ov._override_mask = ~0u;
                            style_changed = true;
                        }
                    }
                }
                // ── Text ────────────────────────────────────────────
                else if (auto *t = dynamic_cast<UI::Text *>(selected))
                {
                    if (ImGui::CollapsingHeader("Style (Text)", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        auto &ov = t->GetStyleOverride();
                        if (DrawReflectedPropertiesByType(ov.GetType(), ov))
                        {
                            ov._override_mask = ~0u;
                            style_changed = true;
                        }
                    }
                }
                // ── Border ──────────────────────────────────────────
                else if (auto *b = dynamic_cast<UI::Border *>(selected))
                {
                    if (ImGui::CollapsingHeader("Style (Border)", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        auto &ov = b->GetStyleOverride();
                        if (DrawReflectedPropertiesByType(ov.GetType(), ov))
                        {
                            ov._override_mask = ~0u;
                            style_changed = true;
                        }
                    }
                }
                // ── Image ───────────────────────────────────────────
                else if (auto *img = dynamic_cast<UI::Image *>(selected))
                {
                    if (ImGui::CollapsingHeader("Style (Image)", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        auto &ov = img->GetStyleOverride();
                        if (DrawReflectedPropertiesByType(ov.GetType(), ov))
                        {
                            ov._override_mask = ~0u;
                            style_changed = true;
                        }
                    }
                }
                // ── Canvas ──────────────────────────────────────────
                else if (auto *c = dynamic_cast<UI::Canvas *>(selected))
                {
                    if (ImGui::CollapsingHeader("Style (Canvas)", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        auto &ov = c->GetStyleOverride();
                        if (DrawReflectedPropertiesByType(ov.GetType(), ov))
                        {
                            ov._override_mask = ~0u;
                            style_changed = true;
                        }
                    }
                }
                // ── LinearBox (includes VerticalBox, HorizontalBox) ─
                else if (auto *lb = dynamic_cast<UI::LinearBox *>(selected))
                {
                    if (ImGui::CollapsingHeader("Style (LinearBox)", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        auto &ov = lb->GetStyleOverride();
                        if (DrawReflectedPropertiesByType(ov.GetType(), ov))
                        {
                            ov._override_mask = ~0u;
                            style_changed = true;
                        }
                    }
                }
                // ── SplitView ───────────────────────────────────────
                else if (auto *spv = dynamic_cast<UI::SplitView *>(selected))
                {
                    if (ImGui::CollapsingHeader("Style (SplitView)", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        auto &ov = spv->GetStyleOverride();
                        if (DrawReflectedPropertiesByType(ov.GetType(), ov))
                        {
                            ov._override_mask = ~0u;
                            style_changed = true;
                        }
                    }
                }

                if (style_changed)
                    selected->InvalidateStyle();
            }

            void DrawUIReflectorSelectionDetails(UI::UIElement *selected)
            {
                if (selected == nullptr)
                {
                    ImGui::TextUnformatted("No UI element selected.");
                    return;
                }
                const auto *type = selected->GetType();
                const Vector4f arr_rect = selected->GetArrangeRect();
                const Vector4f cnt_rect = selected->GetContentRect();
                UI::Widget *widget = FindOwningWidget(selected);
                const String widget_name = widget != nullptr ? widget->Name() : String("Unknown");
                const String parent_name = selected->GetParent() != nullptr ? selected->GetParent()->Name() : String("None");

                bool is_show = selected->IsVisible();
                ImGui::Checkbox("Show",&is_show);
                selected->SetVisible(is_show);
                ImGui::Text("Name: %s", selected->Name().c_str());
                ImGui::Text("Type: %s", type != nullptr ? type->Name().c_str() : "Unknown");
                ImGui::Text("Widget: %s", widget_name.c_str());
                ImGui::Text("Parent: %s", parent_name.c_str());
                ImGui::Separator();
                ImGui::Text("AbsRect: %.1f, %.1f, %.1f, %.1f", arr_rect.x, arr_rect.y, arr_rect.z, arr_rect.w);
                ImGui::Text("ContentRect: %.1f, %.1f, %.1f, %.1f", cnt_rect.x, cnt_rect.y, cnt_rect.z, cnt_rect.w);
                ImGui::Text("Visible: %s", selected->IsVisible() ? "true" : "false");
                ImGui::Text("Hovered: %s", selected->IsHovered() ? "true" : "false");
                ImGui::Text("Pressed: %s", selected->IsPressed() ? "true" : "false");
                ImGui::Text("Focused: %s", selected->IsFocused() ? "true" : "false");
                ImGui::Separator();

                if (ImGui::CollapsingHeader("Dirty State", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    const String element_reasons = UIInvalidationReasonToString(selected->GetDebugInvalidationReasons());
                    const bool has_dirty_descendant = HasDirtyDescendant(selected);
                    ImGui::Text("Self Dirty: %s", selected->IsDebugPaintDirty() ? "true" : "false");
                    ImGui::Text("Self Reasons: %s", element_reasons.c_str());
                    ImGui::Text("Descendant Dirty: %s", has_dirty_descendant ? "true" : "false");
                    if (widget != nullptr)
                    {
                        const String widget_reasons = UIInvalidationReasonToString(widget->GetPaintCacheDirtyReasons());
                        const u16 frame_index = Render::g_pGfxContext != nullptr
                                                    ? static_cast<u16>(Render::g_pGfxContext->GetFrameCount() % Render::RenderConstants::kFrameCount)
                                                    : 0u;
                        ImGui::Text("Widget Cache Dirty: %s", widget->IsPaintCacheDirty(frame_index) ? "true" : "false");
                        ImGui::Text("Widget Reasons: %s", widget_reasons.c_str());
                        ImGui::Text("Invalidation Count: %llu", static_cast<unsigned long long>(widget->GetInvalidationCount()));
                        if (auto *source = widget->GetLastInvalidationSource(); source != nullptr)
                        {
                            const String source_label = BuildUIReflectorElementLabel(source);
                            const String source_reasons = UIInvalidationReasonToString(widget->GetLastInvalidationReasons());
                            ImGui::Text("Last Source: %s", source_label.c_str());
                            ImGui::Text("Last Source Reasons: %s", source_reasons.c_str());
                        }
                        else
                        {
                            ImGui::TextUnformatted("Last Source: None");
                        }
                    }
                    if (has_dirty_descendant)
                    {
                        ImGui::SeparatorText("Dirty Descendants");
                        DrawDirtyDescendantReasons(selected);
                    }
                }
                ImGui::Separator();

                auto slot = selected->GetSlot();
                if (slot != nullptr)
                {
                    ImGui::Text("Slot Type: %s", slot->GetType() != nullptr ? slot->GetType()->Name().c_str() : "Unknown");
                    if (ImGui::CollapsingHeader("Slot", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        const bool slot_changed = DrawReflectedPropertiesByType(slot->GetType(), *slot);
                        if (slot_changed)
                        {
                            slot->PostPropertyChanged();
                        }
                    }
                    ImGui::Separator();
                }

                DrawReflectedPropertiesByType(selected->GetType(), *selected, [](const PropertyInfo &prop)
                                              { return prop.Name() != "_slot_obj" && prop.Name() != "_state"; });

                DrawUIReflectorStyleEditor(selected);
            }
            void ShowUIReflectorWindow(bool *is_show)
            {
                auto *ui_mgr = UI::UIManager::Get();
                if (ui_mgr == nullptr)
                    return;
                UI::UIElement *selected = ui_mgr->GetDebugHighlightTarget();
                if (selected != nullptr && FindOwningWidget(selected) == nullptr)
                    selected = nullptr;
                ui_mgr->SetDebugHighlightTarget(selected);

                const bool is_open = ImGui::Begin("UI Reflector", is_show);
                if (!is_open)
                {
                    ImGui::End();
                    return;
                }

                if (ImGui::Button("Select Hover"))
                    selected = ui_mgr->_hover_target;
                ImGui::SameLine();
                if (ImGui::Button("Select Capture"))
                    selected = ui_mgr->_capture_target;
                ImGui::SameLine();
                if (ImGui::Button("Clear"))
                    selected = nullptr;
                ImGui::SameLine();
                ImGui::Text("Widgets: %llu", static_cast<unsigned long long>(ui_mgr->_widgets.size()));
                auto mpos = Input::GetGlobalMousePos();
                ImGui::Text("MousePos: %.0f, %.0f", mpos.x, mpos.y);
                ImGui::Separator();

                const float details_width = std::min(360.0f, ImGui::GetContentRegionAvail().x * 0.4f);
                const float tree_width = std::max(220.0f, ImGui::GetContentRegionAvail().x - details_width - ImGui::GetStyle().ItemSpacing.x);

                ImGui::BeginChild("UIReflectorTree", ImVec2(tree_width, 0.0f), ImGuiChildFlags_Border);
                DrawUIReflectorRenderStats();
                for (const auto &widget: ui_mgr->_widgets)
                    DrawUIReflectorWidgetTree(widget.get(), selected);
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("UIReflectorDetails", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border);
                DrawUIReflectorSelectionDetails(selected);
                ImGui::EndChild();

                ImGui::End();
                ui_mgr->SetDebugHighlightTarget(selected);
            }
        }// namespace

        using SceneManagement::SceneMgr;

        class ProfileWindow
        {
        public:
            ProfileWindow() {};
            ~ProfileWindow() {};
            void Show()
            {
                ShowImpl();
            };

        public:
            Vector2f _content_size;
            bool _is_show = false;

        private:
            void ShowImpl()
            {
                if (!_is_show)
                    return;
                ImGui::Begin("ProfileWindow");
                ImVec4 origin_child_color = ImGui::GetStyle().Colors[ImGuiCol_ChildBg];
                f32 left_width = 80.0f, top_height = 120.0f;
                // 时间轴参数
                static f32 s_scale_factor = 1.0f;// 缩放系数（影响时间片宽度）
                static f32 s_offset_x = 0.0f;    // X轴偏移（拖动调整起始位置）
                //上方
                {
                    ImGui::BeginChild("Graph", ImVec2(_content_size.x, top_height), ImGuiChildFlags_Border | ImGuiChildFlags_ResizeY);
                    //ImGui::GetStyle().Colors[ImGuiCol_ChildBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
                    ImGui::Text("Graph");
                    static bool s_is_record = false;
                    auto origin_btn_c = ImGui::GetStyle().Colors[ImGuiCol_Button];
                    ImGui::GetStyle().Colors[ImGuiCol_Button] = s_is_record ? ImVec4(1.0f, 0.1f, 0.1f, 1.0f) : ImVec4(0.1f, 1.0f, 0.1f, 1.0f);
                    if (ImGui::Button(s_is_record ? "Stop" : "Start"))
                    {
                        if (s_is_record)
                        {
                            const auto &data = Profiler::Get().GetFrameData();
                            if (!_cached_frame_data.empty())
                            {
                                f32 r = (_view_frame_index - _cached_frame_data.front()._frame_index) / (f32) (_cached_frame_data.back()._frame_index - _cached_frame_data.front()._frame_index);
                                _view_frame_index = (u64) ((data.back()._frame_index - data.front()._frame_index) * r + data.front()._frame_index);
                            }
                            _cached_frame_data.clear();
                            _cached_frame_data.reserve(data.size());
                            u32 index = 0u;
                            for (auto it = data.begin(); it != data.end(); ++it)
                            {
                                if (index > 0)
                                    AL_ASSERT(_cached_frame_data.back()._end_time > it->_start_time);
                                _cached_frame_data.push_back(*it);
                            }
                        }
                        s_is_record = !s_is_record;
                        if (s_is_record)
                            Profiler::Get().StartRecord();
                        else
                            Profiler::Get().StopRecord();
                    }
                    if (_cached_frame_data.size() > 0)
                    {
                        u64 frame_start = _cached_frame_data.front()._frame_index;
                        u64 frame_end = _cached_frame_data.back()._frame_index;
                        u64 old_view_frame_index = _view_frame_index;
                        ImGui::SliderScalar("FrameIndex", ImGuiDataType_U64, &_view_frame_index, &frame_start, &frame_end, "%d", ImGuiSliderFlags_AlwaysClamp);
                        _view_data_index = (u32) (_view_frame_index - (_view_frame_index > frame_start ? frame_start : 0u));
                        if (old_view_frame_index != _view_frame_index)
                            s_offset_x = 0.0f;
                    }
                    ImGui::DragFloat("s_scale_factor", &s_scale_factor);
                    ImGui::DragFloat("s_offset_x", &s_offset_x);
                    ImGui::GetStyle().Colors[ImGuiCol_Button] = origin_btn_c;
                    ImGui::EndChild();
                }
                //下方
                const auto &all_thread = GetAllThreadNameMap();
                {
                    if (g_engine_config.isMultiThreadRender)
                    {
                        ImGui::BeginChild("TimeLineMain", ImVec2(_content_size.x, 200.0f), ImGuiChildFlags_Border | ImGuiChildFlags_ResizeY);
                        //ImGui::GetStyle().Colors[ImGuiCol_ChildBg] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                        {
                            auto main_it = std::find_if(all_thread.begin(), all_thread.end(), [](const auto &it)
                                                        { return it.second == "LogicThread"; });
                            ShowTimeLine(main_it->first, s_offset_x, s_scale_factor);
                        }
                        ImGui::EndChild();
                        ImGui::BeginChild("TimeLineRender", ImVec2(_content_size.x, 200.0f), ImGuiChildFlags_Border | ImGuiChildFlags_ResizeY);
                        //ImGui::GetStyle().Colors[ImGuiCol_ChildBg] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                        {
                            auto render_it = std::find_if(all_thread.begin(), all_thread.end(), [](const auto &it)
                                                          { return it.second == "RenderThread"; });
                            if (render_it != all_thread.end())
                                ShowTimeLine(render_it->first, s_offset_x, s_scale_factor);
                        }
                        ImGui::EndChild();
                    }
                    else
                    {
                        ImGui::BeginChild("TimeLineMain", ImVec2(_content_size.x, 200.0f), ImGuiChildFlags_Border | ImGuiChildFlags_ResizeY);
                        //ImGui::GetStyle().Colors[ImGuiCol_ChildBg] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                        {
                            auto main_it = std::find_if(all_thread.begin(), all_thread.end(), [](const auto &it)
                                                        { return it.second == "MainThread"; });
                            ShowTimeLine(main_it->first, s_offset_x, s_scale_factor);
                        }
                        ImGui::EndChild();
                    }
                }
                ImGui::GetStyle().Colors[ImGuiCol_ChildBg] = origin_child_color;
                ImGui::End();
            }
            void ShowTimeLine(std::thread::id tid, f32 &offset_x, f32 &scale_factor)
            {
                static float viewport_width = 800.0f;// 视口宽度
                // 获取可用的窗口大小
                ImVec2 canvas_size = ImGui::GetContentRegionAvail();
                viewport_width = canvas_size.x;
                // ImGui::SliderFloat("Scale", &scale_factor, 0.01f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                // ImGui::SliderFloat("OffsetX", &offset_x, -1.0f, 1.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
                // 允许鼠标滚轮缩放
                if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
                {
                    float zoom_speed = 1.1f;
                    if (ImGui::GetIO().MouseWheel > 0.0f)
                    {
                        scale_factor *= zoom_speed;// 放大
                    }
                    else
                    {
                        scale_factor /= zoom_speed;// 缩小
                    }
                    scale_factor = std::clamp(scale_factor, 0.01f, 100.0f);// 限制缩放范围
                }
                // 允许鼠标拖动平移（按住左键拖动）
                if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(0))
                {
                    offset_x -= ImGui::GetIO().MouseDelta.x;// / viewport_width;
                }

                static Vector<String> s_indents = {"", "  ", "    ", "      ", "        ", "          ", "            "};
                if (_cached_frame_data.size() > 0)
                {
                    ProfileFrameData &cur_frame_data = _cached_frame_data[_view_data_index];
                    f32 scaled_total_width = viewport_width * scale_factor;
                    static f32 s_last_scaled_total_width = scaled_total_width;
                    // 获取绘制区域
                    ImDrawList *draw_list = ImGui::GetWindowDrawList();
                    ImVec2 start_pos = ImGui::GetCursorScreenPos();
                    f32 base_y_offset = 25.0f;
                    float y_offset = 30.0f;// Y轴初始偏移（行距）
                    // 计算 Profile 时间轴范围（自动缩放到数据范围）
                    float min_time = cur_frame_data._start_time;
                    float max_time = cur_frame_data._end_time;
                    float time_range = max_time - min_time;   // 总时间跨度
                    if (time_range == 0.0f) time_range = 1.0f;// 避免除零
                    // 计算当前鼠标在时间轴上的时间点
                    ImVec2 mouse_pos = ImGui::GetMousePos();
                    f32 mouse_x = mouse_pos.x;
                    f32 mouse_x_offseted = mouse_x - start_pos.x + offset_x;
                    f32 mouse_time = min_time + mouse_x_offseted / scaled_total_width * time_range;
                    mouse_time = std::clamp(mouse_time, min_time, max_time);
                    f32 last_mouse_time = min_time + mouse_x_offseted / s_last_scaled_total_width * time_range;
                    offset_x = mouse_x_offseted / s_last_scaled_total_width * scaled_total_width - (mouse_x - start_pos.x);
                    f32 data_base_time = cur_frame_data._start_time;
                    if (_view_data_index == 0 || _view_data_index == _cached_frame_data.size())
                        return;
                    for (i32 i = -3; i < 3; i++)
                    {
                        i32 cur_data_index = _view_data_index + i;
                        if (cur_data_index < 0 || cur_data_index >= _cached_frame_data.size())
                            continue;
                        const auto &cur_data = _cached_frame_data[cur_data_index];
                        u32 event_id = 0u;
                        for (const auto &itt: cur_data._data_threads.at(tid))
                        {
                            const ProfileFrameData::ProfileEditorData &data = itt;
                            y_offset = 30 + base_y_offset * data._call_depth;
                            f32 cur_time_ratio = data._duration / time_range;
                            f32 x_start = start_pos.x + (data._start_time - data_base_time) / time_range * scaled_total_width - offset_x;
                            f32 x_end = x_start + cur_time_ratio * scaled_total_width;
                            f32 y_start = start_pos.y + y_offset;
                            f32 y_end = y_start + 20.0f;
                            auto c = Random::RandomColor(event_id++);
                            if (cur_data._frame_index != _view_frame_index)
                                c.xyz *= 0.5f;
                            if (x_end >= start_pos.x && x_start <= start_pos.x + viewport_width)
                            {
                                ImU32 color = IM_COL32(c.r * 255, c.g * 255, c.b * 255, 255);
                                draw_list->AddRectFilled(ImVec2(x_start, y_start), ImVec2(x_end, y_end), color);
                                // 显示文本
                                String label = std::format("{} ({:.2f} ms)", data._name, data._duration);
                                auto label_size = ImGui::CalcTextSize(label.c_str());
                                if (label_size.x < (x_end - x_start))
                                    draw_list->AddText(ImVec2(x_start + 5, y_start + 3), IM_COL32(0, 0, 0, 255), label.c_str());
                                if (mouse_pos.x >= x_start && mouse_pos.x <= x_end &&
                                    mouse_pos.y >= y_start && mouse_pos.y <= y_end)
                                {
                                    // 显示 Tooltip
                                    ImGui::BeginTooltip();
                                    ImGui::Text("Name: %s", data._name.c_str());
                                    ImGui::Text("Start Time: %.4f ms", data._start_time);
                                    ImGui::Text("Duration: %.4f ms", data._duration);
                                    ImGui::Text("Stop Time: %.4f ms", data._start_time + data._duration);
                                    ImGui::EndTooltip();
                                }
                            }
                        }
                    }

                    s_last_scaled_total_width = scaled_total_width;
                }
            }

        private:
            Vector<ProfileFrameData> _cached_frame_data;
            u64 _view_frame_index = 0u;
            u32 _view_data_index = 0u;
        };


        static ProfileWindow *s_prifile_wd = nullptr;
        EditorLayer::EditorLayer() : EditorLayer("EditorLayer")
        {
        }

        EditorLayer::EditorLayer(const String &name) : Layer(name)
        {
            s_prifile_wd = new ProfileWindow();
            s_prifile_wd->_content_size = Vector2f(800.0f, 600.0f);

            Selection::on_selection_changed += [this]()
            {
                _selected_entity = Selection::FirstEntity();
                if (_selected_entity != ECS::kInvalidEntity)
                {
                    auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
                    if (r.HasComponent<ECS::CCamera>(_selected_entity))
                        Camera::sSelected = &r.GetComponent<ECS::CCamera>(_selected_entity)->_camera;
                    else
                        Camera::sSelected = nullptr;
                }
                else
                {
                    Camera::sSelected = nullptr;
                }
            };
        }

        EditorLayer::~EditorLayer()
        {
            delete s_prifile_wd; s_prifile_wd = nullptr;
        }
        UI::Text *g_text = nullptr;
        static void FindFirstText(UI::Widget *w)
        {
            for (auto c: *w)
            {
                if (c->GetType() == UI::Text::StaticType())
                {
                    g_text = dynamic_cast<UI::Text *>(c.get());
                    break;
                }
            }
        }

        namespace
        {
            constexpr f32 kEditorToolbarHeight = 34.0f;
            constexpr f32 kEditorStatusBarHeight = 24.0f;

            UI::Button *AddToolbarButton(UI::HorizontalBox *toolbar, const String &text, f32 width)
            {
                auto *button = toolbar->AddChild<UI::Button>(text);
                button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                        .Size({width, 26.0f})
                        .Margin({4.0f, 4.0f, 0.0f, 4.0f});
                return button;
            }

            void ResizeWidgetRoot(const Ref<UI::Widget> &widget, Vector2f position, Vector2f size)
            {
                if (!widget || !widget->Root())
                    return;
                widget->SetPosition(position);
                widget->SetSize(size);
                widget->Root()->Arrange(0.0f, 0.0f, size.x, size.y);
            }
        }

        RenderView *render_view;
        void EditorLayer::OnAttach()
        {
            auto r = Render::RenderPipeline::Get().GetRenderer();
            r->AddFeature(&_pick);
            r->SetShadingMode(EShadingMode::kLit);


            DockManager::Init();

            _main_widget = MakeRef<UI::Widget>();

            auto p = ResourceMgr::GetResSysPath(EAssetDomain::kEditor,L"UI/main_widget.json");
            JsonArchive ar;
            ar.Load(p);
            ar >> *_main_widget;
            FindFirstText(_main_widget.get());
            dynamic_cast<EditorApp &>(Application::Get())._on_file_changed += [&](const fs::path &file)
            {
                const WString cur_path = PathUtils::FormatFilePath(file.wstring());
                if (PathUtils::GetFileName(cur_path) == L"main_widget")
                {
                    LOG_INFO(L"Reload widget {}", cur_path);
                    JsonArchive ar;
                    ar.Load(cur_path);
                    UI::UIManager::Get()->UnRegisterWidget(_main_widget.get());
                    _main_widget.reset();
                    _main_widget = MakeRef<UI::Widget>();
                    _main_widget->BindOutput(RenderTexture::s_backbuffer, nullptr);
                    ar >> *_main_widget;
                    UI::UIManager::Get()->RegisterWidget(_main_widget);
                    FindFirstText(_main_widget.get());
                }
            };

            BuildEditorChrome();
        }

        void EditorLayer::OnDetach()
        {
            if (_toolbar_widget)
                UI::UIManager::Get()->UnRegisterWidget(_toolbar_widget.get());
            if (_status_bar_widget)
                UI::UIManager::Get()->UnRegisterWidget(_status_bar_widget.get());
            _toolbar_widget.reset();
            _status_bar_widget.reset();
            _status_bar_border = nullptr;
            _was_playing = false;
            _status_left_text = nullptr;
            _status_right_text = nullptr;
            DockManager::Shutdown();
            JsonArchive ar;
            ar << *_main_widget;
            auto p = ResourceMgr::GetResSysPath(EAssetDomain::kEditor,L"UI/main_widget.json");
            LOG_INFO(L"Save widget to ", p);
            ar.Save(p);
        }
        static CCDSolver ccd_solver;
        //static CCDSolver ccd_solver1;
        static CCDSolver fabrik_solver;
        void EditorLayer::OnEvent(Event &e)
        {
            if (e.GetEventType() == EEventType::kKeyPressed)
            {
                auto &key_e = static_cast<KeyPressedEvent &>(e);
                if (key_e.GetKeyCode() == EKey::kF11)
                {
                    LOG_INFO("Capture frame...");
                    g_pGfxContext->TakeCapture();
                }
                if (key_e.GetKeyCode() == EKey::kS)
                {
                    if (Input::IsKeyDownAccurate(EKey::kCONTROL))
                    {
                        LOG_INFO("Save assets...");
                        Core::ThreadPool::Get().Enqueue([]()
                                                        {
                                                   f32 asset_count = 1.0f;
                                                   for(auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); it++)
                                                   {
                                                       ResourceMgr::Get().SaveAsset(it->second.get());
                                                       //ImGuiWidget::DisplayProgressBar("SaveAsset...",asset_count / (f32)ResourceMgr::Get().AssetNum());
                                                       asset_count += 1.0;
                                                   } });
                    }
                }
                if (key_e.GetKeyCode() == EKey::kZ)
                {
                    if (Input::IsKeyDownAccurate(EKey::kCONTROL))
                    {
                        if (Input::IsKeyDownAccurate(EKey::kSHIFT))
                        {
                            g_pCommandMgr->Redo();
                        }
                        else
                        {
                            g_pCommandMgr->Undo();
                        }
                    }
                }
                if (key_e.GetKeyCode() == EKey::kD)
                {
                    if (Input::IsKeyDownAccurate(EKey::kCONTROL))
                    {
                        List<ECS::Entity> new_entities;
                        for (auto e: Selection::SelectedEntities())
                        {
                            new_entities.push_back(SceneMgr::Get().ActiveScene()->DuplicateEntity(e));
                        }
                        Selection::RemoveSlection();
                        for (auto &e: new_entities)
                            Selection::AddSelection(e);
                    }
                }
                if (key_e.GetKeyCode() == EKey::kDELETE)
                {
                    List<ECS::Entity> to_delete;
                    for (auto e: Selection::SelectedEntities())
                    {
                        to_delete.push_back(e);
                    }
                    for (auto &e: to_delete)
                        SceneMgr::Get().ActiveScene()->RemoveObject(e);
                    Selection::RemoveSlection();
                }
                if (key_e.GetKeyCode() == EKey::kF)
                {
                    static u16 s_count = 1;
                    Solver::s_is_apply_constraint = s_count % 2;
                    ccd_solver.IsApplyConstraint(s_count % 2);
                    fabrik_solver.IsApplyConstraint(s_count % 2);
                    ++s_count;
                }
                if (key_e.GetKeyCode() == EKey::kQ)
                    _is_transform_gizmo_world = !_is_transform_gizmo_world;
                else if (key_e.GetKeyCode() == EKey::kW)
                    _transform_gizmo_type = (i16) ImGuizmo::OPERATION::TRANSLATE;
                else if (key_e.GetKeyCode() == EKey::kE)
                    _transform_gizmo_type = (i16) ImGuizmo::OPERATION::ROTATE;
                else if (key_e.GetKeyCode() == EKey::kR)
                    _transform_gizmo_type = (i16) ImGuizmo::OPERATION::SCALE;
                else
                {
                };
            }
            else if (e.GetEventType() == EEventType::kMouseButtonPressed)
            {
                MouseButtonPressedEvent &event = static_cast<MouseButtonPressedEvent &>(e);
                if (event.GetButton() == EKey::kLBUTTON)
                {
                    auto m_pos = Input::GetMousePos();
                    //if (_p_scene_view->Hover(m_pos) && _p_scene_view->Focus())
                    //{
                    //    m_pos = m_pos - _p_scene_view->ContentPosition();
                    //    ECS::Entity closest_entity = _pick.GetPickID((u16)m_pos.x, (u16)m_pos.y);
                    //    LOG_INFO("Pick entity: {}", closest_entity);
                    //    if (Input::IsKeyPressed(EKey::kSHIFT))
                    //        Selection::AddSelection(closest_entity);
                    //    else
                    //        Selection::AddAndRemovePreSelection(closest_entity);
                    //    if (closest_entity == ECS::kInvalidEntity)
                    //        Selection::RemoveSlection();
                    //}
                }
            }
            else if (e.GetEventType() == EEventType::kMouseScroll)
            {
            }
        }
        std::once_flag flag;
        void EditorLayer::OnUpdate(f32 dt)
        {
            UpdateEditorChrome(dt);
            DockManager::Get().Update(dt);
            //Gizmo::DrawLine(Vector2f::kZero, Vector2f{200, 200}, Colors::kRed);
            //LOG_INFO("--------------------------------------");
            //LOG_INFO("EditorLayer::OnUpdate: mpos({})", Input::GetMousePos().ToString());
            ccd_solver.Resize(3);
            ccd_solver[0]._position = Vector3f(0, 9, 0);
            ccd_solver[1]._position = Vector3f(0, -3, 0);
            ccd_solver[2]._position = Vector3f(0, -3, 0);
            //ccd_solver[3]._position = Vector3f(0, 0, 1);
            fabrik_solver.Resize(3);
            fabrik_solver[0]._position = Vector3f(0, 9, 0);
            fabrik_solver[1]._position = Vector3f(0, -3, 0);
            fabrik_solver[2]._position = Vector3f(0, -3, 0);
            //fabrik_solver[3]._position = Vector3f(0, 0, 1);


            std::call_once(flag, []()
                           {
                        //for (u16 i = 0; i < ccd_solver.Size(); i++)
            {
                //ccd_solver.AddConstraint(0, new HingeConstraint(Vector3f::kRight));
                //ccd_solver.AddConstraint(1, new HingeConstraint(Vector3f::kRight));
                //ccd_solver.AddConstraint(2, new HingeConstraint(Vector3f::kRight));
                //ccd_solver.AddConstraint(0, new BallSocketConstraint(90.f));
                //ccd_solver.AddConstraint(1, new BallSocketConstraint(-160.f, 0.f));
                //ccd_solver.AddConstraint(2, new BallSocketConstraint(45.f));
                //fabrik_solver.AddConstraint(0, new HingeConstraint(Vector3f::kRight));
                //fabrik_solver.AddConstraint(1, new HingeConstraint(Vector3f::kRight));
                //fabrik_solver.AddConstraint(2, new HingeConstraint(Vector3f::kRight));
                //fabrik_solver.AddConstraint(0, new BallSocketConstraint(90.f));
                //fabrik_solver.AddConstraint(1, new BallSocketConstraint(-160.f,0.f));
                //fabrik_solver.AddConstraint(2, new BallSocketConstraint(45.f));

            } });


            //if (ccd_solver.Size() > 0)
            //{

            //    Gizmo::DrawLine(ccd_solver.GetGlobalTransform(0)._position, ccd_solver.GetGlobalTransform(1)._position, Colors::kRed);
            //    Gizmo::DrawLine(ccd_solver.GetGlobalTransform(1)._position, ccd_solver.GetGlobalTransform(2)._position, Colors::kBlue);
            //    //Gizmo::DrawLine(ccd_solver.GetGlobalTransform(2)._position, ccd_solver.GetGlobalTransform(3)._position, Colors::kWhite);

            //    Gizmo::DrawLine(fabrik_solver.GetGlobalTransform(0)._position, fabrik_solver.GetGlobalTransform(1)._position, Colors::kYellow);
            //    Gizmo::DrawLine(fabrik_solver.GetGlobalTransform(1)._position, fabrik_solver.GetGlobalTransform(2)._position, Colors::kGreen);
            //    //Gizmo::DrawLine(fabrik_solver.GetGlobalTransform(2)._position, fabrik_solver.GetGlobalTransform(3)._position, Colors::kCyan);
            //}
            //            Bezier<Vector3f> curve;
            //            curve.P1 = Vector3f(-5, 0, 0);
            //            curve.P2 = Vector3f(5, 0, 0);
            //            curve.C1 = Vector3f(-2, 1, 0);
            //            curve.C2 = Vector3f(2, 1, 0);
            //            Gizmo::DrawLine(curve.P1,curve.C1,Colors::kBlue);
            //            Gizmo::DrawLine(curve.P2,curve.C2,Colors::kBlue);
            //            for (int i = 0; i < 199; ++ i)
            //            {
            //                float t0 = (float)i / 199.0f;
            //                float t1 = (float)(i + 1) / 199.0f;
            //                Vector3f thisPoint = Bezier<Vector3f>::Interpolate(curve, t0);
            //                Vector3f nextPoint = Bezier<Vector3f>::Interpolate(curve, t1);
            //                Gizmo::DrawLine(thisPoint, nextPoint, Colors::kCyan);
            //            }
        }

        void EditorLayer::BuildEditorChrome()
        {
            _toolbar_widget = MakeRef<UI::Widget>();
            _toolbar_widget->Name("EditorToolbar");
            auto toolbar_border = MakeRef<UI::Border>();
            toolbar_border->_bg_color = Color(0.18f, 0.19f, 0.21f, 1.0f);
            toolbar_border->_border_color = Color(0.34f, 0.36f, 0.40f, 1.0f);
            toolbar_border->Thickness({0.0f, 0.0f, 0.0f, 1.0f});
            auto *toolbar = toolbar_border->AddChild<UI::HorizontalBox>();
            toolbar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto *open_scene = AddToolbarButton(toolbar, "Open Scene", 96.0f);
            open_scene->OnMouseClick() += [this](UI::UIEvent &e)
            {
                _editor_status_message = "Open Scene is not wired yet";
                LOG_INFO("EditorToolbar: Open Scene clicked");
                e._is_handled = true;
            };

            auto *save_scene = AddToolbarButton(toolbar, "Save Scene", 92.0f);
            save_scene->OnMouseClick() += [this](UI::UIEvent &e)
            {
                SaveAllAssets();
                e._is_handled = true;
            };

            auto *play = AddToolbarButton(toolbar, "Play", 58.0f);
            play->OnMouseClick() += [this](UI::UIEvent &e)
            {
                if (!Application::Get()._is_playing_mode)
                {
                    SceneMgr::Get().EnterPlayMode();
                    _editor_status_message = "Play mode";
                    LOG_INFO("EditorToolbar: enter play mode");
                }
                e._is_handled = true;
            };

            auto *pause = AddToolbarButton(toolbar, "Pause", 64.0f);
            pause->OnMouseClick() += [this](UI::UIEvent &e)
            {
                _editor_status_message = "Pause is not wired yet";
                LOG_INFO("EditorToolbar: Pause clicked");
                e._is_handled = true;
            };

            auto *stop = AddToolbarButton(toolbar, "Stop", 58.0f);
            stop->OnMouseClick() += [this](UI::UIEvent &e)
            {
                if (Application::Get()._is_playing_mode)
                {
                    SceneMgr::Get().ExitPlayMode();
                    _editor_status_message = "Edit mode";
                    LOG_INFO("EditorToolbar: exit play mode");
                }
                e._is_handled = true;
            };

            auto *spacer = toolbar->AddChild<UI::Text>("");
            spacer->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto *title = toolbar->AddChild<UI::Text>("AiluEngine");
            title->_color = Colors::kGray;
            title->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kFill)
                    .Margin({0.0f, 0.0f, 10.0f, 0.0f});
            _toolbar_widget->AddToWidget(toolbar_border);
            UI::UIManager::Get()->RegisterWidget(_toolbar_widget);

            _status_bar_widget = MakeRef<UI::Widget>();
            _status_bar_widget->Name("EditorStatusBar");
            _status_bar_widget->_is_receive_event = false;
            auto status_border = MakeRef<UI::Border>();
            status_border->_bg_color = Color(0.16f, 0.17f, 0.18f, 1.0f);
            status_border->_border_color = Color(0.34f, 0.36f, 0.40f, 1.0f);
            status_border->Thickness({0.0f, 1.0f, 0.0f, 0.0f});
            auto *status = status_border->AddChild<UI::HorizontalBox>();
            status->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            _status_left_text = status->AddChild<UI::Text>("Ready");
            _status_left_text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                    .Margin({8.0f, 0.0f, 0.0f, 0.0f});
            _status_left_text->_color = Colors::kGray;

            _status_right_text = status->AddChild<UI::Text>("");
            _status_right_text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kFill)
                    .Margin({0.0f, 0.0f, 8.0f, 0.0f});
            _status_right_text->_color = Colors::kGray;

            _status_bar_border = status_border.get();
            _status_bar_widget->AddToWidget(status_border);
            UI::UIManager::Get()->RegisterWidget(_status_bar_widget);
            UpdateEditorChrome(0.0f);
        }

        void EditorLayer::UpdateEditorChrome(f32 dt)
        {
            (void) dt;
            auto &window = Application::Get().GetWindow();
            const Vector2f window_size{(f32) window.GetWidth(), (f32) window.GetHeight()};
            const f32 dock_height = std::max(0.0f, window_size.y - kEditorToolbarHeight - kEditorStatusBarHeight);
            ResizeWidgetRoot(_toolbar_widget, Vector2f::kZero, {window_size.x, kEditorToolbarHeight});
            ResizeWidgetRoot(_status_bar_widget, {0.0f, kEditorToolbarHeight + dock_height}, {window_size.x, kEditorStatusBarHeight});
            DockManager::Get().SetMainDockArea({0.0f, kEditorToolbarHeight}, {window_size.x, dock_height});

            auto *scene = SceneMgr::Get().ActiveScene();
            const String scene_name = scene ? scene->Name() : String("No Scene");
            String selection_name = "No Selection";
            if (scene && _selected_entity != ECS::kInvalidEntity && scene->IsValidEntity(_selected_entity))
            {
                auto &r = scene->GetRegister();
                if (auto *tag = r.GetComponent<ECS::TagComponent>(_selected_entity); tag != nullptr && !tag->_name.empty())
                    selection_name = tag->_name;
                else
                    selection_name = "Entity";
            }

            // Update status bar background color based on play mode (VS Code-style)
            const bool is_playing = Application::Get()._is_playing_mode;
            if (_status_bar_border && _was_playing != is_playing)
            {
                _was_playing = is_playing;
                if (is_playing)
                    _status_bar_border->_bg_color = Color(0.85f, 0.45f, 0.10f, 1.0f);
                else
                    _status_bar_border->_bg_color = Color(0.16f, 0.17f, 0.18f, 1.0f);
                _status_bar_border->InvalidateStyle();
            }

            if (_status_left_text)
            {
                _status_left_text->SetText(std::format("{}   Scene: {}   Selection: {}",
                                                       _editor_status_message,
                                                       scene_name,
                                                       selection_name),
                                           false);
            }
            if (_status_right_text)
            {
                const auto& ds = RenderingStates::DisplayData();
                _status_right_text->SetText(std::format("FPS: {:.1f}   Frame: {:.2f} ms",
                                                        ds.FrameRate,
                                                        ds.FrameTime),
                                            false);
            }

            auto *ui_mgr = UI::UIManager::Get();
            if (_toolbar_widget)
                ui_mgr->BringToFrontSilently(_toolbar_widget.get());
            if (_status_bar_widget)
                ui_mgr->BringToFrontSilently(_status_bar_widget.get());
        }

        void EditorLayer::SaveAllAssets()
        {
            _editor_status_message = "Save assets queued";
            LOG_INFO("EditorToolbar: Save Scene clicked, saving assets...");
            Core::ThreadPool::Get().Enqueue([]()
            {
                for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); it++)
                {
                    ResourceMgr::Get().SaveAsset(it->second.get());
                }
            });
        }

        static bool show = false;
        static bool s_show_plot_demo = false;
        static bool s_show_asset_table = false;
        static bool s_show_rt = false;
        static bool s_show_renderview = true;
            static bool s_show_threadpool_view = false;
            static bool s_show_imguinode = false;
            static bool s_show_ui_reflector = false;
            static bool s_show_style_theme_editor = false;
            static bool s_show_resource_browser = false;
            static bool s_show_allocator_debug_panel = false;

        static void ShowThreadPoolView(bool *is_show)
        {
            static Vector<List<std::tuple<String, f32, f32>>> s_foucs_records;
            static bool s_foucs_record = false;
            ImGui::Begin("ThreadPoolView", is_show);
            if (ImGui::Button("Capture"))
            {
                s_foucs_records = Core::ThreadPool::Get().TaskTimeRecordView();
                s_foucs_record = !s_foucs_record;
            }
            for (u16 i = 0; i < Core::ThreadPool::Get().ThreadNum(); i++)
            {
                ImGui::Text("Thread %d", i);
                ImGui::Indent();
                ImGui::Text("ThreadStatus: %s", ThreadStatusToString(Core::ThreadPool::Get().Status(i)));
                const auto &records = s_foucs_record ? s_foucs_records[i] : Core::ThreadPool::Get().TaskTimeRecord(i);
                if (records.size() > 0)
                {
                    u16 record_index = 0;
                    for (auto &record: records)
                    {
                        auto &[name, start, duration] = record;
                        ImGui::Text("%s %.2fms %.2fms; ", name.c_str(), start, start + duration);
                        if (record_index++ != records.size() - 1)
                            ImGui::SameLine();
                    }
                }
                ImGui::Unindent();
            }
            ImGui::End();
        }

        void EditorLayer::OnImguiRender()
        {
            static bool s_show_undo_view = false;
            static Scope<Process> s_package_player_process;
            static String s_package_player_status = "Idle";
            MemoryDebugService::Get().Tick(TimeMgr::s_delta_time);

            if (s_package_player_process && s_package_player_process->IsValid() && !s_package_player_process->IsRunning())
            {
                u32 exit_code = 0u;
                if (s_package_player_process->TryGetExitCode(exit_code) && exit_code == 0u)
                {
                    s_package_player_status = "package_player finished";
                    LOG_INFO("package_player finished");
                }
                else
                {
                    s_package_player_status = "package_player failed";
                    if (s_package_player_process->TryGetExitCode(exit_code))
                        s_package_player_status += " (exit=" + std::to_string(exit_code) + ")";
                    LOG_ERROR("{}", s_package_player_status);
                }
                s_package_player_process->Close();
                s_package_player_process.reset();
            }

            const bool is_packaging_player = s_package_player_process && s_package_player_process->IsValid() && s_package_player_process->IsRunning();
            ImGui::Begin("Common");// Create a window called "Hello, world!" and append into it.
            const auto& ds = RenderingStates::DisplayData();
            ImGui::Text("FrameRate: %.2f", ds.FrameRate);
            ImGui::Text("FrameTime: %.2f ms", ds.FrameTime);
            ImGui::Text("GpuLatency: %.2f ms", ds.GpuLatency);
            ImGui::Text("Draw Call: %d", ds.DrawCall);
            ImGui::Text("Dispatch Call: %d", ds.DispatchCall);
            ImGui::Text("VertCount: %d", ds.VertexNum);
            ImGui::Text("TriCount: %d", ds.TriangleNum);
            ImGui::Text("Gfx PSO: %d", ds.GfxPsoBindCount);
            ImGui::Text("Gfx PSO Dirty: %d", ds.GfxPsoDirtyCount);
            ImGui::Text("Gfx Res: %d", ds.GfxResBindCount);
            if (ImGui::CollapsingHeader("Render Submission Stats", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto percent = [](u64 numerator, u64 denominator) -> f32
                {
                    return denominator == 0u ? 0.0f : static_cast<f32>(numerator) * 100.0f / static_cast<f32>(denominator);
                };
                const u64 pso_total = ds.PsoCacheHitCount + ds.PsoCacheMissCount;
                const u64 root_slot_total = ds.ActualRootSlotBindCount + ds.SkippedRootSlotBindCount;
                const u64 material_binding_total = ds.MaterialBindingResolveCount + ds.MaterialBindingCacheHitCount;
                const u64 material_upload_total = ds.MaterialCBufferUploadCount + ds.MaterialCBufferCacheHitCount;

                ImGui::Text("Draw Commands: %llu", static_cast<unsigned long long>(ds.DrawCommandCount));
                ImGui::Text("PSO Lookups: %llu", static_cast<unsigned long long>(ds.PsoLookupCount));
                ImGui::Text("PSO Cache Hit/Miss: %llu / %llu (%.1f%%)", static_cast<unsigned long long>(ds.PsoCacheHitCount),
                            static_cast<unsigned long long>(ds.PsoCacheMissCount), percent(ds.PsoCacheHitCount, pso_total));
                ImGui::Text("PSO Dirty: %llu", static_cast<unsigned long long>(ds.GfxPsoDirtyCount));
                ImGui::Separator();
                ImGui::Text("Material Capture: %llu", static_cast<unsigned long long>(ds.MaterialCaptureCount));
                ImGui::Text("Material Binding Resolve/Cache: %llu / %llu (%.1f%% cache)",
                            static_cast<unsigned long long>(ds.MaterialBindingResolveCount),
                            static_cast<unsigned long long>(ds.MaterialBindingCacheHitCount),
                            percent(ds.MaterialBindingCacheHitCount, material_binding_total));
                ImGui::Text("Material CBuffer Upload/Cache: %llu / %llu (%.1f%% cache)",
                            static_cast<unsigned long long>(ds.MaterialCBufferUploadCount),
                            static_cast<unsigned long long>(ds.MaterialCBufferCacheHitCount),
                            percent(ds.MaterialCBufferCacheHitCount, material_upload_total));
                ImGui::Text("Material CBuffer Bytes: %llu", static_cast<unsigned long long>(ds.MaterialCBufferUploadBytes));
                ImGui::Separator();
                ImGui::Text("Pipeline Resource Submit: %llu", static_cast<unsigned long long>(ds.PipelineResourceSubmitCount));
                ImGui::Text("Pipeline Resource Override: %llu", static_cast<unsigned long long>(ds.PipelineResourceOverrideCount));
                ImGui::Text("Root Slot Bind/Skip: %llu / %llu (%.1f%% skip)",
                            static_cast<unsigned long long>(ds.ActualRootSlotBindCount),
                            static_cast<unsigned long long>(ds.SkippedRootSlotBindCount),
                            percent(ds.SkippedRootSlotBindCount, root_slot_total));
                ImGui::Text("Resource Mark Request/Unique: %llu / %llu (%.1f%% unique)",
                            static_cast<unsigned long long>(ds.ResourceMarkRequestCount),
                            static_cast<unsigned long long>(ds.UniqueResourceMarkCount),
                            percent(ds.UniqueResourceMarkCount, ds.ResourceMarkRequestCount));
            }
            if (ImGui::CollapsingHeader("Features"))
            {
                for (auto feature: Render::RenderPipeline::Get().GetRenderer()->GetFeatures())
                {
                    bool active = feature->IsActive();
                    ImGui::Checkbox(feature->Name().c_str(), &active);
                    feature->SetActive(active);
                    auto *cur_type = feature->GetType();
                    ImGui::Text("Type: %s", cur_type->Name().c_str());
                    ImGui::Text("Members: ");
                    //if (cur_type->BaseType() && cur_type->BaseType() != cur_type)//除去Object
                    {
                        for (auto &prop: cur_type->GetProperties())
                        {
                            DrawMemberProperty(prop, feature);
                        }
                    }
                }
            }

            static bool s_show_shadding_mode = false;
            if (ImGui::CollapsingHeader("ShadingMode"))
            {
                static u16 s_selected_chechbox = 0;
                bool checkbox1 = (s_selected_chechbox == 0);
                if (ImGui::Checkbox("Lit", &checkbox1))
                {
                    if (checkbox1)
                    {
                        Render::RenderPipeline::Get().GetRenderer()->SetShadingMode(EShadingMode::kLit);
                        s_selected_chechbox = 0;
                    }
                }
                bool checkbox2 = (s_selected_chechbox == 1);
                if (ImGui::Checkbox("Wireframe", &checkbox2))
                {
                    if (checkbox2)
                    {
                        Render::RenderPipeline::Get().GetRenderer()->SetShadingMode(EShadingMode::kWireframe);
                        s_selected_chechbox = 1;
                    }
                }
                bool checkbox3 = (s_selected_chechbox == 2);
                if (ImGui::Checkbox("LitWireframe", &checkbox3))
                {
                    if (checkbox3)
                    {
                        Render::RenderPipeline::Get().GetRenderer()->SetShadingMode(EShadingMode::kLitWireframe);
                        s_selected_chechbox = 2;
                    }
                }
            }
            // static bool s_show_pass = false;
            // if (ImGui::CollapsingHeader("Features"))
            // {
            //     for (auto feature: Render::RenderPipeline::Get().GetRenderer()->GetFeatures())
            //     {
            //         bool active = feature->IsActive();
            //         ImGui::Checkbox(feature->Name().c_str(), &active);
            //         feature->SetActive(active);
            //     }
            // }
            static bool s_show_time_info = false;
            if (ImGui::CollapsingHeader("Performace Statics"))
            {
                ImGui::Text("GPU Time:");
                for (const auto &it: Profiler::Get().GetPassGPUTime())
                {
                    auto &[name, time] = it;
                    ImGui::Text("   %s,%.2f ms", name.c_str(), time);
                }
            }
            ImGui::SliderFloat("Gizmo Alpha:", &Gizmo::s_color.a, 0.01f, 1.0f, "%.2f");
            ImGui::SliderFloat("Game Time Scale:", &TimeMgr::s_time_scale, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("ShadowDistance m", &QuailtySetting::s_main_light_shaodw_distance, 0.f, 100.0f, "%.2f");

            static bool s_state_batching = g_engine_config.EnableCpuStateBatchedSubmission;
            if (ImGui::Checkbox("State Batching", &s_state_batching))
            {
                g_engine_config.EnableCpuStateBatchedSubmission = s_state_batching;
                g_engine_config.EnableIncrementalGraphicsBinding = s_state_batching;
            }

            //g_pRenderer->_shadow_distance = shadow_dis_m * 100.0f;
            static bool s_show_anim_clip = false;
            static bool s_raytracing_pipeline = false;
            ImGui::Checkbox("Expand", &show);
            ImGui::Checkbox("ShowPlotDemo", &s_show_plot_demo);
            ImGui::Checkbox("ShowAssetTable", &s_show_asset_table);
            ImGui::Checkbox("ShowRT", &s_show_rt);
            ImGui::Checkbox("ShowUndoView", &s_show_undo_view);
            ImGui::Checkbox("ShowAnimClip", &s_show_anim_clip);
            ImGui::Checkbox("ShowThreadPoolView", &s_show_threadpool_view);
            ImGui::Checkbox("ShowNode", &s_show_imguinode);
            ImGui::Checkbox("ShowUIReflector", &s_show_ui_reflector);
            ImGui::Checkbox("ShowStyleThemeEditor", &s_show_style_theme_editor);
            ImGui::Checkbox("ShowResourceBrowser", &s_show_resource_browser);
            ImGui::Checkbox("AllocatorDebug", &s_show_allocator_debug_panel);
            ImGui::Checkbox("Raytracing Pipeline", &s_raytracing_pipeline);
            RenderPipeline::Get().GetRenderer()->_is_use_raytracing = s_raytracing_pipeline;
            if (ImGui::Button("Capture RDG"))
            {
                RenderPipeline::Get().GetRenderer()->GetRenderGraph()._is_debug = true;
            }
            if (is_packaging_player)
                ImGui::BeginDisabled();
            if (ImGui::Button(is_packaging_player ? "Packaging Player..." : "Build/Package Player"))
            {
                ProcessStartInfo psi(BuildPackagePlayerCommandLine());
                psi.WorkingDirectory = Application::GetProjectRootPath();
                auto process = ProcessFactory::Create();
                if (process && process->Start(psi))
                {
                    s_package_player_status = "package_player running";
                    LOG_INFO(L"Start package_player in {}", psi.WorkingDirectory);
                    s_package_player_process = std::move(process);
                }
                else
                {
                    s_package_player_status = "failed to start package_player";
                    LOG_ERROR("failed to start package_player");
                }
            }
            if (is_packaging_player)
                ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextUnformatted(s_package_player_status.c_str());

            ImGui::Checkbox("Postprocess", &Camera::sCurrent->_is_enable_postprocess);
            //ImGui::Checkbox("UseRenderGraph", &RenderPipeline::Get().GetRenderer()->_is_use_render_graph);

            // for (auto &info: ResourceMgr::Get().GetImportInfos())
            // {
            //     float x = TimeMgr::Get().GetScaledWorldTime(0.25f);
            //     ImGui::Text("%s", info._msg.c_str());
            //     ImGui::SameLine();
            //     ImGui::ProgressBar(x - static_cast<int>(x), ImVec2(0.f, 0.f));
            // }
            auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
            if (_selected_entity != ECS::kInvalidEntity)
            {
                if (r.HasComponent<ECS::CCamera>(_selected_entity))
                {
                    auto &cam = r.GetComponent<ECS::CCamera>(_selected_entity)->_camera;
                    cam.SetPixelSize(300, (u16) (300.0f / cam.Aspect()));
                }
                if (auto sk_comp = r.GetComponent<ECS::CSkeletonMesh>(_selected_entity); sk_comp != nullptr)
                {
                    //g_blend_space_editor->SetTarget(&sk_comp->_blend_space);
                }
                if (auto sk_comp = r.GetComponent<ECS::TransformComponent>(_selected_entity); sk_comp != nullptr)
                {
                    ImGui::DragFloat3("Position", sk_comp->_local_transform._position.data);
                    Vector3f euler = Quaternion::EulerAngles(sk_comp->_local_transform._rotation);
                    ImGui::DragFloat3("Rotation", euler.Data());
                    sk_comp->_local_transform._rotation = Quaternion::EulerAngles(euler);
                    ImGui::DragFloat3("Scale", sk_comp->_local_transform._scale.data);
                }
            }
            auto &cam_controller = dynamic_cast<EditorApp &>(Application::Get()).GetSceneCameraController();
            ImGui::SliderFloat("CameraNear", &cam_controller._camera_near, 0.0f, 10.0f);
            ImGui::SliderFloat("CameraFar", &cam_controller._camera_far, cam_controller._camera_near, 10000.0f);
            Vector3f cam_pos = cam_controller._target_pos;
            Vector2f cam_rot = cam_controller._rotation;
            ImGui::DragFloat3("CameraPos", cam_pos.data, 0.1f);
            ImGui::DragFloat2("CameraRot", cam_rot.data, 0.1f);
            cam_controller.SetTargetPosition(cam_pos, true);
            cam_controller.SetTargetRotation(cam_rot.x, cam_rot.y, true);
            static bool s_draw_scene_bvh = false;
            static bool s_draw_mesh_bvh = false;
            //bvh debug
            {
                ImGui::Checkbox("ShowSceneBVH", &s_draw_scene_bvh);
                if (s_draw_scene_bvh)
                {
                    for (const auto &n: SceneMgr::Get().ActiveScene()->GetBVHNodes())
                        Render::Gizmo::DrawAABB(n._aabb);
                }
                if (_selected_entity != ECS::kInvalidEntity)
                {
                    ImGui::Checkbox("DrawMeshBVH", &s_draw_mesh_bvh);
                    static auto draw_tri = [](float3 v0, float3 v1, float3 v2, Color c = Colors::kGreen)
                    {
                        Gizmo::DrawLine(v0, v1, c);
                        Gizmo::DrawLine(v1, v2, c);
                        Gizmo::DrawLine(v2, v0, c);
                    };
                    static auto scale_aabb = [](const AABB &b, f32 scale) -> auto
                    {
                        AABB result = b;
                        Vector3f dmin = b._min - b.Center();
                        Vector3f dmax = b._max - b.Center();
                        dmin *= scale;
                        dmax *= scale;
                        result._min = b.Center() + dmin;
                        result._max = b.Center() + dmax;
                        return result;
                    };
                    if (auto sk_comp = r.GetComponent<ECS::StaticMeshComponent>(_selected_entity); sk_comp != nullptr)
                    {
                        if (s_draw_mesh_bvh)
                        {
                            const auto &nodes = sk_comp->_p_mesh->GetBVHNodes();
                            static i32 draw_idx = -1;
                            ImGui::SliderInt("MeshBVH Index", &draw_idx, -1, (i32) nodes.size() - 1);
                            if (draw_idx == -1)
                            {
                                const auto &tri_data = sk_comp->_p_mesh->GetTriangleData();
                                Matrix4x4f scale = MatrixScale(0.9f, 0.9f, 0.9f);
                                for (const auto &n: nodes)
                                {
                                    if (!n.IsLeaf())
                                        continue;
                                    for (i32 i = n._child_index_or_first; i < n._child_index_or_first + n._count_or_flag; i++)
                                    {
                                        Vector3f v0 = TransformCoord(scale, tri_data[i].v0);
                                        Vector3f v1 = TransformCoord(scale, tri_data[i].v1);
                                        Vector3f v2 = TransformCoord(scale, tri_data[i].v2);
                                        draw_tri(v0, v1, v2, Colors::kCyan);
                                    }
                                }
                            }
                            else
                            {
                                const auto &n = nodes[draw_idx];
                                Render::Gizmo::DrawAABB(n._aabb);
                                if (n.IsLeaf())
                                {
                                    Matrix4x4f scale = MatrixScale(0.9f, 0.9f, 0.9f);
                                    const auto &tri_data = sk_comp->_p_mesh->GetTriangleData();
                                    for (i32 i = n._child_index_or_first; i < n._child_index_or_first + n._count_or_flag; i++)
                                    {
                                        Vector3f v0 = TransformCoord(scale, tri_data[i].v0);
                                        Vector3f v1 = TransformCoord(scale, tri_data[i].v1);
                                        Vector3f v2 = TransformCoord(scale, tri_data[i].v2);
                                        draw_tri(v0, v1, v2, Colors::kCyan);
                                    }
                                }
                                else
                                {
                                    Render::Gizmo::DrawAABB(scale_aabb(nodes[n._child_index_or_first]._aabb, 0.9f), Colors::kGreen);
                                    Render::Gizmo::DrawAABB(scale_aabb(nodes[-n._count_or_flag]._aabb, 0.9f), Colors::kGreen);
                                }
                            }
                        }
                        else//triangle aabb
                        {
                            static i32 draw_idx = -1;
                            const auto &tri = sk_comp->_p_mesh->GetTriangleData();
                            const auto &tri_bounds = sk_comp->_p_mesh->GetTriangleBounds();
                            ImGui::SliderInt("Tri Index", &draw_idx, -1, (i32) tri.size() - 1);
                            if (draw_idx != -1)
                            {
                                Render::Gizmo::DrawAABB(tri_bounds[draw_idx], Colors::kGreen);
                                draw_tri(tri[draw_idx].v0, tri[draw_idx].v1, tri[draw_idx].v2, Colors::kCyan);
                            }
                        }
                    }
                }
            }
            if (ImGui::Button("ProfileWindow"))
                s_prifile_wd->_is_show = !s_prifile_wd->_is_show;
            if (ImGui::Button("Show3DTexture"))
            {
                VolumetricFog *fog;
                auto renderer = Render::RenderPipeline::Get().GetRenderer();
                for (auto &feature: renderer->GetFeatures())
                {
                    if (feature->GetType() == VolumetricFog::StaticType())
                    {
                        fog = dynamic_cast<VolumetricFog *>(feature);
                        break;
                    }
                }
                auto tex_view = MakeRef<Texture3DView>();
                tex_view->SetSource3D(fog->GetAccTexture());
                DockManager::Get().AddDock(tex_view);
            }
            if (ImGui::Button("Tracy Profiler"))
            {
                auto tarcy_path = Application::GetAiluRoot() + L"Tools/Tracy/tracy-profiler.exe";
                ProcessStartInfo psi(tarcy_path);
                auto p = ProcessFactory::Create();
                if (p)
                    p->Start(psi);
            }
            static Guid s_test_audio_guid = Guid::EmptyGuid();

            if (ImGui::Button("Test Audio"))
            {
                if (s_test_audio_guid == Guid::EmptyGuid())
                {
                    auto clip = MakeRef<AudioClip>();
                    clip->_runtime_path = "D:/BaiduNetdisk/module/BrowserEngine/resources/3.wav"; // 支持 wav/mp3/flac，先用 wav 最稳
                    clip->_load_mode = EAudioLoadMode::kMemory;

                    Asset *asset = ResourceMgr::Get().CreateAsset(L"project://Temp/EditorTestAudio.alasset", clip, true);
                    s_test_audio_guid = asset->GetGuid();
                }

                AudioPlayOptions options;
                options._bus = EAudioBus::kSfx;
                options._volume = 1.0f;
                Audio::Play(s_test_audio_guid, options);
            }
            
            ImGui::End();
            if (show)
                ImGui::ShowDemoWindow(&show);
            if (s_show_plot_demo)
                ImPlot::ShowDemoWindow((&s_show_plot_demo));
            static RenderTexture *s_last_scene_rt = nullptr;
            auto scene_rt = Render::RenderPipeline::Get().GetTarget(0);
            if (s_last_scene_rt != scene_rt)
            {
                //LOG_INFO("Scene RT Changed");
            }
            s_last_scene_rt = scene_rt;

            if (s_show_undo_view)
            {
                ImGui::Begin("UndoView", &s_show_undo_view);
                for (auto cmd: g_pCommandMgr->UndoViews())
                {
                    ImGui::Text("%s", cmd->ToString().c_str());
                }
                ImGui::End();
            }
            if (s_show_ui_reflector)
                ShowUIReflectorWindow(&s_show_ui_reflector);
            UI::UIManager::Get()->SetDebugReflectorVisible(s_show_ui_reflector);
            if (!s_show_ui_reflector)
                UI::UIManager::Get()->SetDebugHighlightTarget(nullptr);
            if (s_show_style_theme_editor)
                ShowStyleThemeEditorWindow(&s_show_style_theme_editor);
            if (s_show_resource_browser)
                ShowResourceBrowserWindow(&s_show_resource_browser);
            if (s_show_allocator_debug_panel)
                ShowAllocatorDebugPanelWindow(&s_show_allocator_debug_panel);
            s_prifile_wd->Show();
        }

        void EditorLayer::Begin()
        {
        }

        void EditorLayer::End()
        {
        }
        void EditorLayer::ProcessTransformGizmo()
        {
        }
    }// namespace Editor
}// namespace Ailu
