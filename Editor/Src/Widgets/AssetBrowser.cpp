#include "Widgets/AssetBrowser.h"
#include "Common/EditorPopup.h"
#include "Editors/SpriteAssetEditor.h"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/ResourceMgr.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"
#include "UI/UIRenderer.h"
#include "UI/TextRenderer.h"
#include "UI/DragDrop.h"
#include "UI/TreeView.h"
#include "Objects/JsonArchive.h"
#include "Project/ProjectManager.h"
#include "Render/AssetPreviewGenerator.h"
#include "Framework/Common/Input.h"
#include "Dock/DockManager.h"
#include "Render/2D/Sprite.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <unordered_map>

namespace Ailu
{
    namespace Editor
    {
        using namespace UI;
        using Render::Sprite;
        namespace
        {
            enum class EImportPopupType : u8
            {
                kDirect,
                kTexture,
                kMesh
            };

            enum class EAssetBrowserRoot : u8
            {
                kProject,
                kEngine
            };

            struct AssetBrowserRootDesc
            {
                EAssetBrowserRoot _root = EAssetBrowserRoot::kEngine;
                EAssetDomain _domain = EAssetDomain::kEngine;
                String _label;
                fs::path _sys_path;
            };

            constexpr f32 kIconLabelHeight = 22.0f;
            constexpr f32 kIconCellMinWidth = 88.0f;
            constexpr f32 kIconCellGap = 8.0f;
            constexpr f32 kIconCellPadding = 4.0f;
            constexpr f32 kListViewIconThreshold = 60.0f;
            constexpr f32 kListRowHeight = 24.0f;
            constexpr f32 kListIconSize = 18.0f;
            constexpr f32 kListTextLeftPadding = 6.0f;

            bool IsListView(f32 icon_size)
            {
                return icon_size < kListViewIconThreshold;
            }

            String FitTextToWidth(const String &text, f32 max_width, f32 font_size)
            {
                if (text.empty() || max_width <= 0.0f)
                    return {};
                if (UI::TextRenderer::CalculateTextSize(text, font_size).x <= max_width)
                    return text;

                constexpr const char *kEllipsis = "...";
                const f32 ellipsis_width = UI::TextRenderer::CalculateTextSize(kEllipsis, font_size).x;
                if (ellipsis_width > max_width)
                    return {};

                u64 left = 0u;
                u64 right = text.size();
                while (left < right)
                {
                    const u64 mid = (left + right + 1u) / 2u;
                    const String candidate = text.substr(0u, mid) + kEllipsis;
                    if (UI::TextRenderer::CalculateTextSize(candidate, font_size).x <= max_width)
                        left = mid;
                    else
                        right = mid - 1u;
                }
                return text.substr(0u, left) + kEllipsis;
            }

            WString NormalizeSysPath(const fs::path &path)
            {
                return PathUtils::NormalizePathWithoutTrailingSlash(PathUtils::FormatFilePath(path.wstring()));
            }

            bool IsSameOrChildPath(const fs::path &path, const fs::path &root)
            {
                const WString normalized_path = NormalizeSysPath(path);
                const WString normalized_root = NormalizeSysPath(root);
                if (normalized_path == normalized_root)
                    return true;

                const WString root_prefix = PathUtils::NormalizeDirectoryPath(normalized_root);
                return normalized_path.compare(0, root_prefix.size(), root_prefix) == 0;
            }

            Vector<AssetBrowserRootDesc> GetAssetBrowserRoots()
            {
                Vector<AssetBrowserRootDesc> roots;
                if (ProjectManager::Get().HasOpenedProject())
                {
                    const WString project_asset_root = ProjectManager::Get().CurrentProject().AssetDirectory();
                    if (!project_asset_root.empty())
                    {
                        roots.push_back({EAssetBrowserRoot::kProject, EAssetDomain::kProject, "ProjAssets",
                                         fs::path(project_asset_root)});
                    }
                }

                roots.push_back({EAssetBrowserRoot::kEngine, EAssetDomain::kEngine, "EngineAssets",
                                 fs::path(ResourceMgr::Get().EngineResRootPath())});
                return roots;
            }

            std::optional<AssetBrowserRootDesc> FindRootForPath(const fs::path &path)
            {
                for (const auto &root: GetAssetBrowserRoots())
                {
                    if (IsSameOrChildPath(path, root._sys_path))
                        return root;
                }
                return std::nullopt;
            }

            EAssetDomain GetDomainForPath(const fs::path &path)
            {
                if (auto root = FindRootForPath(path); root.has_value())
                    return root->_domain;
                return EAssetDomain::kEngine;
            }

            WString GetRelativeAssetDirectory(const fs::path &path)
            {
                if (auto root = FindRootForPath(path); root.has_value())
                {
                    const WString normalized_path = NormalizeSysPath(path);
                    const WString normalized_root = NormalizeSysPath(root->_sys_path);
                    if (normalized_path == normalized_root)
                        return ResourceMgr::kPathScheme[static_cast<int>(root->_domain)];

                    std::error_code error;
                    fs::path relative_path = fs::relative(fs::path(normalized_path), fs::path(normalized_root), error);
                    if (!error)
                        return ResourceMgr::NormalizeAssetPath(relative_path.wstring(), root->_domain);
                }
                return ResourceMgr::NormalizeAssetPath(path.wstring(), GetDomainForPath(path));
            }

            WString FormatLogicalAssetPath(WString path)
            {
                for (auto &ch: path)
                {
                    if (ch == L'\\')
                        ch = L'/';
                }
                return path;
            }

            WString NormalizeLogicalPathWithoutTrailingSlash(const WString &path)
            {
                WString normalized = FormatLogicalAssetPath(path);
                size_t min_size = 0u;
                for (const auto &scheme: ResourceMgr::kPathScheme)
                {
                    if (normalized.starts_with(scheme))
                    {
                        min_size = scheme.size();
                        break;
                    }
                }
                while (normalized.size() > min_size && normalized.back() == L'/')
                    normalized.pop_back();
                return normalized;
            }

            WString NormalizeLogicalDirectoryPath(const WString &path)
            {
                WString normalized = FormatLogicalAssetPath(path);
                if (!normalized.empty() && normalized.back() != L'/')
                    normalized.push_back(L'/');
                return normalized;
            }

            WString ExtractLogicalAssetDirectory(const WString &path)
            {
                WString normalized = FormatLogicalAssetPath(path);
                const size_t slash_pos = normalized.find_last_of(L'/');
                if (slash_pos == WString::npos)
                    return L"";
                for (const auto &scheme: ResourceMgr::kPathScheme)
                {
                    if (normalized.starts_with(scheme) && slash_pos < scheme.size())
                        return scheme;
                }
                return normalized.substr(0, slash_pos);
            }

            WString StripLogicalAssetPathScheme(const WString &path)
            {
                WString normalized = FormatLogicalAssetPath(path);
                for (const auto &scheme: ResourceMgr::kPathScheme)
                {
                    if (normalized.starts_with(scheme))
                        return normalized.substr(scheme.size());
                }
                return normalized;
            }

            String ToLowerCopy(String value)
            {
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });
                return value;
            }

            bool ContainsSearchText(const String &value, const String &search_text)
            {
                return search_text.empty() || ToLowerCopy(value).find(search_text) != String::npos;
            }

            bool IsAssetInDirectory(const WString &asset_directory, const WString &directory, bool recursive)
            {
                const WString normalized_asset_directory = NormalizeLogicalPathWithoutTrailingSlash(asset_directory);
                const WString normalized_directory = NormalizeLogicalPathWithoutTrailingSlash(directory);
                if (normalized_asset_directory == normalized_directory)
                    return true;
                if (!recursive)
                    return false;

                const WString directory_prefix = normalized_directory.empty() ? WString{} : NormalizeLogicalDirectoryPath(normalized_directory);
                return directory_prefix.empty() || normalized_asset_directory.compare(0, directory_prefix.size(), directory_prefix) == 0;
            }

