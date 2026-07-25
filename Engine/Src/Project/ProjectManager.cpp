#include "Project/ProjectManager.h"

#include "Framework/Common/FileManager.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/Utils.h"

namespace Ailu
{
    namespace
    {
        constexpr const wchar_t *kProjectFileExtension = L".ailuproject";
        ProjectManager *s_project_manager = nullptr;

        bool HasDirectoryTraversal(const WString &path)
        {
            fs::path fs_path(path);
            for (const auto &part: fs_path)
            {
                if (part == L"..")
                    return true;
            }
            return false;
        }
    }

    void ProjectManager::Init()
    {
        AL_ASSERT_MSG(s_project_manager == nullptr, "ProjectManager already init!");
        s_project_manager = new ProjectManager();
    }

    void ProjectManager::Shutdown()
    {
        delete s_project_manager; s_project_manager = nullptr;
    }

    ProjectManager &ProjectManager::Get()
    {
        AL_ASSERT_MSG(s_project_manager != nullptr, "ProjectManager has not been init!");
        return *s_project_manager;
    }

    bool ProjectManager::OpenProject(const WString &project_file_path)
    {
        auto project = MakeScope<Project>();
        if (!project->Load(project_file_path))
            return false;
        if (!ValidateProject(*project))
            return false;
        if (!PrepareProjectDirectories(*project))
            return false;

        _project = std::move(project);
        LOG_INFO(L"Opened project: {}", _project->ProjectFilePath());
        return true;
    }

    bool ProjectManager::CreateProject(const WString &parent_directory, const String &project_name)
    {
        if (project_name.empty())
        {
            LOG_ERROR("ProjectManager::CreateProject: project name is empty");
            return false;
        }

        const WString normalized_parent = PathUtils::NormalizeDirectoryPath(parent_directory);
        const WString project_name_w = ToWChar(project_name);
        const WString project_root = PathUtils::NormalizeDirectoryPath((fs::path(normalized_parent) / fs::path(project_name_w)).wstring());
        const WString project_file_path = PathUtils::FormatFilePath((fs::path(project_root) / fs::path(project_name_w + kProjectFileExtension)).wstring());

        if (FileManager::Exist(project_file_path))
        {
            LOG_ERROR(L"ProjectManager::CreateProject: project file {} already exists", project_file_path);
            return false;
        }

        auto project = MakeScope<Project>();
        project->_project_file_path = project_file_path;
        project->_root_directory = project_root;
        project->_descriptor._project_name = project_name;
        project->_descriptor._project_guid = Guid::Generate();

        if (!ValidateProject(*project))
            return false;
        if (!PrepareProjectDirectories(*project))
            return false;
        if (!project->Save())
            return false;

        _project = std::move(project);
        LOG_INFO(L"Created project: {}", _project->ProjectFilePath());
        return true;
    }

    void ProjectManager::CloseProject()
    {
        _project.reset();
    }

    bool ProjectManager::ValidateProject(const Project &project) const
    {
        const auto &descriptor = project.Descriptor();
        if (project.ProjectFilePath().empty() || project.RootDirectory().empty())
        {
            LOG_ERROR("ProjectManager::ValidateProject: project path is empty");
            return false;
        }
        if (descriptor._file_version == 0u)
        {
            LOG_ERROR("ProjectManager::ValidateProject: invalid project file version");
            return false;
        }
        if (descriptor._project_name.empty())
        {
            LOG_ERROR("ProjectManager::ValidateProject: project name is empty");
            return false;
        }
        if (descriptor._project_guid == Guid::EmptyGuid() || descriptor._project_guid.ToString().empty())
        {
            LOG_ERROR("ProjectManager::ValidateProject: project guid is empty");
            return false;
        }

        const WString directories[] = {
                descriptor._asset_directory,
                descriptor._config_directory,
                descriptor._library_directory,
                descriptor._intermediate_directory,
                descriptor._saved_directory,
                descriptor._build_directory,
        };

        for (const WString &directory: directories)
        {
            if (directory.empty() || HasDirectoryTraversal(directory))
            {
                LOG_ERROR(L"ProjectManager::ValidateProject: invalid project directory {}", directory);
                return false;
            }
        }
        return true;
    }

    bool ProjectManager::PrepareProjectDirectories(const Project &project) const
    {
        try
        {
            FileManager::CreateDirectory(project.RootDirectory());
            FileManager::CreateDirectory(project.AssetDirectory());
            FileManager::CreateDirectory(project.ConfigDirectory());
            FileManager::CreateDirectory(project.LibraryDirectory());
            FileManager::CreateDirectory(project.IntermediateDirectory());
            FileManager::CreateDirectory(project.SavedDirectory());
            FileManager::CreateDirectory(project.BuildDirectory());
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("ProjectManager::PrepareProjectDirectories failed: {}", e.what());
            return false;
        }
        return true;
    }
}
