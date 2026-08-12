#pragma once
#ifndef __PREFAB_ASSET_H__
#define __PREFAB_ASSET_H__

#include "Assets/AssetDocument.h"
#include "generated/PrefabAsset.gen.h"

namespace Ailu
{
    ASTRUCT()
    struct AILU_API PrefabEntityDocument
    {
        GENERATED_BODY()

        // Stable identity inside the Prefab asset. It is never a Scene entity GUID.
        APROPERTY()
        Guid _guid = Guid::EmptyGuid();
        APROPERTY()
        Guid _parent = Guid::EmptyGuid();
        APROPERTY()
        String _name;
        APROPERTY()
        bool _enabled = true;
        // Reuses the established scene component schema and stores no runtime ComponentTypeId.
        APROPERTY()
        SceneEntityDocument _entity;
    };

    ACLASS()
    class AILU_API PrefabAssetDocument : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        AssetDocumentHeader _header;
        APROPERTY()
        Guid _root = Guid::EmptyGuid();
        APROPERTY()
        Vector<PrefabEntityDocument> _entities;
    };
}

#endif
