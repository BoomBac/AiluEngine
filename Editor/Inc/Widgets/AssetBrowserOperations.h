#pragma once
#ifndef __ASSET_BROWSER_OPERATIONS_H__
#define __ASSET_BROWSER_OPERATIONS_H__

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/String.h"
#include "Framework/Math/ALMath.hpp"
#include "Scene/Entity.h"

#include <filesystem>
#include <optional>

namespace Ailu
{
    namespace fs = std::filesystem;

    class Asset;

    namespace Render
    {
        class Shader;
    }

    namespace Editor
    {
        // 名称校验与规整。
        String TrimNameCopy(const String &value);
        std::optional<String> ValidateEntryName(const String &value);

        // 各具体 Asset 的创建逻辑，全部显式传入目标目录。
        bool CreateSceneAsset(const fs::path &directory, const String &name);
        bool CreateSpriteAsset(const fs::path &directory, const String &name);
        bool CreateMaterialAsset(const fs::path &directory, const String &name, Render::Shader *shader);
        bool CreateInputActionAsset(const fs::path &directory, const String &name);
        bool CreateAnimationClipAsset(const fs::path &directory, const String &name);
        bool CreateAnimationControllerAsset(const fs::path &directory, const String &name);
        bool CreateWidgetAsset(const fs::path &directory, const String &name);
        bool CreateFlowGraphAsset(const fs::path &directory, const String &name);
        bool CreateScriptAsset(const fs::path &directory, const String &name);
        bool CreatePrefabAsset(ECS::Entity entity, const fs::path &target_directory);

        Vector<Ref<Render::Shader>> CollectMaterialShaders();
        // 材质创建需要选择 Shader，使用自定义对话框，供 AssetTypeRegistry 注册为特殊 Creator。
        void ShowCreateMaterialDialog(Vector2f popup_pos, const fs::path &target_directory);

        class AssetBrowserOperations
        {
        public:
            bool RenameAsset(Asset *asset, const String &new_name);
            bool RenameFolder(const fs::path &folder, const String &new_name);
            bool DeleteAsset(Asset *asset);
            bool DeleteFolder(const fs::path &folder);
            bool CreateFolder(const fs::path &directory, const String &name);
            bool MoveAssets(const Vector<Asset *> &assets, const fs::path &target_directory);
            bool CopyAssets(const Vector<Asset *> &assets, const fs::path &target_directory);

            String MakeUniqueEntryName(const fs::path &directory, const String &base_name, const WString &extension, bool is_directory) const;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__ASSET_BROWSER_OPERATIONS_H__
