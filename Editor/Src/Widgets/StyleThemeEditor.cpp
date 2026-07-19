#include "Widgets/StyleThemeEditor.h"

#include "EditorApp.h"
#include "Ext/imgui/imgui.h"
#include "Objects/JsonArchive.h"
#include "Objects/Type.h"
#include "UI/Style/UITheme.h"
#include "UI/UIElement.h"
#include "UI/UIFramework.h"
#include "UI/Widget.h"

#include <algorithm>
#include <filesystem>

namespace Ailu
{
    namespace Editor
    {
        extern UI::UITheme g_editor_ui_theme;

        namespace
        {
            Path DefaultThemePath()
            {
                return Path(EditorApp::GetEditorRootPath()) / L"Res/UI/UITheme_Dark.json";
            }

            void CopyPathToBuffer(const Path &path, char (&buffer)[512])
            {
                const String text = path.string();
                const size_t len = (std::min)(text.size(), sizeof(buffer) - 1u);
                memcpy(buffer, text.c_str(), len);
                buffer[len] = '\0';
            }

            void InvalidateAllWidgetStyles()
            {
                UI::UIManager *ui_mgr = UI::UIManager::Get();
                if (ui_mgr == nullptr)
                    return;
                ui_mgr->SetTheme(&g_editor_ui_theme);
                for (const auto &widget: ui_mgr->_widgets)
                {
                    if (widget != nullptr && widget->Root() != nullptr)
                        widget->Root()->InvalidateStyle(UI::EStyleInvalidation::kLayoutAndPaint);
                }
            }

            void ApplyThemeChange()
            {
                g_editor_ui_theme.BumpRevision();
                InvalidateAllWidgetStyles();
            }

            bool LoadThemeFromFile(const Path &path)
            {
                JsonArchive ar;
                ar.Load(path);
                if (!ar.IsLoaded())
                    return false;

                Type *type = UI::UITheme::StaticType();
                for (auto &prop: type->GetProperties())
                    prop.Deserialize(&g_editor_ui_theme, ar);
                g_editor_ui_theme.PostDeserialize();
                InvalidateAllWidgetStyles();
                return true;
            }

            bool SaveThemeToFile(const Path &path)
            {
                JsonArchive ar;
                Type *type = UI::UITheme::StaticType();
                for (auto &prop: type->GetProperties())
                    prop.Serialize(&g_editor_ui_theme, ar);
                ar.Save(path);
                return std::filesystem::exists(path);
            }

