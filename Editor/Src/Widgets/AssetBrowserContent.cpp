#include "Widgets/AssetBrowserContent.h"

#include "Assets/Asset.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "Project/ProjectManager.h"

#include <algorithm>
#include <cctype>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
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
                        roots.push_back({EAssetDomain::kProject, "ProjAssets", fs::path(project_asset_root)});
                    }
                }

                roots.push_back({EAssetDomain::kEngine, "EngineAssets", fs::path(ResourceMgr::Get().EngineResRootPath())});
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
        }// namespace

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

        WString AppendChildAssetPath(const WString &directory_asset_path, const WString &file_name)
        {
            if (directory_asset_path.empty())
                return file_name;
            return NormalizeLogicalDirectoryPath(directory_asset_path) + file_name;
        }

        Vector<AssetBrowserEntry> AssetBrowserContent::Query(const fs::path &directory, const String &search_text) const
        {
            Vector<AssetBrowserEntry> entries;
            if (directory.empty() || !fs::exists(directory) || !fs::is_directory(directory))
                return entries;

            const EAssetDomain domain = GetDomainForPath(directory);
            const WString asset_directory = GetRelativeAssetDirectory(directory);
            const bool is_searching = !search_text.empty();

            Vector<fs::directory_entry> directories;
            std::error_code directory_error;
            for (fs::directory_iterator dir_it(directory, directory_error); !directory_error && dir_it != fs::directory_iterator(); dir_it.increment(directory_error))
            {
                if (dir_it->is_directory(directory_error) && ContainsSearchText(dir_it->path().filename().string(), search_text))
                    directories.push_back(*dir_it);
            }
            std::sort(directories.begin(), directories.end(), [](const fs::directory_entry &lhs, const fs::directory_entry &rhs)
            {
                return lhs.path().filename().wstring() < rhs.path().filename().wstring();
            });
            for (auto &dir_entry: directories)
            {
                AssetBrowserEntry entry;
                entry._type = AssetBrowserEntry::EType::kFolder;
                entry._display_name = dir_entry.path().filename().string();
                entry._sys_path = dir_entry.path();
                entries.push_back(std::move(entry));
            }

            Vector<Asset *> assets;
            for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); ++it)
            {
                Asset *asset = it->second.get();
                if (asset->_domain != domain)
                    continue;

                const WString asset_dir = ExtractLogicalAssetDirectory(asset->_asset_path);
                if (!IsAssetInDirectory(asset_dir, asset_directory, is_searching))
                    continue;

                const String asset_display_name = asset->_p_obj ? asset->_p_obj->Name() : asset->Name();
                if (is_searching && !ContainsSearchText(asset_display_name, search_text) && !ContainsSearchText(ToChar(asset->_asset_path.c_str()), search_text))
                    continue;
                assets.push_back(asset);
            }
            std::sort(assets.begin(), assets.end(), [](const Asset *lhs, const Asset *rhs)
            {
                return lhs->Name() < rhs->Name();
            });
            for (Asset *asset: assets)
            {
                AssetBrowserEntry entry;
                entry._type = AssetBrowserEntry::EType::kAsset;
                entry._display_name = asset->_p_obj ? asset->_p_obj->Name() : asset->Name();
                entry._sys_path = fs::path(ResourceMgr::GetResSysPath(asset->_asset_path));
                entry._asset = asset;
                entries.push_back(std::move(entry));
            }
            return entries;
        }

        Vector<AssetBrowserEntry> AssetBrowserContent::GetSubAssets(const Asset *owner) const
        {
            Vector<AssetBrowserEntry> entries;
            if (owner == nullptr)
                return entries;

            const WString owner_path = NormalizeLogicalPathWithoutTrailingSlash(owner->_asset_path);
            const fs::path owner_system_path = fs::path(ResourceMgr::GetResSysPath(owner->_asset_path));
            for (const auto &sub_asset: ResourceMgr::Get().GetSubAssets())
            {
                if (NormalizeLogicalPathWithoutTrailingSlash(ResourceMgr::Get().GuidToAssetPath(sub_asset._guid)) != owner_path)
                    continue;

                AssetBrowserEntry entry;
                entry._type = AssetBrowserEntry::EType::kSubAsset;
                entry._display_name = sub_asset._name;
                entry._sys_path = owner_system_path;
                entry._asset = const_cast<Asset *>(owner);
                entry._sub_asset_guid = sub_asset._guid;
                entry._sub_asset_type = sub_asset._type;
                entries.push_back(std::move(entry));
            }
            std::sort(entries.begin(), entries.end(), [](const AssetBrowserEntry &lhs, const AssetBrowserEntry &rhs)
            {
                return lhs._display_name < rhs._display_name;
            });
            return entries;
        }

        Vector<AssetBrowserRootDesc> AssetBrowserContent::GetRoots() const
        {
            return GetAssetBrowserRoots();
        }

        std::optional<AssetBrowserRootDesc> AssetBrowserContent::FindRoot(const fs::path &path) const
        {
            return FindRootForPath(path);
        }

        bool AssetBrowserContent::IsInsideRoot(const fs::path &path) const
        {
            return FindRootForPath(path).has_value();
        }

        EAssetDomain AssetBrowserContent::GetDomain(const fs::path &path) const
        {
            return GetDomainForPath(path);
        }

        WString AssetBrowserContent::GetAssetDirectory(const fs::path &path) const
        {
            return GetRelativeAssetDirectory(path);
        }

        Vector<Asset *> AssetBrowserContent::CollectAssetsUnderDirectory(const WString &directory_asset_path) const
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

        void DirectoryTreeDataSource::Rebuild()
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

        Vector<UI::TreeItemId> DirectoryTreeDataSource::GetRootItems() const
        {
            return _roots;
        }

        Vector<UI::TreeItemId> DirectoryTreeDataSource::GetChildren(UI::TreeItemId parent) const
        {
            if (const auto it = _nodes.find(parent); it != _nodes.end())
                return it->second._children;
            return {};
        }

        UI::TreeItemId DirectoryTreeDataSource::GetParent(UI::TreeItemId item) const
        {
            if (const auto it = _nodes.find(item); it != _nodes.end())
                return it->second._parent;
            return UI::kInvalidTreeItemId;
        }

        UI::TreeItemPresentation DirectoryTreeDataSource::GetPresentation(UI::TreeItemId item) const
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

        bool DirectoryTreeDataSource::IsValid(UI::TreeItemId item) const
        {
            return _nodes.contains(item);
        }

        fs::path DirectoryTreeDataSource::GetPath(UI::TreeItemId item) const
        {
            if (const auto it = _nodes.find(item); it != _nodes.end())
                return it->second._path;
            return {};
        }

        UI::TreeItemId DirectoryTreeDataSource::FindItemByPath(const fs::path &path) const
        {
            const WString normalized_path = NormalizeSysPath(path);
            if (const auto it = _path_to_id.find(normalized_path); it != _path_to_id.end())
                return it->second;
            return UI::kInvalidTreeItemId;
        }

        UI::TreeItemId DirectoryTreeDataSource::AddNode(const String &label, const fs::path &path, UI::TreeItemId parent, EAssetDomain domain)
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

        void DirectoryTreeDataSource::AddDirectoryChildren(UI::TreeItemId parent, const fs::path &path, EAssetDomain domain)
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
    }// namespace Editor
}// namespace Ailu
