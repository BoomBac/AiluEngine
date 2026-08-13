#include "Project/Project.h"

#include "Framework/Common/FileManager.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Path.h"
#include "Objects/JsonArchive.h"
#include "Objects/Serialize.h"

namespace Ailu
{
    namespace
    {
        constexpr const wchar_t *kProjectSettingsFileName = L"ProjectSettings.json";

        WString GetProjectSettingsPath(const Project &project)
        {
            return PathUtils::FormatFilePath((fs::path(project.ConfigDirectory()) / kProjectSettingsFileName).wstring());
        }
    }

    bool Project::Load(const WString &project_file_path)
    {
        const WString normalized_project_file = PathUtils::FormatFilePath(project_file_path);
        if (!FileManager::Exist(normalized_project_file))
        {
            LOG_ERROR(L"Project::Load: project file {} not exist", normalized_project_file);
            return false;
        }

        JsonArchive ar;
        ar.Load(normalized_project_file);
        if (!ar.IsLoaded())
            return false;

        ProjectDescriptor descriptor;
        const Type *type = ProjectDescriptor::StaticType();
        for (auto &property: type->GetProperties())
            property.Deserialize(&descriptor, ar);

        _descriptor = std::move(descriptor);
        _project_file_path = normalized_project_file;
        _root_directory = PathUtils::NormalizeDirectoryPath(PathUtils::Parent(_project_file_path));

        const WString settings_path = GetProjectSettingsPath(*this);
        if (FileManager::Exist(settings_path))
        {
            JsonArchive settings_archive;
            settings_archive.Load(settings_path);
            if (!settings_archive.IsLoaded())
                return false;
            const Type *settings_type = ProjectSettings::StaticType();
            for (auto &property: settings_type->GetProperties())
                property.Deserialize(&_settings, settings_archive);
        }
        _settings.EnsureValid();
        return true;
    }

    bool Project::Save() const
    {
        if (_project_file_path.empty())
        {
            LOG_ERROR("Project::Save: project file path is empty");
            return false;
        }

        JsonArchive ar;
        const Type *type = ProjectDescriptor::StaticType();
        for (auto &property: type->GetProperties())
            property.Serialize(const_cast<ProjectDescriptor *>(&_descriptor), ar);
        ar.Save(_project_file_path);

        JsonArchive settings_archive;
        const Type *settings_type = ProjectSettings::StaticType();
        for (auto &property: settings_type->GetProperties())
            property.Serialize(const_cast<ProjectSettings *>(&_settings), settings_archive);
        settings_archive.Save(GetProjectSettingsPath(*this));
        return true;
    }

    WString Project::AssetDirectory() const
    {
        return ResolvePath(PathUtils::NormalizeRelativeDirectory(_descriptor._asset_directory));
    }

    WString Project::ConfigDirectory() const
    {
        return ResolvePath(PathUtils::NormalizeRelativeDirectory(_descriptor._config_directory));
    }

    WString Project::LibraryDirectory() const
    {
        return ResolvePath(PathUtils::NormalizeRelativeDirectory(_descriptor._library_directory));
    }

    WString Project::IntermediateDirectory() const
    {
        return ResolvePath(PathUtils::NormalizeRelativeDirectory(_descriptor._intermediate_directory));
    }

    WString Project::SavedDirectory() const
    {
        return ResolvePath(PathUtils::NormalizeRelativeDirectory(_descriptor._saved_directory));
    }

    WString Project::BuildDirectory() const
    {
        return ResolvePath(PathUtils::NormalizeRelativeDirectory(_descriptor._build_directory));
    }

    WString Project::ResolvePath(const WString &relative_path) const
    {
        if (relative_path.empty())
            return relative_path;
        if (PathUtils::IsSystemPath(relative_path))
            return PathUtils::FormatFilePath(relative_path);
        return PathUtils::FormatFilePath((fs::path(_root_directory) / fs::path(relative_path)).wstring());
    }
}