            String TrimNameCopy(const String &value)
            {
                size_t begin = 0;
                size_t end = value.size();
                while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])))
                    ++begin;
                while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
                    --end;
                return value.substr(begin, end - begin);
            }

            bool HasInvalidFileNameChars(const String &value)
            {
                static const String kInvalidChars = "\\/:*?\"<>|";
                return value.find_first_of(kInvalidChars) != String::npos;
            }

            std::optional<String> ValidateEntryName(const String &value)
            {
                if (value.empty())
                    return String("Name cannot be empty.");
                if (value == "." || value == "..")
                    return String("Name is reserved.");
                if (HasInvalidFileNameChars(value))
                    return String("Name contains invalid characters.");
                return std::nullopt;
            }

            Ref<Render::Shader> EnsureDefaultMaterialShader()
            {
                auto shader = Render::Shader::s_p_defered_standart_lit.lock();
                if (!shader)
                {
                    shader = ResourceMgr::Get().Load<Render::Shader>(L"Shaders/hlsl/defered_standard_lit.alasset");
                    Render::Shader::s_p_defered_standart_lit = shader;
                }
                return shader;
            }

            Vector<Ref<Render::Shader>> CollectMaterialShaders()
            {
                Vector<Ref<Render::Shader>> shaders;
                auto default_shader = EnsureDefaultMaterialShader();
                if (default_shader)
                    shaders.push_back(default_shader);

                for (auto it = ResourceMgr::Get().ResourceBegin<Render::Shader>(); it != ResourceMgr::Get().ResourceEnd<Render::Shader>(); ++it)
                {
                    auto shader = ResourceMgr::IterToRefPtr<Render::Shader>(it);
                    if (!shader)
                        continue;
                    const bool already_added = std::any_of(shaders.begin(), shaders.end(), [shader](const Ref<Render::Shader> &item)
                    {
                        return item.get() == shader.get();
                    });
                    if (!already_added)
                        shaders.push_back(shader);
                }
                return shaders;
            }

            WString AppendChildAssetPath(const WString &directory_asset_path, const WString &file_name)
            {
                if (directory_asset_path.empty())
                    return file_name;
                return NormalizeLogicalDirectoryPath(directory_asset_path) + file_name;
            }

            bool RewriteAssetHeaderName(const WString &sys_path, const String &new_name)
            {
                WString content;
                if (!FileManager::ReadFile(sys_path, content))
                    return false;

                auto lines = StringUtils::Split(content, L"\n");
                if (lines.size() < 3u)
                    return false;

                lines[2] = std::format(L"name: {}", ToWChar(new_name.c_str()));
                std::wstringstream buffer;
                for (size_t i = 0; i < lines.size(); ++i)
                {
                    if (!lines[i].empty() && lines[i].back() == L'\r')
                        lines[i].pop_back();
                    buffer << lines[i];
                    if (i + 1u < lines.size())
                        buffer << L'\n';
                }
                return FileManager::WriteFile(sys_path, false, buffer.str());
            }

            EImportPopupType GetImportPopupType(const WString &sys_path)
            {
                const String ext = fs::path(sys_path).extension().string();
                if (ext == ".fbx" || ext == ".FBX")
                    return EImportPopupType::kMesh;
                if (ResourceMgr::kHDRImageExt.contains(ext) || ResourceMgr::kLDRImageExt.contains(ext))
                    return EImportPopupType::kTexture;
                return EImportPopupType::kDirect;
            }
        }// namespace

        class AssetBrowser::DirectoryTreeDataSource final : public UI::ITreeViewDataSource
        {
        public:
            void Rebuild()
            {
                _nodes.clear();
                _roots.clear();
                _path_to_id.clear();
                _next_id = 1u;

                for (const auto &root: GetAssetBrowserRoots())
                {
                    if (root._sys_path.empty() || !fs::exists(root._sys_path))
                        continue;

                    const UI::TreeItemId root_id = AddNode(root._label, root._sys_path, UI::kInvalidTreeItemId, root._domain);
                    _roots.push_back(root_id);
                    AddDirectoryChildren(root_id, root._sys_path, root._domain);
                }
            }

            Vector<UI::TreeItemId> GetRootItems() const override
            {
                return _roots;
            }

            Vector<UI::TreeItemId> GetChildren(UI::TreeItemId parent) const override
            {
                if (const auto it = _nodes.find(parent); it != _nodes.end())
                    return it->second._children;
                return {};
            }

            UI::TreeItemId GetParent(UI::TreeItemId item) const override
            {
                if (const auto it = _nodes.find(item); it != _nodes.end())
                    return it->second._parent;
                return UI::kInvalidTreeItemId;
            }

            UI::TreeItemPresentation GetPresentation(UI::TreeItemId item) const override
            {
                UI::TreeItemPresentation presentation;
                if (const auto it = _nodes.find(item); it != _nodes.end())
                {
                    presentation._label = it->second._label;
                    presentation._selectable = true;
                    presentation._drop_target = true;
                }
                return presentation;
            }

            bool IsValid(UI::TreeItemId item) const override
            {
                return _nodes.contains(item);
            }

            fs::path GetPath(UI::TreeItemId item) const
            {
                if (const auto it = _nodes.find(item); it != _nodes.end())
                    return it->second._path;
                return {};
            }

            UI::TreeItemId FindItemByPath(const fs::path &path) const
            {
                const WString normalized_path = NormalizeSysPath(path);
                if (const auto it = _path_to_id.find(normalized_path); it != _path_to_id.end())
                    return it->second;
                return UI::kInvalidTreeItemId;
            }

        private:
            struct DirectoryNode
            {
                UI::TreeItemId _id = UI::kInvalidTreeItemId;
                UI::TreeItemId _parent = UI::kInvalidTreeItemId;
                EAssetDomain _domain = EAssetDomain::kEngine;
                String _label;
                fs::path _path;
                Vector<UI::TreeItemId> _children;
            };

            UI::TreeItemId AddNode(const String &label, const fs::path &path, UI::TreeItemId parent, EAssetDomain domain)
            {
                const UI::TreeItemId id = _next_id++;
                DirectoryNode node;
                node._id = id;
                node._parent = parent;
                node._domain = domain;
                node._label = label;
                node._path = path;
                _path_to_id[NormalizeSysPath(path)] = id;
                _nodes[id] = std::move(node);
                if (parent != UI::kInvalidTreeItemId)
                    _nodes[parent]._children.push_back(id);
                return id;
            }

            void AddDirectoryChildren(UI::TreeItemId parent, const fs::path &path, EAssetDomain domain)
            {
                Vector<fs::directory_entry> directories;
                std::error_code error;
                for (fs::directory_iterator it(path, error); !error && it != fs::directory_iterator(); it.increment(error))
                {
                    if (it->is_directory(error))
                        directories.push_back(*it);
                }

                std::sort(directories.begin(), directories.end(), [](const fs::directory_entry &lhs, const fs::directory_entry &rhs)
                {
                    return lhs.path().filename().wstring() < rhs.path().filename().wstring();
                });

                for (const auto &directory: directories)
                {
                    const UI::TreeItemId child = AddNode(directory.path().filename().string(), directory.path(), parent, domain);
                    AddDirectoryChildren(child, directory.path(), domain);
                }
            }

            UI::TreeItemId _next_id = 1u;
            Vector<UI::TreeItemId> _roots;
            std::unordered_map<UI::TreeItemId, DirectoryNode> _nodes;
            std::unordered_map<WString, UI::TreeItemId> _path_to_id;
        };

        AssetBrowser::AssetBrowser() : DockWindow("Asset Browser")
        {
            _sv = _content_root->AddChild<UI::SplitView>();
            _sv->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _sv->SlotPadding() = UI::Padding(_content_root->Thickness());
            _sv->InvalidateLayout();
            auto left = _sv->AddChild<UI::VerticalBox>();
            left->SlotPadding() = UI::Padding(2.0f);
            left->InvalidateLayout();
            _directory_tree_data_source = new DirectoryTreeDataSource();
            _directory_tree = left->AddChild<UI::TreeView>();
            _directory_tree->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill).CrossAlignment(UI::EAlignment::kFill);
            _directory_tree->SetDataSource(_directory_tree_data_source);
            _directory_tree->_row_height = 20.0f;
            _directory_tree->_on_selection_changed += [this](UI::TreeItemId item)
            {
                if (item == UI::kInvalidTreeItemId || _directory_tree_data_source == nullptr)
                    return;
                NavigateToPath(_directory_tree_data_source->GetPath(item));
            };

            _right = _sv->AddChild<UI::VerticalBox>();
            _right->SlotPadding() = UI::Padding(2.0f, 2.0f, 0.0f, 0.0f);
            _right->InvalidateLayout();
            _on_size_change += [this](Vector2f new_size)
            {
                auto t = _content_root->Thickness();
                _sv->GetSlot()->Size({new_size.x, new_size.y - kTitleBarHeight});
            };
            auto roots = GetAssetBrowserRoots();
            _current_path = roots.empty() ? fs::path(ResourceMgr::Get().EngineResRootPath()) : roots.front()._sys_path;
            auto hb = _right->AddChild<UI::HorizontalBox>();
            hb->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).CrossAlignment(UI::EAlignment::kFill);
            auto back_btn = hb->AddChild<UI::Button>();
            back_btn->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed).Size({22.0f, 22.0f});
            back_btn->OnMouseClick() += [&](UI::UIEvent& e) 
            {
                const fs::path parent_path = _current_path.parent_path();
                if (auto root = FindRootForPath(_current_path); root.has_value() && IsSameOrChildPath(parent_path, root->_sys_path))
                    NavigateToPath(parent_path);
            };
            back_btn->SetText("<");
            _path_bar = hb->AddChild<UI::HorizontalBox>();
            _path_bar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({0.0f, 22.0f}).FillRate(1.0f);
            _path_title = _path_bar->AddChild<UI::Text>("Current Path");
            _path_title->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kFixed).Size({120.0f, 22.0f});
            _path_title->_horizontal_align = EAlignment::kLeft;
            auto search_label = hb->AddChild<UI::Text>("Search");
            search_label->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed).Size({48.0f, 22.0f});
            search_label->_horizontal_align = EAlignment::kCenter;
            _search_input = hb->AddChild<UI::InputBlock>();
            _search_input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed).Size({180.0f, 22.0f});
            _search_input->_on_content_changed += [this](String value)
            {
                _search_text = ToLowerCopy(std::move(value));
                _is_dirty = true;
            };
            _search_input->SetContent("", false);
            _icon_area = _right->AddChild<UI::ScrollView>();
            _icon_area->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill).CrossAlignment(UI::EAlignment::kFill);
            auto slider = _right->AddChild<UI::Slider>();
            slider->_range = {50.0f, 200.0f};
            slider->SetValue(64.0f);
            _is_list_view = IsListView(_icon_size);
            _icon_content = _icon_area->AddChild<UI::Canvas>();
            _icon_content->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kAuto);
            const auto blank_context_handler = [this](UI::UIEvent &e)
            {
                if (e._key_code != EKey::kRBUTTON)
                    return;
                if (e._target != _icon_area && e._target != _icon_content)
                    return;
                ShowBlankAreaContextMenu(e._mouse_position);
                e._is_handled = true;
            };
            _icon_area->OnMouseDown() += blank_context_handler;
            _icon_content->OnMouseDown() += blank_context_handler;
            slider->_on_value_change += [&](f32 value)
            {
                const bool was_list_view = _is_list_view;
                _icon_size = value;
                _is_list_view = IsListView(_icon_size);
                if (_is_list_view != was_list_view)
                    _is_dirty = true;
                else
                    _is_icon_layout_dirty = true;
            };
            DropHandler handler;
            handler._can_drop = [](const DragPayload &payload) -> bool
            {
                return payload._type == EDragType::kFile;
            };
            handler._on_drop = [this](const DragPayload &payload, f32 x, f32 y)
            {
                LOG_INFO("{} drop", StaticEnum<EDragType>()->GetNameByEnum(payload._type));
            };
            _icon_area->SetDropHandler(handler);
            _icon_area->OnFileDrop() += [this](UI::UIEvent &e)
            {
                Vector<WString> dropped_files;
                dropped_files.reserve(e._drop_files.size());
                for (auto &f: e._drop_files)
                {
                    LOG_INFO(L"Drop file {}",(f));
                    dropped_files.push_back(f);
                }
                QueueImportFiles(dropped_files, e._mouse_position);
            };
        }

        AssetBrowser::~AssetBrowser()
        {
            delete _directory_tree_data_source;
            _directory_tree_data_source = nullptr;
        }

        void AssetBrowser::NavigateToPath(const fs::path &path)
        {
            if (path.empty() || !fs::exists(path) || !fs::is_directory(path))
                return;

            fs::path target_path = path;
            if (!FindRootForPath(target_path).has_value())
            {
                auto roots = GetAssetBrowserRoots();
                target_path = roots.empty() ? fs::path(ResourceMgr::Get().EngineResRootPath()) : roots.front()._sys_path;
            }

            _current_path = target_path;
            LOG_INFO("AssetBrowser: navigate to {}", _current_path.string());
            _is_dirty = true;
            _is_icon_layout_dirty = true;
        }

        void AssetBrowser::RefreshDirectoryTree()
        {
            if (_directory_tree == nullptr || _directory_tree_data_source == nullptr)
                return;

            _directory_tree_data_source->Rebuild();
            _directory_tree->Refresh();
            const UI::TreeItemId current_item = _directory_tree_data_source->FindItemByPath(_current_path);
            if (current_item != UI::kInvalidTreeItemId)
            {
                _directory_tree->ExpandParents(current_item);
                _directory_tree->SetSelectedItem(current_item, false);
            }
            _is_directory_tree_dirty = false;
        }

        void AssetBrowser::UpdatePathButtons()
        {
            if (_path_bar == nullptr)
                return;

            _path_bar->ClearChildren();
            const auto root = FindRootForPath(_current_path);
            if (!root.has_value())
            {
                _path_title = _path_bar->AddChild<UI::Text>(_current_path.string());
                _path_title->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({0.0f, 22.0f});
                _path_title->_horizontal_align = EAlignment::kLeft;
                return;
            }

            Vector<std::pair<String, fs::path>> crumbs;
            crumbs.push_back({root->_label, root->_sys_path});
            const WString relative_directory = StripLogicalAssetPathScheme(GetRelativeAssetDirectory(_current_path));
            if (!relative_directory.empty())
            {
                fs::path walk_path = root->_sys_path;
                for (const auto &part: fs::path(relative_directory))
                {
                    walk_path /= part;
                    crumbs.push_back({part.string(), walk_path});
                }
            }

            for (size_t i = 0u; i < crumbs.size(); ++i)
            {
                auto *button = _path_bar->AddChild<UI::Button>();
                const f32 button_width = std::max(56.0f, static_cast<f32>(crumbs[i].first.size()) * 8.0f + 20.0f);
                button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed).Size({button_width, 22.0f});
                button->SetText(crumbs[i].first);
                button->OnMouseClick() += [this, target_path = crumbs[i].second](UI::UIEvent &e)
                {
                    NavigateToPath(target_path);
                    e._is_handled = true;
                };

                if (i + 1u < crumbs.size())
                {
                    auto *separator = _path_bar->AddChild<UI::Text>(">");
                    separator->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed).Size({14.0f, 22.0f});
                    separator->_horizontal_align = EAlignment::kCenter;
                }
            }
        }

        void AssetBrowser::QueueImportFiles(const Vector<WString> &files, Vector2f popup_pos)
        {
            if (files.empty())
                return;

            const bool was_empty = _pending_import_files.empty();
            _import_popup_pos = popup_pos;
            _pending_import_files.insert(_pending_import_files.end(), files.begin(), files.end());
            if (was_empty)
                ShowNextImportPopup();
        }

        void AssetBrowser::ShowNextImportPopup()
        {
            while (!_pending_import_files.empty())
            {
                const WString &sys_path = _pending_import_files.front();
                switch (GetImportPopupType(sys_path))
                {
                case EImportPopupType::kTexture:
                case EImportPopupType::kMesh:
                    ShowImportPopupForFile(sys_path);
                    return;
                case EImportPopupType::kDirect:
                default:
                    ResourceMgr::Get().ImportResource(sys_path, _current_path.wstring());
                    _pending_import_files.erase(_pending_import_files.begin());
                    _is_dirty = true;
                    break;
                }
            }
        }

        void AssetBrowser::ShowImportPopupForFile(const WString &sys_path)
        {
            const String file_name = fs::path(sys_path).filename().string();
            const auto finish_popup = [this]()
            {
                UIManager::Get()->HidePopup();
                AdvanceImportQueue();
            };

            switch (GetImportPopupType(sys_path))
            {
            case EImportPopupType::kTexture:
            {
                auto setting = std::make_shared<TextureImportSetting>(TextureImportSetting::Default());
                EditorPopup::ShowDialogAt(_import_popup_pos, "AssetBrowserTextureImportPrompt",
                                          std::format("Import Texture: {}", file_name), {320.0f, 180.0f},
                                          [setting, file_name](UI::VerticalBox *content, UI::Text *)
                                          {
                                              auto *file_text = content->AddChild<Text>(std::format("File: {}", file_name));
                                              file_text->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
                                              file_text->_horizontal_align = EAlignment::kLeft;

                                              auto *srgb = EditorPopup::AddCheckBoxRow(content, "sRGB", setting->_is_sRGB);
                                              srgb->_on_click += [setting](bool checked)
                                              {
                                                  setting->_is_sRGB = checked;
                                              };

                                              auto *mipmap = EditorPopup::AddCheckBoxRow(content, "Generate Mipmap", setting->_generate_mipmap);
                                              mipmap->_on_click += [setting](bool checked)
                                              {
                                                  setting->_generate_mipmap = checked;
                                              };

                                              auto *readable = EditorPopup::AddCheckBoxRow(content, "Readable", setting->_is_readable);
                                              readable->_on_click += [setting](bool checked)
                                              {
                                                  setting->_is_readable = checked;
                                              };
                                          },
                                          {
                                                  {"Import", [this, setting, sys_path, finish_popup]() -> std::optional<String>
                                                   {
                                                       ResourceMgr::Get().ImportResource(sys_path, _current_path.wstring(), *setting);
                                                       _is_dirty = true;
                                                       finish_popup();
                                                       return std::nullopt;
                                                   }, false, false},
                                                  {"Cancel", [finish_popup]() -> std::optional<String>
                                                   {
                                                       finish_popup();
                                                       return std::nullopt;
                                                   }, false, false}
                                          });
                return;
            }
            case EImportPopupType::kMesh:
            {
                auto setting = std::make_shared<MeshImportSetting>(MeshImportSetting::Default());
                setting->_import_flag |= MeshImportSetting::kImportFlagMesh;
                EditorPopup::ShowDialogAt(_import_popup_pos, "AssetBrowserMeshImportPrompt",
                                          std::format("Import Mesh: {}", file_name), {320.0f, 180.0f},
                                          [setting, file_name](UI::VerticalBox *content, UI::Text *)
                                          {
                                              auto *file_text = content->AddChild<Text>(std::format("File: {}", file_name));
                                              file_text->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
                                              file_text->_horizontal_align = EAlignment::kLeft;

                                              auto *materials = EditorPopup::AddCheckBoxRow(content, "Import Materials", setting->_is_import_material);
                                              materials->_on_click += [setting](bool checked)
                                              {
                                                  setting->_is_import_material = checked;
                                              };

                                              auto *combine_mesh = EditorPopup::AddCheckBoxRow(content, "Combine Meshes", setting->_is_combine_mesh);
                                              combine_mesh->_on_click += [setting](bool checked)
                                              {
                                                  setting->_is_combine_mesh = checked;
                                              };

                                              const bool import_animation = (setting->_import_flag & MeshImportSetting::kImportFlagAnimation) != 0;
                                              auto *animation = EditorPopup::AddCheckBoxRow(content, "Import Animation", import_animation);
                                              animation->_on_click += [setting](bool checked)
                                              {
                                                  if (checked)
                                                      setting->_import_flag |= MeshImportSetting::kImportFlagAnimation;
                                                  else
                                                      setting->_import_flag &= ~MeshImportSetting::kImportFlagAnimation;
                                              };
                                          },
                                          {
                                                  {"Import", [this, setting, sys_path, finish_popup]() -> std::optional<String>
                                                   {
                                                       ResourceMgr::Get().ImportResource(sys_path, _current_path.wstring(), *setting);
                                                       _is_dirty = true;
                                                       finish_popup();
                                                       return std::nullopt;
                                                   }, false, false},
                                                  {"Cancel", [finish_popup]() -> std::optional<String>
                                                   {
                                                       finish_popup();
                                                       return std::nullopt;
                                                   }, false, false}
                                          });
                return;
            }
            case EImportPopupType::kDirect:
            default:
                ResourceMgr::Get().ImportResource(sys_path, _current_path.wstring());
                _is_dirty = true;
                AdvanceImportQueue();
                return;
            }
        }

        void AssetBrowser::AdvanceImportQueue()
        {
            if (!_pending_import_files.empty())
                _pending_import_files.erase(_pending_import_files.begin());
            ShowNextImportPopup();
        }

        void AssetBrowser::SaveDockLayoutState(JsonArchive &ar)
        {
            if (_sv)
                _split_ratio = _sv->GetRatio();
            ar.BeginObject("_split_ratio");
            ar << _split_ratio;
            ar.EndObject();
        }

        void AssetBrowser::LoadDockLayoutState(JsonArchive &ar)
        {
            if (ar.HasField("_split_ratio"))
            {
                ar.BeginObject("_split_ratio");
                ar >> _split_ratio;
                ar.EndObject();
            }
        }

        void AssetBrowser::OnDockLayoutLoaded()
        {
            if (_sv)
                _sv->SetRatio(_split_ratio);
        }

        void AssetBrowser::Update(f32 dt)
        {
            DockWindow::Update(dt);
            if (_is_directory_tree_dirty)
                RefreshDirectoryTree();

            static auto s_folder_icon = ResourceMgr::Get().Get<Texture2D>(EnginePath::kEngineIconPathW + L"folder.alasset");
            static auto s_file_icon = ResourceMgr::Get().Get<Texture2D>(EnginePath::kEngineIconPathW + L"file.alasset");
            static auto s_mesh_icon = ResourceMgr::Get().Get<Texture2D>(EnginePath::kEngineIconPathW + L"3d.alasset");
            static auto s_shader_icon = ResourceMgr::Get().Get<Texture2D>(EnginePath::kEngineIconPathW + L"shader.alasset");
            static auto s_image_icon = ResourceMgr::Get().Get<Texture2D>(EnginePath::kEngineIconPathW + L"image.alasset");
            static auto s_scene_icon = ResourceMgr::Get().Get<Texture2D>(EnginePath::kEngineIconPathW + L"dark/scene.alasset");
            static auto s_material_icon = ResourceMgr::Get().Get<Texture2D>(EnginePath::kEngineIconPathW + L"dark/material.alasset");
            static auto s_animclip_icon = ResourceMgr::Get().Get<Texture2D>(EnginePath::kEngineIconPathW + L"dark/anim_clip.alasset");
            static auto s_skeleton_icon = ResourceMgr::Get().Get<Texture2D>(EnginePath::kEngineIconPathW + L"dark/skeleton.alasset");

            Vector2f parent_size = _icon_area->GetContentRect().zw;
            if (parent_size.x <= 0.0f || parent_size.y <= 0.0f)
                return;

            if (!NearbyEqual(parent_size, _last_icon_area_size))
            {
                _last_icon_area_size = parent_size;
                _is_icon_layout_dirty = true;
            }
            static Render::Sprite* preview_sp = nullptr;
            // if (preview_sp)
            // {
            //     AssetPreviewGenerator::GeneratorSpriteSnapshot(256,256,preview_sp,_asset_preview_icons[preview_sp]);
            // }
            if (_is_dirty)
            {
                if (fs::exists(_current_path))
                {
                    _icon_content->ClearChildren();
                    const WString current_asset_directory = GetRelativeAssetDirectory(_current_path);
                    const EAssetDomain current_domain = GetDomainForPath(_current_path);
                    const bool is_searching = !_search_text.empty();
                    _cur_dir_assets.clear();
                    for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); ++it)
                    {
                        Asset *asset = it->second.get();
                        if (asset->_domain != current_domain)
                            continue;

                        const WString asset_directory = ExtractLogicalAssetDirectory(asset->_asset_path);
                        if (!IsAssetInDirectory(asset_directory, current_asset_directory, is_searching))
                            continue;

                        const String asset_display_name = asset->_p_obj ? asset->_p_obj->Name() : asset->Name();
                        if (is_searching && !ContainsSearchText(asset_display_name, _search_text) && !ContainsSearchText(ToChar(asset->_asset_path.c_str()), _search_text))
                            continue;
                        _cur_dir_assets.push_back(asset);
                    }
                    std::sort(_cur_dir_assets.begin(), _cur_dir_assets.end(), [](const Asset *lhs, const Asset *rhs)
                    {
                        return lhs->Name() < rhs->Name();
                    });
                    UpdatePathButtons();
                    const auto create_icon_group = [this](const String &display_name) -> std::tuple<Ref<UI::UIElement>, UI::Image *, UI::Text *>
                    {
                        Ref<UI::UIElement> root;
                        UI::Image *icon = nullptr;
                        UI::Text *text = nullptr;
                        if (_is_list_view)
                        {
                            auto hb = MakeRef<UI::HorizontalBox>();
                            hb->SlotPadding() = UI::Padding(2.0f);
                            hb->InvalidateLayout();
                            icon = hb->AddChild<UI::Image>();
                            icon->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                                    .Size({kListIconSize, kListIconSize}).Margin({2.0f, 1.0f, 4.0f, 1.0f});
                            text = hb->AddChild<UI::Text>();
                            text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                                    .Size({0.0f, kListRowHeight}).FillRate(1.0f).CrossAlignment(UI::EAlignment::kFill);
                            text->_horizontal_align = UI::EAlignment::kLeft;
                            root = hb;
                        }
                        else
                        {
                            auto vb = MakeRef<UI::VerticalBox>();
                            vb->SlotPadding() = UI::Padding(kIconCellPadding);
                            vb->InvalidateLayout();
                            icon = vb->AddChild<UI::Image>();
                            icon->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                                    .Size({_icon_size, _icon_size}).CrossAlignment(UI::EAlignment::kCenter);
                            text = vb->AddChild<UI::Text>();
                            text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                                    .Size({kIconCellMinWidth, kIconLabelHeight}).CrossAlignment(UI::EAlignment::kCenter);
                            text->_horizontal_align = UI::EAlignment::kCenter;
                            root = vb;
                        }
                        root->Name(display_name);
                        text->Name(display_name);
                        text->SetText(display_name);
                        text->_vertical_align = UI::EAlignment::kCenter;
                        text->SlotPadding() = UI::Padding(kListTextLeftPadding, 0.0f, kListTextLeftPadding, 0.0f);
                        text->InvalidateLayout();
                        text->FontSize(14.0f);
                        return std::make_tuple(root, icon, text);
                    };
                    Vector<fs::directory_entry> directories;
                    std::error_code directory_error;
                    for (fs::directory_iterator dir_it(_current_path, directory_error); !directory_error && dir_it != fs::directory_iterator(); dir_it.increment(directory_error))
                    {
                        if (dir_it->is_directory(directory_error) && ContainsSearchText(dir_it->path().filename().string(), _search_text))
                            directories.push_back(*dir_it);
                    }
                    std::sort(directories.begin(), directories.end(), [](const fs::directory_entry &lhs, const fs::directory_entry &rhs)
                    {
                        return lhs.path().filename().wstring() < rhs.path().filename().wstring();
                    });
                    for (auto &dir_it: directories)
                    {
                        fs::path item_path = dir_it.path();
                        const auto folder_sys_path = item_path.wstring();
                        const String display_name = item_path.filename().string();
                        auto [vb, icon,text] = create_icon_group(display_name);
                        icon->Name(display_name);
                        icon->SetTexture(s_folder_icon);
                        vb->OnMouseDown() += [this, folder_sys_path](UI::UIEvent &e)
                        {
                            if (e._key_code != EKey::kRBUTTON)
                                return;
                            ShowFolderContextMenu(folder_sys_path, e._mouse_position);
                            e._is_handled = true;
                        };
                        icon->OnMouseEnter() += [this, icon](UI::UIEvent &e)
                        {
                            _hover_item = e._current_target;
                            e._current_target->As<Image>()->_tint_color = Colors::kYellow;
                        };
                        icon->OnMouseExit() += [this](UI::UIEvent &e)
                        {
                            if (_hover_item == e._current_target)
                            {
                                _hover_item = nullptr;
                                e._current_target->As<Image>()->_tint_color = Colors::kWhite;
                            }
                        };
                        icon->OnMouseDoubleClick() += [this, item_path](UI::UIEvent &e)
                        {
                            NavigateToPath(item_path);
                        };
                        //icon->OnMouseDown() += [this, icon](UI::UIEvent &e)
                        //{
                        //    s_is_drag_start = true;
                        //    auto payload = DragPayload{EDragType::kFolder, nullptr};
                        //    DragDropManager::Get().BeginDrag(payload, "folder");
                        //};
                        _icon_content->AddChild(vb);
                    }
                    for (auto asset: _cur_dir_assets)
                    {
                        const String display_name = asset->_p_obj ? asset->_p_obj->Name() : asset->Name();
                        auto [vb, icon, text] = create_icon_group(display_name);
                        Color tint = asset->_p_obj ? Colors::kWhite : Colors::kGray;
                        icon->Name(asset->Name());
                        icon->_tint_color = tint;
                        vb->OnMouseDown() += [this, asset](UI::UIEvent &e)
                        {
                            if (e._key_code != EKey::kRBUTTON)
                                return;
                            ShowAssetContextMenu(asset, e._mouse_position);
                            e._is_handled = true;
                        };
                        if (asset->_asset_type == StaticClass<Render::Mesh>())
                        {
                            if (asset->_p_obj)
                            {
                                auto mesh = asset->As<Render::Mesh>();
                                    Ref<RenderTexture> mesh_icon{nullptr};
                                    AssetPreviewGenerator::GeneratorMeshSnapshot(512u, 512u, mesh, mesh_icon);
                                    _asset_preview_icons[mesh] = mesh_icon;
                                icon->SetTexture(_asset_preview_icons[mesh].get());
                            }
                            else
                            icon->SetTexture(s_mesh_icon);
                        }
                        else if (asset->_asset_type == StaticClass<Render::Shader>())
                        {
                            icon->SetTexture(s_shader_icon);
                        }
                        else if (asset->_asset_type == StaticClass<SceneManagement::Scene>())
                        {
                            icon->SetTexture(s_scene_icon);
                        }
                        else if (asset->_asset_type == StaticClass<Render::Texture2D>())
                        {
                            if (asset->_p_obj == nullptr)
                            {
                                //并非当帧完成
                                icon->SetTexture(ResourceMgr::Get().Load<Texture2D>(asset->_asset_path).get());
                            }
                            else
                            {
                                icon->SetTexture(asset->As<Texture>());
                            }
                        }
                        else if (asset->_asset_type == StaticClass<Render::Material>())
                            icon->SetTexture(s_material_icon);
                        else if (asset->_asset_type == StaticClass<AnimationClip>())
                            icon->SetTexture(s_animclip_icon);
                        else if (asset->_asset_type == StaticClass<Render::SkeletonMesh>())
                            icon->SetTexture(s_skeleton_icon);
                        else if (asset->_asset_type == StaticClass<Render::Sprite>())
                        {
                            if (asset->_p_obj)
                            {
                                auto obj = asset->As<Render::Sprite>();
                                Ref<RenderTexture> preview_icon{nullptr};
                                AssetPreviewGenerator::GeneratorSpriteSnapshot(256,256,obj,preview_icon);
                                _asset_preview_icons[obj] = preview_icon;
                                preview_sp = obj;
                                icon->SetTexture(_asset_preview_icons[obj].get());
                            }
                            else
                            icon->SetTexture(s_mesh_icon);
                        }
                        else {};
                        icon->OnMouseEnter() += [this, icon](UI::UIEvent &e)
                        {
                            _hover_item = e._current_target;
                            e._current_target->As<Image>()->_tint_color = Colors::kYellow;
                        };
                        icon->OnMouseExit() += [this,tint](UI::UIEvent &e)
                        {
                            if (_hover_item == e._current_target)
                            {
                                _hover_item = nullptr;
                                e._current_target->As<Image>()->_tint_color = tint;
                            }
                        };
                        icon->OnMouseDoubleClick() += [this, asset](UI::UIEvent &e)
                        {
                            OpenAsset(asset);
                        };
                        _icon_content->AddChild(vb);
                        if (asset->_asset_type == StaticClass<Render::Mesh>())
                        {
                            icon->OnMouseDown() += [this, icon, asset](UI::UIEvent &e)
                            {
                                _is_dragging = false;
                                _drag_start_pos = e._mouse_position;
                            };
                            icon->OnMouseMove() += [this, icon, asset](UI::UIEvent &e)
                            {
                                if (Input::IsKeyDown(EKey::kLBUTTON))
                                {
                                    if (!_is_dragging)
                                    {
                                        f32 dist = Magnitude(e._mouse_position - _drag_start_pos);
                                        if (dist > kDragThreshold)
                                        {
                                            _is_dragging = true;
                                            auto payload = DragPayload{EDragType::kMesh, asset};
                                            DragDropManager::Get().BeginDrag(payload, "mesh");
                                        }
                                    }
                                }
                            };

                        }
                    }
                }
                _is_icon_layout_dirty = true;
                _is_dirty = false;
            }

            if (_is_icon_layout_dirty)
            {
                const f32 cell_width = _is_list_view ? parent_size.x : std::max(kIconCellMinWidth, _icon_size + kIconCellPadding * 2.0f + kIconCellGap);
                const f32 icon_draw_size = _is_list_view ? kListIconSize : std::max(1.0f, _icon_size);
                const f32 cell_height = _is_list_view ? kListRowHeight : icon_draw_size + kIconLabelHeight + kIconCellPadding * 2.0f;
                const f32 label_width = _is_list_view ? std::max(0.0f, parent_size.x - kListIconSize - kListTextLeftPadding * 2.0f - 12.0f) : std::max(0.0f, cell_width - kIconCellPadding * 2.0f);
                f32 x = 0.0f;
                f32 y = 0.0f;
                u32 num_per_row = _is_list_view ? 1u : (u32) (parent_size.x / cell_width);
                if (num_per_row == 0u)
                    num_per_row = 1u;
                for (u32 i = 0; i < (u32) _icon_content->GetChildren().size(); i++)
                {
                    auto child = _icon_content->ChildAt(i);
                    child->GetSlotAs<UI::CanvasSlot>().Position({x, y}).Size({cell_width, cell_height});
                    if (_is_list_view)
                    {
                        if (auto row = child->As<UI::HorizontalBox>())
                        {
                            if (auto icon = row->ChildAt(0u); icon != nullptr)
                                icon->GetSlotAs<UI::LinearSlot>().Size({icon_draw_size, icon_draw_size});
                            if (auto text = row->ChildAt(1u)->As<UI::Text>(); text != nullptr)
                            {
                                text->GetSlotAs<UI::LinearSlot>().Size({label_width, kListRowHeight});
                                text->SetText(FitTextToWidth(text->Name(), label_width, text->FontSize()));
                            }
                        }
                    }
                    else if (auto tile = child->As<UI::VerticalBox>())
                    {
                        if (auto icon = tile->ChildAt(0u); icon != nullptr)
                            icon->GetSlotAs<UI::LinearSlot>().Size({icon_draw_size, icon_draw_size});
                        if (auto text = tile->ChildAt(1u)->As<UI::Text>(); text != nullptr)
                        {
                            text->GetSlotAs<UI::LinearSlot>().Size({label_width, kIconLabelHeight});
                            text->SetText(FitTextToWidth(text->Name(), label_width, text->FontSize()));
                        }
                    }
                    if ((i + 1) % num_per_row == 0)
                    {
                        x = 0.0f;
                        y += cell_height;
                    }
                    else
                    {
                        x += cell_width;
                    }
                }
                _is_icon_layout_dirty = false;
            }
        }

        void AssetBrowser::OpenAsset(Asset *asset)
        {
            if (asset == nullptr)
                return;

            if (asset->_asset_type == StaticClass<SceneManagement::Scene>())
            {
                SceneManagement::SceneMgr::Get().OpenScene(asset->_asset_path);
            }
            else if (asset->_asset_type == StaticClass<Render::Mesh>())
            {
                if (asset->_p_obj == nullptr)
                {
                    ResourceMgr::Get().Load<Mesh>(asset->_asset_path);
                    _is_dirty = true;
                }
            }
            else if (asset->_asset_type == StaticClass<Render::Sprite>())
            {
                if (asset->_p_obj == nullptr)
                    ResourceMgr::Get().Load<Render::Sprite>(asset->_asset_path);

                if (asset->_p_obj)
                {
                    auto editor = MakeRef<SpriteAssetEditor>();
                    editor->Open(asset->As<Sprite>());
                    DockManager::Get().AddDock(editor);
                }
            }
        }

        void AssetBrowser::ShowCreateMaterialDialog(Vector2f popup_pos, const fs::path &target_sys_path)
        {
            auto material_name = std::make_shared<String>(MakeUniqueEntryName(target_sys_path.wstring(), "NewMaterial", L".alasset", false));
            auto shaders = std::make_shared<Vector<Ref<Render::Shader>>>(CollectMaterialShaders());
            auto shader_names = std::make_shared<Vector<String>>();
            shader_names->reserve(shaders->size());
            for (const auto &shader: *shaders)
                shader_names->push_back(shader ? shader->Name() : String("null shader"));
            if (shader_names->empty())
                shader_names->push_back("Missing Shader");

            auto selected_shader_index = std::make_shared<i32>(shaders->empty() ? -1 : 0);
            UI::InputBlock *name_input = nullptr;
            UI::Dropdown *shader_dropdown = nullptr;

            EditorPopup::ShowDialogAt(popup_pos, "AssetBrowserMaterialCreatePrompt", "Create Material", {300.0f, 134.0f},
                                      [material_name, shader_names, selected_shader_index, &name_input, &shader_dropdown](UI::VerticalBox *content, UI::Text *)
                                      {
                                          UI::HorizontalBox *value_box = nullptr;
                                          auto *name_row = EditorPopup::AddPropertyRow(content, "name:", &value_box);
                                          name_row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({280.0f, 24.0f});
                                          name_input = value_box->AddChild<UI::InputBlock>();
                                          name_input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({0.0f, 24.0f});
                                          name_input->_on_content_changed += [material_name](String value)
                                          {
                                              *material_name = std::move(value);
                                          };
                                          name_input->SetContent(*material_name, false);

                                          auto *shader_row = EditorPopup::AddPropertyRow(content, "shader:", &value_box);
                                          shader_row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({280.0f, 24.0f});
                                          shader_dropdown = value_box->AddChild<UI::Dropdown>(*shader_names);
                                          shader_dropdown->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({0.0f, 24.0f});
                                          shader_dropdown->_on_selected_changed += [selected_shader_index](i32 index)
                                          {
                                              *selected_shader_index = index;
                                          };
                                          shader_dropdown->SetSelectedIndex(*selected_shader_index);
                                      },
                                      {
                                              {"OK", [this, target_sys_path, material_name, shaders, selected_shader_index, name_input]() -> std::optional<String>
                                               {
                                                   const String name = TrimNameCopy(*material_name);
                                                   if (auto error = ValidateEntryName(name); error.has_value())
                                                   {
                                                       if (name_input != nullptr)
                                                           name_input->RequestFocus();
                                                       return error;
                                                   }
                                                   if (*selected_shader_index < 0 || *selected_shader_index >= static_cast<i32>(shaders->size()) || (*shaders)[*selected_shader_index] == nullptr)
                                                       return String("Shader is required.");

                                                   const fs::path previous_path = _current_path;
                                                   _current_path = target_sys_path;
                                                   const bool created = CreateMaterialEntry(name, (*shaders)[*selected_shader_index].get());
                                                   _current_path = previous_path;
                                                   if (!created)
                                                       return String("Material already exists.");
                                                   return std::nullopt;
                                               }},
                                              {"Cancel", []() -> std::optional<String> { return std::nullopt; }}
                                      },
                                      [name_input]()
                                      {
                                          if (name_input != nullptr)
                                              name_input->RequestFocus();
                                      });
        }

        void AssetBrowser::ShowBlankAreaContextMenu(Vector2f popup_pos)
        {
            Vector<PopupMenuAction> actions;
            actions.push_back({"New Folder", [this, popup_pos]()
            {
                EditorPopup::ShowTextInputAt(popup_pos, "Create Folder",
                                             MakeUniqueEntryName(_current_path.wstring(), "NewFolder", L"", true),
                                             [this](const String &input) -> std::optional<String>
                                             {
                                                 const String name = TrimNameCopy(input);
                                                 if (auto error = ValidateEntryName(name); error.has_value())
                                                     return error;
                                                 if (!CreateFolderEntry(name))
                                                     return String("Folder already exists.");
                                                 return std::nullopt;
                                             });
            }});
            actions.push_back({"New Scene", [this, popup_pos]()
            {
                EditorPopup::ShowTextInputAt(popup_pos, "Create Scene",
                                             MakeUniqueEntryName(_current_path.wstring(), "NewScene", L".almap", false),
                                             [this](const String &input) -> std::optional<String>
                                             {
                                                 const String name = TrimNameCopy(input);
                                                 if (auto error = ValidateEntryName(name); error.has_value())
                                                     return error;
                                                 if (!CreateSceneEntry(name))
                                                     return String("Scene already exists.");
                                                 return std::nullopt;
                                             });
            }});
            actions.push_back({"New Material", [this, popup_pos]()
            {
                ShowCreateMaterialDialog(popup_pos, _current_path);
            }});
            actions.push_back({"Refresh", [this]() { _is_dirty = true; }});
            EditorPopup::ShowActionMenuAt(popup_pos, actions);
        }

        void AssetBrowser::ShowFolderContextMenu(const WString &folder_sys_path, Vector2f popup_pos)
        {
            const String folder_name = fs::path(folder_sys_path).filename().string();
            const auto create_folder_in_target = [this, folder_sys_path](const String &name) -> bool
            {
                const fs::path previous_path = _current_path;
                _current_path = folder_sys_path;
                const bool created = CreateFolderEntry(name);
                _current_path = previous_path;
                return created;
            };
            const auto create_scene_in_target = [this, folder_sys_path](const String &name) -> bool
            {
                const fs::path previous_path = _current_path;
                _current_path = folder_sys_path;
                const bool created = CreateSceneEntry(name);
                _current_path = previous_path;
                return created;
            };
            Vector<PopupMenuAction> actions;
            actions.push_back({"Open", [this, folder_sys_path]()
            {
                NavigateToPath(folder_sys_path);
            }});
            actions.push_back({"Rename", [this, folder_name, folder_sys_path, popup_pos]()
            {
                EditorPopup::ShowTextInputAt(popup_pos, "Rename Folder", folder_name,
                                             [this, folder_sys_path](const String &input) -> std::optional<String>
                                             {
                                                 const String name = TrimNameCopy(input);
                                                 if (auto error = ValidateEntryName(name); error.has_value())
                                                     return error;
                                                 if (!RenameFolderEntry(folder_sys_path, name))
                                                     return String("Folder rename failed.");
                                                 return std::nullopt;
                                             });
            }});
            actions.push_back({"Delete", [this, folder_name, folder_sys_path, popup_pos]()
            {
                EditorPopup::ShowConfirmAt(popup_pos, std::format("Delete folder \"{}\"?", folder_name),
                                           [this, folder_sys_path]() { DeleteFolderEntry(folder_sys_path); });
            }, true});
            actions.push_back({"New Folder", [this, popup_pos, folder_sys_path, create_folder_in_target]()
            {
                EditorPopup::ShowTextInputAt(popup_pos, "Create Folder",
                                             MakeUniqueEntryName(folder_sys_path, "NewFolder", L"", true),
                                             [create_folder_in_target](const String &input) -> std::optional<String>
                                             {
                                                 const String name = TrimNameCopy(input);
                                                 if (auto error = ValidateEntryName(name); error.has_value())
                                                     return error;
                                                 if (!create_folder_in_target(name))
                                                     return String("Folder already exists.");
                                                 return std::nullopt;
                                             });
            }});
            actions.push_back({"New Scene", [this, popup_pos, folder_sys_path, create_scene_in_target]()
            {
                EditorPopup::ShowTextInputAt(popup_pos, "Create Scene",
                                             MakeUniqueEntryName(folder_sys_path, "NewScene", L".almap", false),
                                             [create_scene_in_target](const String &input) -> std::optional<String>
                                             {
                                                 const String name = TrimNameCopy(input);
                                                 if (auto error = ValidateEntryName(name); error.has_value())
                                                     return error;
                                                 if (!create_scene_in_target(name))
                                                     return String("Scene already exists.");
                                                 return std::nullopt;
                                             });
            }});
            actions.push_back({"New Material", [this, popup_pos, folder_sys_path]()
            {
                ShowCreateMaterialDialog(popup_pos, folder_sys_path);
            }});
            EditorPopup::ShowActionMenuAt(popup_pos, actions);
        }

        void AssetBrowser::ShowAssetContextMenu(Asset *asset, Vector2f popup_pos)
        {
            if (asset == nullptr)
                return;

            const String asset_name = asset->Name();
            Vector<PopupMenuAction> actions;
            if (asset->_asset_type == StaticClass<SceneManagement::Scene>() || asset->_asset_type == StaticClass<Render::Mesh>())
            {
                actions.push_back({"Open", [this, asset]() { OpenAsset(asset); }});
            }
            actions.push_back({"Rename", [this, asset_name, asset, popup_pos]()
            {
                EditorPopup::ShowTextInputAt(popup_pos, "Rename Asset", asset_name,
                                             [this, asset](const String &input) -> std::optional<String>
                                             {
                                                 const String name = TrimNameCopy(input);
                                                 if (auto error = ValidateEntryName(name); error.has_value())
                                                     return error;
                                                 if (!RenameAssetEntry(asset, name))
                                                     return String("Asset rename failed.");
                                                 return std::nullopt;
                                             });
            }});
            actions.push_back({"Delete", [this, asset_name, asset, popup_pos]()
            {
                EditorPopup::ShowConfirmAt(popup_pos, std::format("Delete asset \"{}\"?", asset_name),
                                           [this, asset]() { DeleteAssetEntry(asset); });
            }, true});
            EditorPopup::ShowActionMenuAt(popup_pos, actions);
        }

        bool AssetBrowser::RenameAssetEntry(Asset *asset, const String &new_name)
        {
            if (asset == nullptr)
                return false;

            const String name = TrimNameCopy(new_name);
            if (name.empty())
                return false;
            if (PathUtils::GetFileName(asset->_asset_path) == ToWChar(name.c_str()))
                return true;

            const WString old_asset_path = asset->_asset_path;
            const WString old_sys_path = ResourceMgr::GetResSysPath(old_asset_path);
            const WString new_asset_path = PathUtils::RenameFile(old_asset_path, ToWChar(name.c_str()));
            const WString new_sys_path = ResourceMgr::GetResSysPath(new_asset_path);
            if (ResourceMgr::Get().GetAsset(new_asset_path) != nullptr || fs::exists(new_sys_path))
                return false;

            std::error_code rename_error;
            fs::rename(old_sys_path, new_sys_path, rename_error);
            if (rename_error)
            {
                LOG_WARNING("AssetBrowser: rename file failed, {}", rename_error.message());
                return false;
            }
            if (!ResourceMgr::Get().RenameAsset(asset, ToWChar(name.c_str())))
            {
                std::error_code rollback_error;
                fs::rename(new_sys_path, old_sys_path, rollback_error);
                if (rollback_error)
                    LOG_WARNING("AssetBrowser: rename rollback failed, {}", rollback_error.message());
                return false;
            }

            if (asset->_p_obj)
                asset->_p_obj->Name(name);
            RewriteAssetHeaderName(new_sys_path, name);
            ResourceMgr::Get().SaveAllUnsavedAssets();
            _is_dirty = true;
            _is_directory_tree_dirty = true;
            return true;
        }

        bool AssetBrowser::RenameFolderEntry(const WString &folder_sys_path, const String &new_name)
        {
            const String name = TrimNameCopy(new_name);
            if (name.empty())
                return false;

            const fs::path old_path(folder_sys_path);
            if (!fs::exists(old_path) || !fs::is_directory(old_path))
                return false;
            if (old_path.filename().string() == name)
                return true;

            const fs::path new_path = old_path.parent_path() / fs::path(ToWChar(name.c_str()));
            if (fs::exists(new_path))
                return false;

            const WString old_dir_asset_path = ResourceMgr::NormalizeAssetPath(old_path.wstring(), GetDomainForPath(old_path));
            const WString new_dir_asset_path = ResourceMgr::NormalizeAssetPath(new_path.wstring(), GetDomainForPath(new_path));
            auto nested_assets = CollectAssetsUnderDirectory(old_dir_asset_path);

            std::error_code rename_error;
            fs::rename(old_path, new_path, rename_error);
            if (rename_error)
            {
                LOG_WARNING("AssetBrowser: rename folder failed, {}", rename_error.message());
                return false;
            }

            const WString old_prefix = NormalizeLogicalDirectoryPath(old_dir_asset_path);
            for (auto *asset: nested_assets)
            {
                WString asset_path = NormalizeLogicalPathWithoutTrailingSlash(asset->_asset_path);
                if (asset_path.compare(0, old_prefix.size(), old_prefix) != 0)
                    continue;
                WString suffix = asset_path.substr(old_prefix.size());
                WString new_asset_path = AppendChildAssetPath(new_dir_asset_path, suffix);
                if (!ResourceMgr::Get().MoveAsset(asset, new_asset_path))
                    LOG_WARNING(L"AssetBrowser: move asset {} to {} failed after folder rename.", asset->_asset_path, new_asset_path);
            }

            ResourceMgr::Get().SaveAllUnsavedAssets();
            _is_dirty = true;
            return true;
        }

        void AssetBrowser::DeleteAssetEntry(Asset *asset)
        {
            if (asset == nullptr)
                return;

            const WString asset_sys_path = ResourceMgr::GetResSysPath(asset->_asset_path);
            std::error_code remove_error;
            const bool removed = fs::remove(asset_sys_path, remove_error);
            if (remove_error || !removed)
            {
                LOG_WARNING("AssetBrowser: delete asset file failed, {}", remove_error.message());
                return;
            }

            ResourceMgr::Get().DeleteAsset(asset);
            ResourceMgr::Get().Tick(0.0f);
            ResourceMgr::Get().SaveAllUnsavedAssets();
            _is_dirty = true;
            _is_directory_tree_dirty = true;
        }

        void AssetBrowser::DeleteFolderEntry(const WString &folder_sys_path)
        {
            const fs::path folder_path(folder_sys_path);
            if (!fs::exists(folder_path) || !fs::is_directory(folder_path))
                return;

            const WString dir_asset_path = ResourceMgr::NormalizeAssetPath(folder_sys_path, GetDomainForPath(folder_sys_path));
            auto assets_to_delete = CollectAssetsUnderDirectory(dir_asset_path);

            std::error_code remove_error;
            fs::remove_all(folder_path, remove_error);
            if (remove_error)
            {
                LOG_WARNING("AssetBrowser: delete folder failed, {}", remove_error.message());
                return;
            }

            for (auto *asset: assets_to_delete)
                ResourceMgr::Get().DeleteAsset(asset);
            ResourceMgr::Get().Tick(0.0f);
            ResourceMgr::Get().SaveAllUnsavedAssets();
            _is_dirty = true;
        }

        bool AssetBrowser::CreateFolderEntry(const String &name)
        {
            const String trimmed_name = TrimNameCopy(name);
            if (trimmed_name.empty())
                return false;

            const fs::path new_folder_path = _current_path / fs::path(ToWChar(trimmed_name.c_str()));
            if (fs::exists(new_folder_path))
                return false;

            FileManager::CreateDirectory(new_folder_path.wstring());
            _is_dirty = true;
            _is_directory_tree_dirty = true;
            return fs::exists(new_folder_path);
        }

        bool AssetBrowser::CreateSceneEntry(const String &name)
        {
            const String trimmed_name = TrimNameCopy(name);
            if (trimmed_name.empty())
                return false;

            const WString asset_path = BuildCurrentAssetPath(ToWChar(trimmed_name.c_str()) + WString(L".almap"));
            if (ResourceMgr::Get().GetAsset(asset_path) != nullptr || fs::exists(ResourceMgr::GetResSysPath(asset_path)))
                return false;

            auto scene = SceneManagement::SceneMgr::Get().Create(trimmed_name);
            if (!scene)
                return false;

            ResourceMgr::Get().CreateAsset(asset_path, scene);
            ResourceMgr::Get().SaveAllUnsavedAssets();
            _is_dirty = true;
            return true;
        }

        bool AssetBrowser::CreateMaterialEntry(const String &name, Render::Shader *shader)
        {
            const String trimmed_name = TrimNameCopy(name);
            if (trimmed_name.empty())
                return false;

            const WString asset_path = BuildCurrentAssetPath(ToWChar(trimmed_name.c_str()) + WString(L".alasset"));
            if (ResourceMgr::Get().GetAsset(asset_path) != nullptr || fs::exists(ResourceMgr::GetResSysPath(asset_path)))
                return false;

            if (!shader)
                return false;

            Ref<Render::Material> material = nullptr;
            if (shader == Render::Shader::s_p_defered_standart_lit.lock().get() || shader->Name() == "defered_standard_lit")
                material = MakeRef<Render::StandardMaterial>(trimmed_name);
            else
                material = MakeRef<Render::Material>(shader, trimmed_name);
            ResourceMgr::Get().CreateAsset(asset_path, material);
            ResourceMgr::Get().SaveAllUnsavedAssets();
            _is_dirty = true;
            return true;
        }

        WString AssetBrowser::CurrentAssetDirectoryPath() const
        {
            return GetRelativeAssetDirectory(_current_path);
        }

        WString AssetBrowser::BuildCurrentAssetPath(const WString &file_name) const
        {
            return AppendChildAssetPath(CurrentAssetDirectoryPath(), file_name);
        }

        Vector<Asset *> AssetBrowser::CollectAssetsUnderDirectory(const WString &directory_asset_path) const
        {
            Vector<Asset *> assets;
            const WString normalized_dir = NormalizeLogicalPathWithoutTrailingSlash(directory_asset_path);
            const WString normalized_prefix = NormalizeLogicalDirectoryPath(normalized_dir);
            for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); ++it)
            {
                Asset *asset = it->second.get();
                WString asset_path = NormalizeLogicalPathWithoutTrailingSlash(asset->_asset_path);
                if (asset_path == normalized_dir || asset_path.compare(0, normalized_prefix.size(), normalized_prefix) == 0)
                    assets.push_back(asset);
            }
            return assets;
        }

        String AssetBrowser::MakeUniqueEntryName(const WString &directory_sys_path, const String &base_name, const WString &extension, bool is_directory) const
        {
            const String fallback_name = is_directory ? "NewFolder" : "NewAsset";
            const String seed = TrimNameCopy(base_name).empty() ? fallback_name : TrimNameCopy(base_name);
            String candidate = seed;
            u32 suffix = 1u;
            while (true)
            {
                WString file_name = ToWChar(candidate.c_str());
                if (!is_directory)
                    file_name += extension;
                const fs::path candidate_path = fs::path(directory_sys_path) / fs::path(file_name);
                if (!fs::exists(candidate_path))
                    return candidate;
                candidate = std::format("{}_{}", seed, suffix++);
            }
        }
    }// namespace Editor
}// namespace Ailu
