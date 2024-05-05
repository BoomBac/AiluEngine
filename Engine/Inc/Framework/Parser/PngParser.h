#pragma once
#ifndef __PNG_PARASE_H__
#define __PNG_PARASE_H__
#include "Framework/Interface/IParser.h"

namespace Ailu
{
	class PngParser : public ITextureParser
	{
	public:
		Ref<CubeMap> Parser(Vector<String>& paths) final;
		Ref<Texture2D> Parser(const WString& sys_path) final;
	private:

	};
}


#endif // !PNG_PARASE_H__

