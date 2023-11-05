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
	};

	namespace TextureUtils
	{
		static uint8_t* ExpandImageDataToFourChannel(uint8_t* p_data,size_t size,uint8_t alpha = 255u)
		{
			uint8_t* new_data = new uint8_t[size / 3 * 4];
			auto pixel_num = size / 3;
			for (size_t i = 0; i < pixel_num; i++)
			{
				memcpy(new_data + i * 4, p_data + i * 3, 3);
				new_data[i * 4 + 3] = alpha;
			}
			return new_data;
		}
	}
}


#endif // !IPARSER_H__