            bool DrawColorField(const char *label, Color &value)
            {
                Color old_value = value;
                if (!ImGui::ColorEdit4(label, value.data, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                    return false;
                return old_value != value;
            }

            bool DrawPaddingField(const char *label, UI::Padding &value)
            {
                Vector4f old_value(value._l, value._t, value._r, value._b);
                Vector4f new_value = old_value;
                if (!ImGui::InputFloat4(label, new_value.data))
                    return false;
                value = UI::Padding(new_value);
                return old_value != new_value;
            }

            bool DrawVector2Field(const char *label, Vector2f &value)
            {
                Vector2f old_value = value;
                if (!ImGui::InputFloat2(label, value.data))
                    return false;
                return old_value != value;
            }

            bool DrawVector4Field(const char *label, Vector4f &value)
            {
                Vector4f old_value = value;
                if (!ImGui::InputFloat4(label, value.data))
                    return false;
                return old_value != value;
            }

            bool DrawFloatField(const char *label, f32 &value)
            {
                f32 old_value = value;
                if (!ImGui::InputFloat(label, &value))
                    return false;
                return old_value != value;
            }

            bool DrawU32Field(const char *label, u32 &value)
            {
                u32 old_value = value;
                if (!ImGui::InputScalar(label, ImGuiDataType_U32, &value))
                    return false;
                return old_value != value;
            }

            bool DrawStringField(const char *label, String &value)
            {
                char buffer[256];
                const size_t len = (std::min)(value.size(), sizeof(buffer) - 1u);
                memcpy(buffer, value.c_str(), len);
                buffer[len] = '\0';
                if (!ImGui::InputText(label, buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
                    return false;
                if (value == buffer)
                    return false;
                value = buffer;
                return true;
            }

            bool DrawEnumField(PropertyInfo &prop, void *instance)
            {
                const auto *enum_type = static_cast<const Enum *>(prop.GetType());
                u32 &raw_value = prop.Get<u32>(instance);
                const String &preview = enum_type->GetNameByIndex(raw_value);
                bool changed = false;
                if (ImGui::BeginCombo(prop.Name().c_str(), preview.c_str()))
                {
                    for (const String *name: enum_type->GetEnumNames())
                    {
                        if (name == nullptr)
                            continue;
                        const u32 enum_value = static_cast<u32>(enum_type->GetIndexByName(*name));
                        const bool selected = raw_value == enum_value;
                        if (ImGui::Selectable(name->c_str(), selected))
                        {
                            raw_value = enum_value;
                            changed = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                return changed;
            }

            bool DrawProperties(Type *type, void *instance);

            template<typename T>
            bool DrawNestedProperty(PropertyInfo &prop, void *instance)
            {
                T &value = prop.Get<T>(instance);
                bool changed = false;
                if (ImGui::TreeNode(prop.Name().c_str()))
                {
                    changed = DrawProperties(StaticClass<T>(), &value);
                    ImGui::TreePop();
                }
                return changed;
            }

            bool DrawProperty(PropertyInfo &prop, void *instance)
            {
                if (prop.IsConst())
                    return false;

                const Type *prop_type = prop.GetType();
                if (prop_type == nullptr)
                    return false;

                if (prop_type == StaticClass<f32>())
                    return DrawFloatField(prop.Name().c_str(), prop.Get<f32>(instance));
                if (prop_type == StaticClass<u32>())
                    return DrawU32Field(prop.Name().c_str(), prop.Get<u32>(instance));
                if (prop_type == StaticClass<String>())
                    return DrawStringField(prop.Name().c_str(), prop.Get<String>(instance));
                if (prop_type == StaticClass<Vector2f>())
                    return DrawVector2Field(prop.Name().c_str(), prop.Get<Vector2f>(instance));
                if (prop.TypeName() == "Color")
                    return DrawColorField(prop.Name().c_str(), prop.Get<Color>(instance));
                if (prop_type == StaticClass<Vector4f>())
                {
                    if (prop.MetaInfo().GetBool("IsColor"))
                        return DrawColorField(prop.Name().c_str(), prop.Get<Color>(instance));
                    return DrawVector4Field(prop.Name().c_str(), prop.Get<Vector4f>(instance));
                }
                if (prop_type == StaticClass<UI::Padding>())
                    return DrawPaddingField(prop.Name().c_str(), prop.Get<UI::Padding>(instance));
                if (prop_type->IsEnum())
                    return DrawEnumField(prop, instance);

                if (prop_type == StaticClass<UI::UIBrush>())
                    return DrawNestedProperty<UI::UIBrush>(prop, instance);
                if (prop_type == StaticClass<UI::UIControlVisual>())
                    return DrawNestedProperty<UI::UIControlVisual>(prop, instance);
                if (prop_type == StaticClass<UI::UIColorTokens>())
                    return DrawNestedProperty<UI::UIColorTokens>(prop, instance);
                if (prop_type == StaticClass<UI::UISpacingTokens>())
                    return DrawNestedProperty<UI::UISpacingTokens>(prop, instance);
                if (prop_type == StaticClass<UI::UITypographyTokens>())
                    return DrawNestedProperty<UI::UITypographyTokens>(prop, instance);
                if (prop_type == StaticClass<UI::UIButtonStyle>())
                    return DrawNestedProperty<UI::UIButtonStyle>(prop, instance);
                if (prop_type == StaticClass<UI::UISliderStyle>())
                    return DrawNestedProperty<UI::UISliderStyle>(prop, instance);
                if (prop_type == StaticClass<UI::UICheckBoxStyle>())
                    return DrawNestedProperty<UI::UICheckBoxStyle>(prop, instance);
                if (prop_type == StaticClass<UI::UIInputStyle>())
                    return DrawNestedProperty<UI::UIInputStyle>(prop, instance);
                if (prop_type == StaticClass<UI::UIScrollBarStyle>())
                    return DrawNestedProperty<UI::UIScrollBarStyle>(prop, instance);
                if (prop_type == StaticClass<UI::UIScrollViewStyle>())
                    return DrawNestedProperty<UI::UIScrollViewStyle>(prop, instance);

                ImGui::TextDisabled("%s: %s", prop.Name().c_str(), prop.TypeName().c_str());
                return false;
            }

            bool DrawProperties(Type *type, void *instance)
            {
                bool changed = false;
                for (Type *cur_type = type; cur_type != nullptr; cur_type = cur_type->BaseType())
                {
                    for (auto &prop: cur_type->GetProperties())
                    {
                        ImGui::PushID(prop.Name().c_str());
                        changed |= DrawProperty(prop, instance);
                        ImGui::PopID();
                    }
                }
                return changed;
            }
        }

        void ShowStyleThemeEditorWindow(bool *is_show)
        {
            if (is_show == nullptr || !*is_show)
                return;

            static Path s_theme_path = DefaultThemePath();
            static char s_path_buffer[512] = {};
            static bool s_path_initialized = false;
            static String s_status;
            if (!s_path_initialized)
            {
                CopyPathToBuffer(s_theme_path, s_path_buffer);
                s_path_initialized = true;
            }

            if (!ImGui::Begin("Style Theme Editor", is_show))
            {
                ImGui::End();
                return;
            }

            if (ImGui::InputText("Theme Json", s_path_buffer, sizeof(s_path_buffer), ImGuiInputTextFlags_EnterReturnsTrue))
                s_theme_path = Path(s_path_buffer);

            if (ImGui::Button("Dark"))
            {
                s_theme_path = Path(EditorApp::GetEditorRootPath()) / L"Res/UI/UITheme_Dark.json";
                CopyPathToBuffer(s_theme_path, s_path_buffer);
                s_status = LoadThemeFromFile(s_theme_path) ? "loaded" : "load failed";
            }
            ImGui::SameLine();
            if (ImGui::Button("Light"))
            {
                s_theme_path = Path(EditorApp::GetEditorRootPath()) / L"Res/UI/UITheme_Light.json";
                CopyPathToBuffer(s_theme_path, s_path_buffer);
                s_status = LoadThemeFromFile(s_theme_path) ? "loaded" : "load failed";
            }
            ImGui::SameLine();
            if (ImGui::Button("Load"))
            {
                s_theme_path = Path(s_path_buffer);
                s_status = LoadThemeFromFile(s_theme_path) ? "loaded" : "load failed";
            }
            ImGui::SameLine();
            if (ImGui::Button("Save"))
            {
                s_theme_path = Path(s_path_buffer);
                s_status = SaveThemeToFile(s_theme_path) ? "saved" : "save failed";
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply"))
            {
                ApplyThemeChange();
                s_status = "applied";
            }
            if (!s_status.empty())
                ImGui::SameLine(), ImGui::TextUnformatted(s_status.c_str());

            ImGui::Separator();
            if (DrawProperties(UI::UITheme::StaticType(), &g_editor_ui_theme))
            {
                ApplyThemeChange();
                s_status = "modified";
            }

            ImGui::End();
        }
    }// namespace Editor
}// namespace Ailu
