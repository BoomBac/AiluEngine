#pragma once
#ifndef __ASSET_BROWSER_CONTENT_H__
#define __ASSET_BROWSER_CONTENT_H__

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/String.h"
#include "Framework/Math/Guid.h"
#include "UI/TreeView.h"

#include <filesystem>
#include <optional>

namespace Ailu
{
    namespace fs = std::filesystem;

    class Asset;
    class Type;
    enum class EAssetDomain : i32;

    namespace Editor
    {
        struct AssetBrowserRootDesc
        {
            EAssetDomain _domain = EAssetDomain::kEngine;
            String _label;
            fs::path _sys_path;
        };

        struct AssetBrowserEntry
        {
            enum class EType : u8
            {
                kFolder,
                kAsset,
                kSubAsset
            };

            EType _type = EType::kAsset;
            String _display_name;
            fs::path _sys_path;
            Asset *_asset = nullptr;
            Guid _sub_asset_guid = Guid::EmptyGuid();
            const Type *_sub_asset_type = nullptr;
        };

        // 逻辑路径辅助函数，供 AssetBrowser / AssetBrowserOperations 共用。
        WString FormatLogicalAssetPath(WString path);
        WString NormalizeLogicalPathWithoutTrailingSlash(const WString &path);
        WString NormalizeLogicalDirectoryPath(const WString &path);
        WString StripLogicalAssetPathScheme(const WString &path);
        WString AppendChildAssetPath(const WString &directory_asset_path, const WString &file_name);

        class AssetBrowserContent
        {
        public:
            Vector<AssetBrowserEntry> Query(const fs::path &directory, const String &search_text) const;

            Vector<AssetBrowserEntry> GetSubAssets(const Asset *owner) const;

            Vector<AssetBrowserRootDesc> GetRoots() const;

            std::optional<AssetBrowserRootDesc> FindRoot(const fs::path &path) const;

            bool IsInsideRoot(const fs::path &path) const;

            EAssetDomain GetDomain(const fs::path &path) const;

            WString GetAssetDirectory(const fs::path &path) const;

            Vector<Asset *> CollectAssetsUnderDirectory(const WString &directory_asset_path) const;
        };

        class DirectoryTreeDataSource final : public UI::ITreeViewDataSource
        {
        public:
            void Rebuild();

            Vector<UI::TreeItemId> GetRootItems() const override;
            Vector<UI::TreeItemId> GetChildren(UI::TreeItemId parent) const override;
            UI::TreeItemId GetParent(UI::TreeItemId item) const override;
            UI::TreeItemPresentation GetPresentation(UI::TreeItemId item) const override;
            bool IsValid(UI::TreeItemId item) const override;

            fs::path GetPath(UI::TreeItemId item) const;
            UI::TreeItemId FindItemByPath(const fs::path &path) const;

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

            UI::TreeItemId AddNode(const String &label, const fs::path &path, UI::TreeItemId parent, EAssetDomain domain);
            void AddDirectoryChildren(UI::TreeItemId parent, const fs::path &path, EAssetDomain domain);

            UI::TreeItemId _next_id = 1u;
            Vector<UI::TreeItemId> _roots;
            HashMap<UI::TreeItemId, DirectoryNode> _nodes;
            HashMap<WString, UI::TreeItemId> _path_to_id;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__ASSET_BROWSER_CONTENT_H__
