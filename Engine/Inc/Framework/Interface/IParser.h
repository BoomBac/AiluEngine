#pragma once
#ifndef __IPARSER_H__
#define __IPARSER_H__
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/List.h"
#include "Animation/Clip.h"
#include "Render/Mesh.h"
#include "Render/Texture.h"
#include "generated/IParser.gen.h"

using Ailu::Render::Mesh;
using Ailu::Render::Texture2D;
using Ailu::Render::CubeMap;

namespace Ailu
{
    class SkeletonAsset;

    enum class EResourceType : u8
    {
        kStaticMesh = 0,
        kImage
    };
    enum class EMeshLoader : u8
    {
        kFbx = 0,
        kObj,
        kGltf
    };
    enum class EImageLoader : u8
    {
        kPNG = 0,
        kJPEG,
        kTGA,
        kHDR,
        kDDS
    };
    ASTRUCT()
    struct AILU_API ImportSetting
    {
        GENERATED_BODY()

    public:
        static ImportSetting &Default()
        {
            static ImportSetting s_default;
            return s_default;
        }
        String _name_id;
        bool _is_copy = true;
        bool _is_reimport = false;
        ImportSetting() = default;
        ImportSetting(String name_id, bool is_copy = true)
        {
            _name_id = name_id;
            _is_copy = is_copy;
        }
        virtual ~ImportSetting() = default;
        //virtual void* operator new(size_t size) = 0;
        //virtual void* operator new[](size_t size) = 0;
        //virtual void operator delete(void* ptr) = 0;
        //virtual void operator delete[](void* ptr) = 0;)
    };

    // 默认纹理导入设置：sRGB(伽马空间)，生成 mipmap，不可读
    ASTRUCT()
    struct AILU_API TextureImportSetting : public ImportSetting
    {
        GENERATED_BODY()

    public:
        static TextureImportSetting &Default()
        {
            static TextureImportSetting s_default;
            return s_default;
        }
        APROPERTY(Category = "Texture"; Order = 0)
        bool _is_sRGB = true;
        APROPERTY(Category = "Texture"; Order = 1)
        bool _generate_mipmap = true;
        APROPERTY(Category = "Texture"; Order = 2)
        bool _is_readable = false;
    };
    ASTRUCT()
    struct AILU_API MeshImportSetting : public ImportSetting
    {
        GENERATED_BODY()

    public:
        inline static u8 kImportFlagMesh = 1;
        inline static u8 kImportFlagAnimation = 2;
        inline static u8 kImportFlagMaterial = 4;
        inline static u8 kImportFlagSkeleton = 8;
        static MeshImportSetting &Default()
        {
            static MeshImportSetting s_default;
            return s_default;
        }
        MeshImportSetting() = default;
        MeshImportSetting(String name_id, bool is_copy, bool is_import_material = true) : ImportSetting(name_id, is_copy)
        {
            _name_id = name_id;
            if (is_import_material)
                _import_flag |= kImportFlagMaterial;
        }
        //此项为true时会导入fbx中的材质信息并生成材质资产,false只会导入材质信息，并在运行时生成材质
        APROPERTY(Category = "Mesh"; Order = 0)
        bool _is_recalculate_normals = false;
        //1 is mesh,2 is animation,4 is material,8 is skeleton
        APROPERTY(Category = "Mesh"; Order = 1)
        u8 _import_flag = kImportFlagMesh;
        APROPERTY(Category = "Mesh"; Order = 2)
        bool _is_combine_mesh = false;
        APROPERTY(Hidden = true)
        String _mesh_name;//指定fbx的某个mesh进行导入，如果为空则导入所有mesh
        APROPERTY(Category = "Animation"; Order = 0)
        i32 _animation_stack_index = 0;//默认只导入第一个 AnimationStack
        APROPERTY(Category = "Mesh"; Order = 3)
        Guid _skeleton = Guid::EmptyGuid();

        bool ShouldImportMesh() const
        {
            return (_import_flag & kImportFlagMesh) != 0u;
        }

        bool ShouldImportAnimation() const
        {
            return (_import_flag & kImportFlagAnimation) != 0u;
        }

        bool ShouldImportMaterial() const
        {
            return (_import_flag & kImportFlagMaterial) != 0u;
        }

        bool ShouldImportSkeleton() const
        {
            return (_import_flag & kImportFlagSkeleton) != 0u;
        }
    };
    struct AILU_API ShaderImportSetting : public ImportSetting
    {
    public:
        static ShaderImportSetting &Default()
        {
            static ShaderImportSetting s_default;
            return s_default;
        }
        String _vs_entry, _ps_entry;
        String _cs_kernel;
    };
    class IMeshParser
    {
    public:
        virtual ~IMeshParser() = default;
        virtual void Parser(const WString &sys_path, const MeshImportSetting &import_setting) = 0;
        virtual const List<Ref<AnimationClip>> &GetAnimationClips() const = 0;
        virtual void GetMeshes(List<Ref<Mesh>> &out_mesh) = 0;
        virtual Ref<SkeletonAsset> GetSkeletonAsset() const { return nullptr; }
    };

    struct TextureLoadData
    {
        u16 _width;
        u16 _height;
        Vector<u8*> _data;
        Render::ETextureFormat _format;
        ~TextureLoadData()
        {
            for (auto &d : _data)
            {
                AL_FREE(d);
            }
        }
    };
    class ITextureParser
    {
    public:
        virtual ~ITextureParser() = default;
        virtual Ref<CubeMap> Parser(Vector<String> &paths,const TextureImportSetting& import_settings) = 0;
        virtual Ref<Texture2D> Parser(const WString &sys_path,const TextureImportSetting& import_settings) = 0;
        virtual bool Parser(const WString &sys_path,Ref<Texture2D>& texture,const TextureImportSetting& import_settings) = 0;
    protected:
        virtual bool LoadTextureData(const WString &sys_path,TextureLoadData& data) = 0;
    };
}// namespace Ailu


#endif// !IPARSER_H__
