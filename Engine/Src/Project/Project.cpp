#include "Project/Project.h"

#include "Framework/Common/FileManager.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Path.h"
#include "Objects/JsonArchive.h"
#include "Objects/Serialize.h"

namespace Ailu
{
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
        Type *type = ProjectDescriptor::StaticType();
        for (auto &property: type->GetProperties())
            property.Deserialize(&descriptor, ar);

        _descriptor = std::move(descriptor);
        _project_file_path = normalized_project_file;
        _root_directory = PathUtils::NormalizeDirectoryPath(PathUtils::Parent(_project_file_path));
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
        Type *type = ProjectDescriptor::StaticType();
        for (auto &property: type->GetProperties())
            property.Serialize(const_cast<ProjectDescriptor *>(&_descriptor), ar);
        ar.Save(_project_file_path);
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
