#pragma once
#ifndef __HDR_PARASE_H__
#define __HDR_PARASE_H__
#include "Framework/Interface/IParser.h"

namespace Ailu
{
	class HDRParser : public ITextureParser
	{
	public:
		Ref<CubeMap> Parser(Vector<String>& paths,const TextureImportSetting& import_settings) final;
		Ref<Texture2D> Parser(const WString& sys_path,const TextureImportSetting& import_settings) final;
		bool Parser(const WString &sys_path,Ref<Texture2D>& texture,const TextureImportSetting& import_settings) final;
	protected:
		bool LoadTextureData(const WString &sys_path,TextureLoadData& data) final;
	};
}


#endif // !__HDR_PARASE_H__

