#pragma once

#include <mutex>
#include <unordered_set>

#include "Render/ShaderArtifact.h"

namespace Ailu::Render
{
    class AILU_API ShaderDependencyRegistry
    {
    public:
        static ShaderDependencyRegistry &Get();

        void Register(u64 artifact_key, const Vector<ShaderArtifactDependency> &dependencies);
        void Unregister(u64 artifact_key);
        Vector<u64> FindAffectedArtifacts(const WString &changed_file) const;

    private:
        mutable std::mutex _mutex;
        HashMap<WString, std::unordered_set<u64>> _dependency_to_artifacts;
        HashMap<u64, Vector<WString>> _artifact_to_dependencies;
    };
}
