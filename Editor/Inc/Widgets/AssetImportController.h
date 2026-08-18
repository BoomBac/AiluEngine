#pragma once
#ifndef __ASSET_IMPORT_CONTROLLER_H__
#define __ASSET_IMPORT_CONTROLLER_H__

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/String.h"
#include "Framework/Math/ALMath.hpp"

#include <filesystem>
#include <functional>

namespace Ailu
{
    namespace fs = std::filesystem;

    namespace Editor
    {
        // 管理文件导入队列与导入弹窗状态。每个待导入项记录其目标目录，
        // 避免用户在弹窗打开期间切换 Browser 目录时导入到错误位置。
        class AssetImportController
        {
        public:
            using OnImportedCallback = std::function<void()>;

            void SetOnImported(OnImportedCallback callback) { _on_imported = std::move(callback); }

            void QueueFiles(const Vector<WString> &files, const fs::path &target_directory, Vector2f popup_pos);

        private:
            struct PendingImport
            {
                WString _sys_path;
                fs::path _target_directory;
            };

            void ProcessNext();
            void ShowImportDialog(const PendingImport &item);
            void FinishCurrent();

            void NotifyImported();

            Vector<PendingImport> _queue;
            Vector2f _popup_pos = Vector2f::kZero;
            OnImportedCallback _on_imported;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__ASSET_IMPORT_CONTROLLER_H__