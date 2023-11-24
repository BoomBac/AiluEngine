#pragma once
#ifndef __PNG_PARASE_H__
#define __PNG_PARASE_H__
#include "Framework/Interface/IParser.h"

namespace Ailu
{
	class PngParser : public ITextureParser
	{
	public:
		Ref<Texture2D> Parser(const std::string_view& path) final;
		Ref<TextureCubeMap> Parser(Vector<String>& paths) final;
	private:

	};
}


#endif // !PNG_PARASE_H__

