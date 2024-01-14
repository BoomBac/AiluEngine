#pragma once
#ifndef __HDR_PARASE_H__
#define __HDR_PARASE_H__
#include "Framework/Interface/IParser.h"

namespace Ailu
{
	class HDRParser : public ITextureParser
	{
	public:
		Ref<Texture2D> Parser(const std::string_view& path) final;
		Ref<Texture2D> Parser(const std::string_view& path, u8 mip_level) final;
		Ref<TextureCubeMap> Parser(Vector<String>& paths) final;
		static bool IsHDRFormat(const std::string_view& path);
	};
}


#endif // !__HDR_PARASE_H__

