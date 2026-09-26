#include "pch.h"

#include "Render/ShaderDependencyRegistry.h"
#include "Render/ShaderDependencyPath.h"

namespace Ailu::Render
{
    ShaderDependencyRegistry &ShaderDependencyRegistry::Get()
    {
        static ShaderDependencyRegistry s_registry;
        return s_registry;
    }

    void ShaderDependencyRegistry::Register(u64 artifact_key, const Vector<ShaderArtifactDependency> &dependencies)
    {
        std::lock_guard lock(_mutex);
        const auto old_it = _artifact_to_dependencies.find(artifact_key);
        if (old_it != _artifact_to_dependencies.end())
        {
            for (const auto &path : old_it->second)
            {
                auto dependency_it = _dependency_to_artifacts.find(path);
                if (dependency_it == _dependency_to_artifacts.end())
                    continue;
                dependency_it->second.erase(artifact_key);
                if (dependency_it->second.empty())
                    _dependency_to_artifacts.erase(dependency_it);
            }
            _artifact_to_dependencies.erase(old_it);
        }
        auto &paths = _artifact_to_dependencies[artifact_key];
        for (const auto &dependency : dependencies)
        {
            const WString path = NormalizeShaderDependencyPath(dependency._path);
            paths.emplace_back(path);
            _dependency_to_artifacts[path].insert(artifact_key);
        }
    }

    void ShaderDependencyRegistry::Unregister(u64 artifact_key)
    {
        const auto artifact_it = _artifact_to_dependencies.find(artifact_key);
        if (artifact_it == _artifact_to_dependencies.end())
            return;
        for (const auto &path : artifact_it->second)
        {
            auto dependency_it = _dependency_to_artifacts.find(path);
            if (dependency_it == _dependency_to_artifacts.end())
                continue;
            dependency_it->second.erase(artifact_key);
            if (dependency_it->second.empty())
                _dependency_to_artifacts.erase(dependency_it);
        }
        _artifact_to_dependencies.erase(artifact_it);
    }

    Vector<u64> ShaderDependencyRegistry::FindAffectedArtifacts(const WString &changed_file) const
    {
        std::lock_guard lock(_mutex);
        Vector<u64> result;
        const auto it = _dependency_to_artifacts.find(NormalizeShaderDependencyPath(changed_file));
        if (it == _dependency_to_artifacts.end())
            return result;
        result.assign(it->second.begin(), it->second.end());
        return result;
    }
}
