#pragma once
#ifndef __IPARSER_H__
#define __IPARSER_H__
#include <string>
#include <list>
#include "Framework/Assets/Mesh.h"
#include "Render/Texture.h"
#include "GlobalMarco.h"

namespace Ailu
{
	enum class EResourceType : uint8_t
	{
		kStaticMesh = 0,kImage
	};
	enum class EMeshLoader : uint8_t
	{
		kFbx = 0,kObj
	};
	enum class EImageLoader : uint8_t
	{
		kPNG = 0,kJPEG,kTGA,kHDR
	};
	class IMeshParser
	{
	public:
		virtual ~IMeshParser() = default;
		virtual List<Ref<Mesh>> Parser(std::string_view sys_path) = 0;
	};

	class ITextureParser
	{
	public:
		virtual ~ITextureParser() = default;
		virtual Ref<Texture2D> Parser(const std::string_view& path) = 0;
		virtual Ref<Texture2D> Parser(const std::string_view& path,u8 mip_level) = 0;
		virtual Ref<TextureCubeMap> Parser(Vector<String>& paths) = 0;
	};
}


#endif // !IPARSER_H__

