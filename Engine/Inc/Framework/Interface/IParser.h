#pragma once
#ifndef __IPARSER_H__
#define __IPARSER_H__
#include <string>
#include "Framework/Assets/Mesh.h"
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
		kPNG = 0,kJPEG,kHDR
	};
	class IMeshParser
	{
	public:
		virtual ~IMeshParser() = default;
		virtual Ref<Mesh> Parser(const std::string_view& path) = 0;
	};
}


#endif // !IPARSER_H__

