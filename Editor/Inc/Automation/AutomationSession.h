#pragma once
#ifndef __AUTOMATION_SESSION_H__
#define __AUTOMATION_SESSION_H__

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"

#include <filesystem>

namespace Ailu
{
    namespace Editor
    {
        struct AutomationSessionInfo
        {
            u32 _pid = 0u;
            String _session_id;
            String _pipe_name;
            String _project_path;
            String _editor_version;
        };

        // Writes / reads <project>/.ailu/editor_session.json so external MCP
        // clients can discover the running editor and its named pipe.
        class AutomationSession
        {
        public:
            static std::filesystem::path SessionFilePath(const std::filesystem::path &project_root);
            static bool Save(const std::filesystem::path &project_root, const AutomationSessionInfo &info);
            static bool Load(const std::filesystem::path &project_root, AutomationSessionInfo &out);
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_SESSION_H__
