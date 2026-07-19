#ifndef __PROJECT_MGR_H__
#define __PROJECT_MGR_H__
#include "Project.h"

namespace Ailu
{
    class AILU_API ProjectManager
    {
    public:
        static void Init();
        static void Shutdown();
        static ProjectManager& Get();

        bool OpenProject(const WString& project_file_path);
        bool CreateProject(const WString& parent_directory,const String& project_name);

        void CloseProject();

        bool HasOpenedProject() const
        {
            return _project != nullptr;
        }

        Project& CurrentProject()
        {
            AL_ASSERT(_project != nullptr);
            return *_project;
        }

        const Project& CurrentProject() const
        {
            AL_ASSERT(_project != nullptr);
            return *_project;
        }

    private:
        bool ValidateProject(const Project& project) const;
        bool PrepareProjectDirectories(const Project& project) const;

    private:
        Scope<Project> _project;
    };
}

#endif