#pragma once
#ifndef __TGA_PARASE_H__
#define __TGA_PARASE_H__
#include "Framework/Interface/IParser.h"

namespace Ailu
{
	class TagParser : public ITextureParser
	{
	public:
		Ref<Texture2D> Parser(const std::string_view& path) final;
	private:

	};
}


#endif // !__TGA_PARASE_H__

