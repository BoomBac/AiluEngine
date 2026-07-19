#ifndef __PROJECT_H__
#define __PROJECT_H__

#include "ProjectDescriptor.h"

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
        WString _project_file_path;
        WString _root_directory;
    };
}
#endif
