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
        kHard,
        kSoft,
        kEditorOnly
    };

    ASTRUCT()
    struct AILU_API AssetDependency
    {
        GENERATED_BODY()
    public:
        APROPERTY()
        Guid _guid = Guid::EmptyGuid();

        APROPERTY()
        EAssetDependencyType _type = EAssetDependencyType::kHard;
    };

    class Asset;
    struct ImportSetting;

    struct AILU_API AssetLoadContext
    {
        class ResourceMgr *_resource_mgr = nullptr;
        WString _asset_path;
        WString _system_path;
        const ImportSetting *_import_setting = nullptr;
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
        virtual bool ReloadInPlace(Asset &target, const Asset &source);
    };

    inline constexpr u32 kSerializedAssetDocumentVersion = 1u;
}
