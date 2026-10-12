#pragma once
#ifndef __ASSET_IMPORT_CONTROLLER_H__
#define __ASSET_IMPORT_CONTROLLER_H__
#include "Common/EditorProgressService.h"
#include "Framework/Math/ALMath.hpp"
#include <filesystem>
#include <functional>
#include <memory>

namespace Ailu
{
    struct ImportSetting;
    namespace fs = std::filesystem;
    namespace Editor
    {
        // Main-thread-only queue; each item retains its destination across Browser directory changes.
        // A single ImportResource call remains synchronous. Tick yields between files, not inside import.
        class AssetImportController
        {
        public:
            using OnImportedCallback = std::function<void()>;
            using ImportCallback = std::function<bool(const WString &, const WString &, const ImportSetting &)>;
            explicit AssetImportController(EditorProgressService &service, ImportCallback import_resource = {});
            ~AssetImportController();
            AssetImportController(const AssetImportController &) = delete;
            AssetImportController &operator=(const AssetImportController &) = delete;
            void SetOnImported(OnImportedCallback callback) { _on_imported = std::move(callback); }
            void QueueFiles(const Vector<WString> &files, const fs::path &target_directory, Vector2f popup_pos);
            void Tick();
        private:
            struct PendingImport { WString _sys_path; fs::path _target_directory; };
            void ProcessNext();
            void ShowImportDialog(const PendingImport &item);
            void ImportCurrent(const ImportSetting &setting);
            void FinishCurrent(bool success, bool skipped = false);
            void FinishBatch(bool cancelled = false);
            void CancelQueued();
            void NotifyImported();
            String Summary() const;
            EditorProgressService &_service;
            EditorProgressHandle _progress;
            ImportCallback _import_resource;
            Vector<PendingImport> _queue;
            Vector2f _popup_pos = Vector2f::kZero;
            OnImportedCallback _on_imported;
            std::shared_ptr<int> _lifetime = std::make_shared<int>(0);
            u64 _popup_group_id = 0;
            u64 _total = 0, _succeeded = 0, _skipped = 0, _failed = 0;
            bool _prepared = false;
            bool _waiting_for_settings = false;
            String _last_error;
        };
    }
}
#endif
