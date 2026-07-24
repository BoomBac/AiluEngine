#include "Widgets/ResourceBrowser.h"

#include "Ext/imgui/imgui.h"
#include "Framework/Common/ResourceMgr.h"
#include "Objects/Type.h"
#include "Render/2D/Sprite.h"
#include "Render/AssetPreviewGenerator.h"
#include "Render/ImGuiRenderer.h"

#include <algorithm>
#include <cstdio>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            // --- Reused property-drawing helpers (same pattern as StyleThemeEditor) ---

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
                char buffer[512];
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

            bool DrawColorField(const char *label, Color &value)
            {
                Color old_value = value;
                if (!ImGui::ColorEdit4(label, value.data, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                    return false;
                return old_value != value;
            }

            bool DrawEnumField(const PropertyInfo &prop, void *instance)
            {
                const auto *enum_type = static_cast<const Enum *>(prop.GetType());
                u32 &raw_value = prop.Get<u32>(instance);
                const String &preview = enum_type->GetNameByIndex(raw_value);
                bool changed = false;
                if (ImGui::BeginCombo(prop.Name().c_str(), preview.c_str()))
                {
                    for (const String *name : enum_type->GetEnumNames())
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

            bool DrawProperties(const Type *type, void *instance);

            bool DrawProperty(const PropertyInfo &prop, void *instance)
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
                if (prop_type->IsEnum())
                    return DrawEnumField(prop, instance);

                // For nested struct/object types, draw as a tree node
                if (prop_type->GetProperties().size() > 0)
                {
                    // Use Get<u8> to compute the address at the property offset
                    void *nested_instance = static_cast<void *>(&prop.Get<u8>(instance));
                    if (nested_instance == nullptr)
                    {
                        ImGui::TextDisabled("%s: %s (null)", prop.Name().c_str(), prop.TypeName().c_str());
                        return false;
                    }
                    bool changed = false;
                    if (ImGui::TreeNode(prop.Name().c_str()))
                    {
                        changed = DrawProperties(prop_type, nested_instance);
                        ImGui::TreePop();
                    }
                    return changed;
                }

                ImGui::TextDisabled("%s: %s", prop.Name().c_str(), prop.TypeName().c_str());
                return false;
            }

            bool DrawProperties(const Type *type, void *instance)
            {
                bool changed = false;
                for (auto *cur_type = type; cur_type != nullptr; cur_type = cur_type->BaseType())
                {
                    for (auto &prop : cur_type->GetProperties())
                    {
                        ImGui::PushID(prop.Name().c_str());
                        changed |= DrawProperty(prop, instance);
                        ImGui::PopID();
                    }
                }
                return changed;
            }

            // --- Resource list helpers ---

            /// A minimal filter query that searches asset name, path, and type name.
            struct AssetFilterQuery
            {
                String search_text;       ///< case-insensitive substring match
                const Type *type_filter = nullptr; ///< if non-null, only show assets of this type
            };

            bool PassesFilter(const Asset *asset, const AssetFilterQuery &query)
            {
                if (asset == nullptr)
                    return false;

                // Type filter
                if (query.type_filter != nullptr && asset->_asset_type != query.type_filter)
                    return false;

                // Text search
                if (!query.search_text.empty())
                {
                    const String lower_search = [&]()
                    {
                        String s = query.search_text;
                        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                        return s;
                    }();

                    const String asset_name = [&]()
                    {
                        String s = asset->Name();
                        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                        return s;
                    }();

                    const String asset_path = [&]()
                    {
                        String s = ToChar(asset->_asset_path);
                        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                        return s;
                    }();

                    if (asset_name.find(lower_search) == String::npos &&
                        asset_path.find(lower_search) == String::npos)
                        return false;
                }

                return true;
            }

            /// Collect all registered asset types for the type-filter combo.
            void CollectAssetTypes(std::vector<const Type *> &out_types)
            {
                auto &rm = ResourceMgr::Get();
                std::set<const Type *> unique_types;
                for (auto it = rm.Begin(); it != rm.End(); ++it)
                {
                    if (it->second != nullptr && it->second->_asset_type != nullptr)
                        unique_types.insert(it->second->_asset_type);
                }
                out_types.assign(unique_types.begin(), unique_types.end());
                // Sort by type name for stable UI
                std::sort(out_types.begin(), out_types.end(),
                          [](const Type *a, const Type *b)
                          { return a->Name() < b->Name(); });
            }
        } // namespace

        void ShowResourceBrowserWindow(bool *is_show)
        {
            if (is_show == nullptr || !*is_show)
                return;

            // ---- persistent UI state ----
            static char s_search_buffer[256] = {};
            static int s_selected_index = -1;
            static Asset *s_selected_asset = nullptr;
            static int s_type_filter_index = -1; // -1 = "All"
            static std::vector<const Type *> s_cached_types;
            static bool s_types_dirty = true;
            static HashMap<Render::Sprite *, Ref<Render::RenderTexture>> s_sprite_previews;

            if (!ImGui::Begin("Resource Browser", is_show))
            {
                ImGui::End();
                return;
            }

            auto &rm = ResourceMgr::Get();

            // --- Top bar: search + type filter ---
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
            ImGui::InputTextWithHint("##Search", "Filter by name or path...", s_search_buffer, sizeof(s_search_buffer));

            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);

            if (s_types_dirty)
            {
                s_cached_types.clear();
                CollectAssetTypes(s_cached_types);
                s_types_dirty = false;
            }

            if (ImGui::BeginCombo("##TypeFilter", s_type_filter_index < 0 ? "All Types" : s_cached_types[s_type_filter_index]->Name().c_str()))
            {
                if (ImGui::Selectable("All Types", s_type_filter_index < 0))
                {
                    s_type_filter_index = -1;
                    s_selected_index = -1;
                    s_selected_asset = nullptr;
                }
                if (s_type_filter_index < 0)
                    ImGui::SetItemDefaultFocus();

                for (int i = 0; i < (int)s_cached_types.size(); ++i)
                {
                    const bool selected = (i == s_type_filter_index);
                    if (ImGui::Selectable(s_cached_types[i]->Name().c_str(), selected))
                    {
                        s_type_filter_index = i;
                        s_selected_index = -1;
                        s_selected_asset = nullptr;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // Refresh button
            ImGui::SameLine();
            if (ImGui::Button("Refresh"))
            {
                s_types_dirty = true;
                s_selected_index = -1;
                s_selected_asset = nullptr;
            }

            ImGui::Separator();

            // --- Build filtered asset list ---
            AssetFilterQuery query;
            query.search_text = s_search_buffer;
            if (s_type_filter_index >= 0 && s_type_filter_index < (int)s_cached_types.size())
                query.type_filter = s_cached_types[s_type_filter_index];

            std::vector<Asset *> filtered_assets;
            for (auto it = rm.Begin(); it != rm.End(); ++it)
            {
                if (PassesFilter(it->second.get(), query))
                    filtered_assets.push_back(it->second.get());
            }

            // Sort alphabetically by name
            std::sort(filtered_assets.begin(), filtered_assets.end(),
                      [](const Asset *a, const Asset *b)
                      {
                          return a->Name() < b->Name();
                      });

            // --- Two-column layout ---
            const float left_width = ImGui::GetContentRegionAvail().x * 0.4f;
            const float footer_height = ImGui::GetFrameHeightWithSpacing();

            // Left panel: asset list
            if (ImGui::BeginChild("AssetList", ImVec2(left_width, -footer_height), ImGuiChildFlags_Border))
            {
                for (int i = 0; i < (int)filtered_assets.size(); ++i)
                {
                    Asset *asset = filtered_assets[i];

                    // Build display name: prefer _name, fall back to filename from _asset_path
                    auto display_name = asset->Name();
                    if (display_name.empty())
                        display_name = ToChar(PathUtils::GetFileName(asset->_asset_path));
                    if (display_name.empty())
                        display_name = "(Unnamed)";

                    // Separate display text from ID to avoid empty-ID assertion
                    char selectable_label[640];
                    snprintf(selectable_label, sizeof(selectable_label), "%s###asset_%d", display_name.c_str(), i);

                    const bool is_selected = (i == s_selected_index && s_selected_asset == asset);
                    if (ImGui::Selectable(selectable_label, is_selected))
                    {
                        s_selected_index = i;
                        s_selected_asset = asset;
                    }

                    // Right-click context menu
                    if (ImGui::BeginPopupContextItem())
                    {
                        if (ImGui::MenuItem("Copy Path"))
                        {
                            auto path_str = ToChar(asset->_asset_path);
                            ImGui::SetClipboardText(path_str.c_str());
                        }
                        if (asset->_p_obj != nullptr && ImGui::MenuItem("Copy Object Name"))
                        {
                            ImGui::SetClipboardText(asset->_p_obj->Name().c_str());
                        }
                        ImGui::EndPopup();
                    }

                    // Tooltip with full path
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        auto path_str = ToChar(asset->_asset_path);
                        ImGui::Text("Path: %s", path_str.c_str());
                        ImGui::Text("Type: %s", asset->_asset_type ? asset->_asset_type->Name().c_str() : "Unknown");
                        ImGui::Text("Domain: %d", (int)asset->_domain);
                        ImGui::EndTooltip();
                    }
                }

                if (filtered_assets.empty())
                    ImGui::TextDisabled("No resources match the filter.");
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // Right panel: detail view
            if (ImGui::BeginChild("AssetDetail", ImVec2(0.0f, -footer_height), ImGuiChildFlags_Border))
            {
                if (s_selected_asset != nullptr)
                {
                    // Header info
                    auto name_str = s_selected_asset->Name();
                    ImGui::TextUnformatted(name_str.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled(" | %s", s_selected_asset->_asset_type ? s_selected_asset->_asset_type->Name().c_str() : "Unknown");

                    ImGui::Separator();

                    // Path info
                    auto path_str = ToChar(s_selected_asset->_asset_path);
                    ImGui::TextDisabled("Path:");
                    ImGui::SameLine();
                    ImGui::TextUnformatted(path_str.c_str());

                    if (!s_selected_asset->_addi_info.empty())
                    {
                        auto addi_str = ToChar(s_selected_asset->_addi_info);
                        ImGui::TextDisabled("Addi Info:");
                        ImGui::SameLine();
                        ImGui::TextUnformatted(addi_str.c_str());
                    }

                    ImGui::TextDisabled("Domain:");
                    ImGui::SameLine();
                    ImGui::Text("%d", (int)s_selected_asset->_domain);

                    ImGui::TextDisabled("GUID:");
                    ImGui::SameLine();
                    ImGui::TextUnformatted(s_selected_asset->GetGuid().ToString().c_str());

                    ImGui::TextDisabled("Object ID:");
                    ImGui::SameLine();
                    if (s_selected_asset->_p_obj)
                        ImGui::Text("%u", s_selected_asset->_p_obj->ID());
                    else
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "(not loaded)");

                    // --- Sprite / Texture preview ---
                    {
                        using Render::Sprite;
                        using Render::Texture2D;
                        using Render::RenderTexture;

                        Sprite *sprite = nullptr;
                        RenderTexture *sprite_preview_rt = nullptr;
                        Texture2D *preview_tex = nullptr;

                        if (s_selected_asset->_asset_type == StaticClass<Sprite>())
                        {
                            // Auto-load sprite if not loaded
                            if (s_selected_asset->_p_obj == nullptr)
                                s_selected_asset->_p_obj = ResourceMgr::Get().Load<Sprite>(s_selected_asset->_asset_path);

                            sprite = s_selected_asset->As<Sprite>();
                            if (sprite)
                            {
                                // Generate preview via full rendering pipeline (matches actual render)
                                if (!s_sprite_previews.contains(sprite))
                                {
                                    Ref<RenderTexture> preview_rt;
                                    AssetPreviewGenerator::GeneratorSpriteSnapshot(256u, 256u, sprite, preview_rt);
                                    if (preview_rt)
                                        s_sprite_previews[sprite] = preview_rt;
                                }
                                sprite_preview_rt = s_sprite_previews.contains(sprite) ? s_sprite_previews[sprite].get() : nullptr;
                            }
                        }
                        else if (s_selected_asset->_p_obj)
                        {
                            // Fallback: if the object itself is a Texture2D, preview it directly
                            preview_tex = dynamic_cast<Texture2D *>(s_selected_asset->_p_obj.get());
                        }

                        if (sprite_preview_rt || preview_tex)
                        {
                            bool is_sprite = sprite_preview_rt != nullptr;
                            auto *display_tex = is_sprite ? static_cast<Texture *>(sprite_preview_rt) : static_cast<Texture *>(preview_tex);
                            ImGui::SeparatorText(is_sprite ? "Sprite Preview" : "Texture Preview");

                            const f32 available_w = ImGui::GetContentRegionAvail().x;
                            const f32 preview_sz = std::min(available_w * 0.7f, 256.0f);
                            const f32 tex_aspect = display_tex->Width() > 0 && display_tex->Height() > 0
                                                       ? (f32)display_tex->Width() / (f32)display_tex->Height()
                                                       : 1.0f;
                            const f32 preview_h = preview_sz / tex_aspect;

                            // Ensure the render texture is in SRV state for ImGui sampling
                            if (sprite_preview_rt)
                                Render::ImGuiRenderer::Get().RecordImguiUsedTexture(sprite_preview_rt);

                            auto tex_handle = (ImTextureID)(uintptr_t)display_tex->GetNativeTextureHandle();
                            if (tex_handle)
                            {
                                ImGui::Image(tex_handle, ImVec2(preview_sz, preview_h));

                                // Sprite metadata
                                if (sprite)
                                {
                                    ImGui::TextDisabled("Size: %.1f x %.1f", sprite->_size.x, sprite->_size.y);
                                    ImGui::TextDisabled("Pivot: (%.2f, %.2f)", sprite->_pivot.x, sprite->_pivot.y);
                                    if (sprite->_border != Vector4f::kZero)
                                        ImGui::TextDisabled("Border: L%.0f R%.0f B%.0f T%.0f",
                                                            sprite->_border.x, sprite->_border.y,
                                                            sprite->_border.z, sprite->_border.w);
                                }
                                if (preview_tex)
                                    ImGui::TextDisabled("Texture: %dx%d", preview_tex->Width(), preview_tex->Height());
                            }
                        }
                    }

                    // Dependencies
                    if (!s_selected_asset->_dependencies.empty())
                    {
                        ImGui::SeparatorText("Dependencies");
                        for (size_t d = 0; d < s_selected_asset->_dependencies.size(); ++d)
                        {
                            auto &dep = s_selected_asset->_dependencies[d];
                            ImGui::BulletText("[%zu] %s (type:%d)", d,
                                              dep._guid.ToString().c_str(), (int)dep._type);
                        }
                    }

                    ImGui::SeparatorText("Object Properties");

                    // Object property inspection
                    if (s_selected_asset->_p_obj != nullptr)
                    {
                        const Type *obj_type = s_selected_asset->_p_obj->GetType();
                        if (obj_type != nullptr)
                        {
                            DrawProperties(obj_type, s_selected_asset->_p_obj.get());
                        }
                        else
                        {
                            ImGui::TextDisabled("Object has no registered type.");
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("Object not loaded. Use ResourceMgr::Load() to load it first.");
                    }
                }
                else
                {
                    ImGui::TextDisabled("Select a resource from the list to view its properties.");
                }
            }
            ImGui::EndChild();

            // --- Footer status bar ---
            char status[128];
            snprintf(status, sizeof(status), "%d assets registered | %d visible",
                     (int)rm.AssetNum(), (int)filtered_assets.size());
            ImGui::TextDisabled("%s", status);

            ImGui::End();
        }
    } // namespace Editor
} // namespace Ailu
