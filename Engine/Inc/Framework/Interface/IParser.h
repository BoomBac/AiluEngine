#pragma once
#ifndef __IPARSER_H__
#define __IPARSER_H__
#include <string>
#include <list>
#include "Render/Mesh.h"
#include "Render/Texture.h"
#include "GlobalMarco.h"

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
	class IMeshParser
	{
	public:
		virtual ~IMeshParser() = default;
		virtual List<Ref<Mesh>> Parser(std::string_view sys_path) = 0;
		virtual List<Ref<Mesh>> Parser(const WString& sys_path) = 0;
		virtual const List<ImportedMaterialInfo>& GetImportedMaterialInfos() const = 0;
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

