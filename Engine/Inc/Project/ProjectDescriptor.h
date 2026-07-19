#pragma once

#include "Framework/Math/Guid.h"
#include "generated/ProjectDescriptor.gen.h"

namespace Ailu
{
    ASTRUCT()
    struct AILU_API ProjectDescriptor
    {
        GENERATED_BODY()

        APROPERTY()
        u32 _file_version = 1u;
        APROPERTY()
        String _engine_version;
        APROPERTY()
        String _project_name;
        APROPERTY()
        Guid _project_guid;
        APROPERTY()
        String _company_name;
        APROPERTY()
        WString _startup_scene;
        APROPERTY()
        WString _asset_directory = L"Assets";
        APROPERTY()
        WString _config_directory = L"Config";
        APROPERTY()
        WString _library_directory = L"Library";
        APROPERTY()
        WString _intermediate_directory = L"Intermediate";
        APROPERTY()
        WString _saved_directory = L"Saved";
        APROPERTY()
        WString _build_directory = L"Build";
    };
}// namespace Ailu