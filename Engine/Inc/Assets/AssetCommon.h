#pragma once
#include "Framework/Math/Guid.h"
#include "Framework/Core/ReflectionMacros.h"
#include "generated/AssetCommon.gen.h"

namespace Ailu
{
    AENUM()
    enum class EAssetDomain
    {
        kEngine,
        kEditor,
        kProject,
        kRuntime
    };

    AENUM()
    enum class EAssetDependencyType : u8
    {
        // Runtime references resolve through handles. A dependency publish does not invalidate
        // this asset's artifact or force a reload of its runtime snapshot.
        kRuntime,
        // The dependency participates in artifact generation, but does not require a runtime refresh.
        kBuild,
        // The dependency participates in both artifact generation and runtime cache validation.
        kBuildAndRuntime,

        // Serialized projects created before the dependency split may still contain these names.
        // Keep their values stable while handlers migrate to the explicit semantics above.
        kHard = kRuntime,
        kSoft = kRuntime,
        kEditorOnly = kRuntime,
    };

    ASTRUCT()
    struct AILU_API AssetDependency
    {
        GENERATED_BODY()
        AssetDependency() = default;
        AssetDependency(Guid guid, EAssetDependencyType type) : _guid(guid), _type(type) {}

    public:
        APROPERTY()
        Guid _guid = Guid::EmptyGuid();

        APROPERTY()
        EAssetDependencyType _type = EAssetDependencyType::kRuntime;
    };

    class Asset;
    struct ImportSetting;
    class DerivedDataCache;

    struct AILU_API AssetLoadContext
    {
        class ResourceMgr *_resource_mgr = nullptr;
        WString _asset_path;
        WString _system_path;
        const ImportSetting *_import_setting = nullptr;
        DerivedDataCache *_derived_data_cache = nullptr;
        // Reload consumes an existing artifact when its key matches. Manual reimport skips that
        // lookup and rebuilds the artifact from source with the current import settings.
        bool _force_reimport = false;
    };

    struct AILU_API AssetSaveContext
    {
        class ResourceMgr *_resource_mgr = nullptr;
        WString _asset_path;
        WString _system_path;
        const Asset *_asset = nullptr;
    };

    class AILU_API IAssetHandler
    {
    public:
        virtual ~IAssetHandler() = default;

        virtual const Type *AssetType() const = 0;
        virtual Scope<Asset> Load(const AssetLoadContext &context) = 0;
        virtual bool Save(const AssetSaveContext &context) = 0;
        // ABI compatibility only. Asset updates must create a candidate snapshot and Publish it;
        // this function never performs an in-place update.
    };

    inline constexpr u32 kSerializedAssetDocumentVersion = 1u;
}
