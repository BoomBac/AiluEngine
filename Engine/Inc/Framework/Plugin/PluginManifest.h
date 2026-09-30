#pragma once
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Platform/Api.h"
#include "Framework/Plugin/PluginApi.h"

namespace Ailu
{
    // Engine-owned discovery data. Scanning never loads a DLL or accesses reflection/assets.
    struct PluginManifest
    {
        String _id;
        String _name;
        String _version;
        u32 _api_version = kPluginApiVersion;
        EPluginType _type = EPluginType::kRuntime;
        WString _library_path;
        WString _manifest_path;
        bool _autoload = false;
    };

    AILU_API Vector<PluginManifest> ScanPluginManifests(const Vector<WString>& roots);
}
