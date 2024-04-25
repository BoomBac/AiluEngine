#pragma once
#ifndef __HDR_PARASE_H__
#define __HDR_PARASE_H__
#include "Framework/Interface/IParser.h"

namespace Ailu
{
	class HDRParser : public ITextureParser
	{
	public:
		Ref<CubeMap> Parser(Vector<String>& paths) final;
		Ref<Texture2D> Parser(const WString& sys_path) final;
	};
}


#endif // !__HDR_PARASE_H__

