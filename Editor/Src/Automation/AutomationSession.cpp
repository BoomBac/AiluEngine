#include "Automation/AutomationSession.h"

#include "Automation/AutomationJson.h"

#include <fstream>
#include <sstream>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            String EscapeJsonString(StringView value)
            {
                String out;
                for (const char c : value)
                {
                    switch (c)
                    {
                        case '"': out += "\\\""; break;
                        case '\\': out += "\\\\"; break;
                        case '\n': out += "\\n"; break;
                        case '\r': out += "\\r"; break;
                        case '\t': out += "\\t"; break;
                        default: out += c; break;
                    }
                }
                return out;
            }
        }// namespace

        std::filesystem::path AutomationSession::SessionFilePath(const std::filesystem::path &project_root)
        {
            return project_root / ".ailu" / "editor_session.json";
        }

        bool AutomationSession::Save(const std::filesystem::path &project_root, const AutomationSessionInfo &info)
        {
            std::error_code ec;
            std::filesystem::create_directories(project_root / ".ailu", ec);
            if (ec)
                return false;

            std::ostringstream ss;
            ss << "{\n";
            ss << "  \"pid\": " << info._pid << ",\n";
            ss << "  \"session_id\": \"" << EscapeJsonString(info._session_id) << "\",\n";
            ss << "  \"pipe_name\": \"" << EscapeJsonString(info._pipe_name) << "\",\n";
            ss << "  \"project_path\": \"" << EscapeJsonString(info._project_path) << "\",\n";
            ss << "  \"editor_version\": \"" << EscapeJsonString(info._editor_version) << "\"\n";
            ss << "}\n";

            std::ofstream file(SessionFilePath(project_root), std::ios::trunc);
            if (!file)
                return false;
            file << ss.str();
            file.flush();
            return file.good();
        }

        bool AutomationSession::Load(const std::filesystem::path &project_root, AutomationSessionInfo &out)
        {
            const std::filesystem::path path = SessionFilePath(project_root);
            std::ifstream file(path);
            if (!file)
                return false;
            std::ostringstream ss;
            ss << file.rdbuf();

            AutomationValue root;
            if (!AutomationJson::Read(ss.str(), root) || !root.IsObject())
                return false;
            const AutomationObject &object = root.AsObject();
            const auto pid = object.find("pid");
            out._pid = pid != object.end() ? static_cast<u32>(pid->second.AsUInt()) : 0u;
            const auto session_id = object.find("session_id");
            out._session_id = session_id != object.end() ? session_id->second.AsString() : String{};
            const auto pipe_name = object.find("pipe_name");
            out._pipe_name = pipe_name != object.end() ? pipe_name->second.AsString() : String{};
            const auto project_path = object.find("project_path");
            out._project_path = project_path != object.end() ? project_path->second.AsString() : String{};
            const auto editor_version = object.find("editor_version");
            out._editor_version = editor_version != object.end() ? editor_version->second.AsString() : String{};
            return true;
        }
    }// namespace Editor
}// namespace Ailu
