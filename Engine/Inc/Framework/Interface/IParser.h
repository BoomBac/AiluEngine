#pragma once
#ifndef __IPARSER_H__
#define __IPARSER_H__
#include <string>
#include <list>
#include "Render/Mesh.h"
#include "Render/Texture.h"
#include "GlobalMarco.h"
#include "Animation/Clip.h"

namespace Ailu
{
	enum class EResourceType : u8
	{
		kStaticMesh = 0,kImage
	};
	enum class EMeshLoader : u8
	{
		kFbx = 0,kObj
	};
	enum class EImageLoader : u8
	{
		kPNG = 0,kJPEG,kTGA,kHDR,kDDS
	};
    struct AILU_API ImportSetting
    {
    public:
        static ImportSetting &Default()
        {
            static ImportSetting s_default;
            return s_default;
        }
        String _name_id;
        bool _is_copy = true;
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

    struct AILU_API TextureImportSetting : public ImportSetting
    {
    public:
        static TextureImportSetting &Default()
        {
            static TextureImportSetting s_default;
            return s_default;
        }
        bool _is_srgb = false;
        bool _generate_mipmap = true;
    };
    struct AILU_API MeshImportSetting : public ImportSetting
    {
    public:
        inline static u8 kImportFlagMesh = 1;
        inline static u8 kImportFlagAnimation = 2;
        static MeshImportSetting &Default()
        {
            static MeshImportSetting s_default;
            return s_default;
        }
        MeshImportSetting() = default;
        MeshImportSetting(String name_id, bool is_copy, bool is_import_material = true) : ImportSetting(name_id, is_copy)
        {
            _name_id = name_id;
            _is_import_material = is_import_material;
        }
        bool _is_import_material = true;
        //1 is mesh,2 is animation
        u8 _import_flag = 1;
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
        virtual void Parser(const WString &sys_path, const MeshImportSetting& import_setting) = 0;
		virtual const List<ImportedMaterialInfo>& GetImportedMaterialInfos() const = 0;
        virtual const List<Ref<AnimationClip>> &GetAnimationClips() const = 0;
        virtual const List<Ref<Mesh>> &GetMeshes() const = 0;
	};

	class ITextureParser
	{
	public:
		virtual ~ITextureParser() = default;
		virtual Ref<CubeMap> Parser(Vector<String>& paths) = 0;
		virtual Ref<Texture2D> Parser(const WString& sys_path) = 0;
	};
}


#endif // !IPARSER_H__

