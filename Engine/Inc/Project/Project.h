#ifndef __PROJECT_H__
#define __PROJECT_H__

#include "ProjectDescriptor.h"
#include "ProjectSettings.h"

namespace Ailu
{
    class AILU_API Project
    {
    public:
        bool Load(const WString& project_file_path);
        bool Save() const;

        const ProjectDescriptor& Descriptor() const
        {
            return _descriptor;
        }

        ProjectSettings &Settings() { return _settings; }
        const ProjectSettings &Settings() const { return _settings; }

        const WString& ProjectFilePath() const
        {
            return _project_file_path;
        }

        const WString& RootDirectory() const
        {
            return _root_directory;
        }

        WString AssetDirectory() const;
        WString ConfigDirectory() const;
        WString LibraryDirectory() const;
        WString IntermediateDirectory() const;
        WString SavedDirectory() const;
        WString BuildDirectory() const;
        WString ResolvePath(const WString& relative_path) const;

    private:
        friend class ProjectManager;

        ProjectDescriptor _descriptor;
        ProjectSettings _settings;
        WString _project_file_path;
        WString _root_directory;
    };
}
#endif
