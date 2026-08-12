#pragma once
#ifndef __PREFAB_TYPES_H__
#define __PREFAB_TYPES_H__

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Math/Guid.h"
#include "generated/PrefabTypes.gen.h"

namespace Ailu::SceneManagement
{
    ASTRUCT()
    struct AILU_API PrefabOverride
    {
        GENERATED_BODY()

        APROPERTY()
        Guid _prefab_entity = Guid::EmptyGuid();
        APROPERTY()
        String _component;
        APROPERTY()
        String _property;
    };

    ASTRUCT()
    struct AILU_API PrefabInstance
    {
        GENERATED_BODY()

        APROPERTY()
        Guid _prefab_asset = Guid::EmptyGuid();
        APROPERTY()
        Guid _root_entity = Guid::EmptyGuid();
        APROPERTY()
        Vector<PrefabOverride> _overrides;
    };
}

#endif
