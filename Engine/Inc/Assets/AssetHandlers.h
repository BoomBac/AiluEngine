#pragma once
#include "AssetCommon.h"

namespace Ailu
{
    // ============================================================
    // Asset Handlers
    // ============================================================

    class AILU_API SpriteAssetHandler : public IAssetHandler
    {
        const Type *AssetType() const final;
        Scope<Asset> Load(const AssetLoadContext &context) final;
        bool Save(const AssetSaveContext &context) final;
    };

    class AILU_API ShaderAssetHandler : public IAssetHandler
    {
        const Type *AssetType() const final;
        Scope<Asset> Load(const AssetLoadContext &context) final;
        bool Save(const AssetSaveContext &context) final;
    };

    class AILU_API ComputeShaderAssetHandler : public IAssetHandler
    {
        const Type *AssetType() const final;
        Scope<Asset> Load(const AssetLoadContext &context) final;
        bool Save(const AssetSaveContext &context) final;
    };

    class AILU_API TextureAssetHandler : public IAssetHandler
    {
        const Type *AssetType() const final;
        Scope<Asset> Load(const AssetLoadContext &context) final;
        bool Save(const AssetSaveContext &context) final;
    };

    class AILU_API MaterialAssetHandler : public IAssetHandler
    {
        const Type *AssetType() const final;
        Scope<Asset> Load(const AssetLoadContext &context) final;
        bool Save(const AssetSaveContext &context) final;
    };

    class AILU_API MeshAssetHandler : public IAssetHandler
    {
        const Type *AssetType() const final;
        Scope<Asset> Load(const AssetLoadContext &context) final;
        bool Save(const AssetSaveContext &context) final;
    };

    class AILU_API SkeletonMeshAssetHandler : public IAssetHandler
    {
        const Type *AssetType() const final;
        Scope<Asset> Load(const AssetLoadContext &context) final;
        bool Save(const AssetSaveContext &context) final;
    };

    class AILU_API SceneAssetHandler : public IAssetHandler
    {
        const Type *AssetType() const final;
        Scope<Asset> Load(const AssetLoadContext &context) final;
        bool Save(const AssetSaveContext &context) final;
    };

    class AILU_API AnimationClipAssetHandler : public IAssetHandler
    {
        const Type *AssetType() const final;
        Scope<Asset> Load(const AssetLoadContext &context) final;
        bool Save(const AssetSaveContext &context) final;
    };

    class AILU_API InputActionAssetHandler : public IAssetHandler
    {
        const Type *AssetType() const final;
        Scope<Asset> Load(const AssetLoadContext &context) final;
        bool Save(const AssetSaveContext &context) final;
    };
}
