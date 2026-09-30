#include "Framework/Plugin/PluginManifest.h"
#include "Framework/Common/Log.h"
#include "Ext/rapidjson/inc/document.h"
#include "Ext/rapidjson/inc/error/en.h"
#include "Ext/rapidjson/inc/istreamwrapper.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace Ailu
{
    namespace
    {
        namespace fs = std::filesystem;

        bool ReadManifest(const fs::path& path, PluginManifest& manifest)
        {
            auto reject = [&path](const String& reason)
            {
                LOG_ERROR(L"[Plugin] Manifest ignored: '{}': {}", path.wstring(), ToWChar(reason));
                return false;
            };
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return reject("cannot open file");
            rapidjson::IStreamWrapper stream(file);
            rapidjson::Document document;
            document.ParseStream<rapidjson::kParseValidateEncodingFlag>(stream);
            if (document.HasParseError())
                return reject(String(rapidjson::GetParseError_En(document.GetParseError()))
                    + " at byte " + std::to_string(document.GetErrorOffset()));
            if (!document.IsObject())
                return reject("expected JSON object");
            const char* fields[] = {"id", "name", "version", "api_version", "type", "library", "autoload"};
            if (document.MemberCount() != std::size(fields))
                return reject("expected exactly the seven v1 fields");
            for (const auto* field : fields)
            {
                u32 matches = 0;
                for (auto member = document.MemberBegin(); member != document.MemberEnd(); ++member)
                    if (member->name.GetStringLength() == std::strlen(field)
                        && std::memcmp(member->name.GetString(), field, std::strlen(field)) == 0)
                        ++matches;
                if (matches != 1)
                    return reject(String("missing or duplicate field: ") + field);
            }
            for (const auto* field : {"id", "name", "version", "type", "library"})
            {
                const auto& value = document[field];
                if (!value.IsString() || value.GetStringLength() == 0
                    || std::strlen(value.GetString()) != value.GetStringLength())
                    return reject(String("expected nonempty string without NUL: ") + field);
            }
            if (!document["api_version"].IsUint() || !document["autoload"].IsBool())
                return reject("api_version must be uint32; autoload must be boolean");
            if (document["api_version"].GetUint() != kPluginApiVersion)
                return reject("unsupported manifest api_version=" + std::to_string(document["api_version"].GetUint()));
            const String type = document["type"].GetString();
            if (type != "runtime" && type != "editor")
                return reject("type must be runtime or editor");
            const String library = document["library"].GetString();
            const auto relative_library = fs::u8path(library).lexically_normal();
            if (relative_library.empty() || relative_library == "." || relative_library.has_root_path()
                || *relative_library.begin() == ".." || library.find(':') != String::npos)
                return reject("library must stay relative to the manifest directory");
            const auto directory = fs::weakly_canonical(path.parent_path());
            const auto library_path = fs::weakly_canonical(directory / relative_library);
            const auto contained_path = library_path.lexically_relative(directory);
            if (contained_path.empty() || *contained_path.begin() == ".." || contained_path.has_root_path())
                return reject("library resolves outside the manifest directory");
            manifest = {document["id"].GetString(), document["name"].GetString(), document["version"].GetString(),
                document["api_version"].GetUint(), type == "editor" ? EPluginType::kEditor : EPluginType::kRuntime,
                library_path.wstring(), fs::weakly_canonical(path).wstring(), document["autoload"].GetBool()};
            return true;
        }
    }

    Vector<PluginManifest> ScanPluginManifests(const Vector<WString>& roots)
    {
        Vector<PluginManifest> manifests;
        for (const auto& root : roots)
        {
            try
            {
                std::error_code error;
                if (!fs::exists(root, error) && !error)
                    continue;
                Vector<fs::path> paths;
                for (const auto& entry : fs::directory_iterator(root))
                {
                    try
                    {
                        if (entry.is_directory() && fs::is_regular_file(entry.path() / "plugin.json"))
                            paths.push_back(entry.path() / "plugin.json");
                    }
                    catch (const std::exception& exception)
                    {
                        LOG_ERROR(L"[Plugin] Scan entry ignored: '{}': {}",
                            entry.path().wstring(), ToWChar(exception.what()));
                    }
                }
                std::sort(paths.begin(), paths.end());
                for (const auto& path : paths)
                {
                    try
                    {
                        PluginManifest manifest;
                        if (!ReadManifest(path, manifest))
                            continue;
                        const auto duplicate = std::find_if(manifests.begin(), manifests.end(),
                            [&manifest](const auto& item) { return item._id == manifest._id; });
                        if (duplicate != manifests.end())
                        {
                            LOG_ERROR(L"[Plugin] Duplicate manifest id: {}; keeping='{}', skipped='{}",
                                ToWChar(manifest._id), duplicate->_manifest_path, manifest._manifest_path);
                            continue;
                        }
                        manifests.push_back(std::move(manifest));
                    }
                    catch (const std::exception& exception)
                    {
                        LOG_ERROR(L"[Plugin] Manifest ignored: '{}': {}", path.wstring(), ToWChar(exception.what()));
                    }
                }
            }
            catch (const std::exception& exception)
            {
                LOG_ERROR(L"[Plugin] Scan root failed: '{}': {}", root, ToWChar(exception.what()));
            }
        }
        return manifests;
    }
}
