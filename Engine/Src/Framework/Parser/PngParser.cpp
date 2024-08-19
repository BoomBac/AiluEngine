#include "pch.h"
#include "Framework/Parser/PngParser.h"
#include "Framework/Common/Log.h"
#define STB_IMAGE_IMPLEMENTATION
#include "Ext/stb/stb_image.h"
#include "Framework/Common/Path.h"



namespace Ailu
{
	Ref<CubeMap> PngParser::Parser(Vector<String>& paths)
	{
		if (paths.size() != 6)
		{
			g_pLogMgr->LogErrorFormat("{}: load cubemap at {} failed,make sure input 6 textures!", Modules::Parser, paths.empty() ? "none" : paths[0]);
			return nullptr;
		}
		int x, y, n;
		Vector<u8*> datas;
		for (int i = 0; i < 6; ++i)
		{
			u8* data = stbi_load(paths[i].data(), &x, &y, &n, 0);
			u8* expand_data = nullptr;
			if (data == nullptr)
			{
				g_pLogMgr->LogErrorFormat("{}: Load {} failed: {}", Modules::Parser, paths[i], stbi_failure_reason());
				return nullptr;
			}
			else
			{
				if (n != 4)
				{
					expand_data = TextureUtils::ExpandImageDataToFourChannel(data, x * y * n,n);
				}
				else
				{
					int data_size = x * y * n;
					expand_data = new u8[data_size];
					memcpy(expand_data, data, data_size);
				}
				stbi_image_free(data);
				datas.emplace_back(expand_data);
			}
		}
		auto tex = CubeMap::Create(x,false,ETextureFormat::kRGBA32);
		return tex;
	}
	Ref<Texture2D> PngParser::Parser(const WString& sys_path)
	{
		int x, y, n;
		String sys_path_n = ToChar(sys_path);
		u8* data = stbi_load(sys_path_n.data(), &x, &y, &n, 0);
		if (data == nullptr)
		{
			LOG_ERROR("Load {} failed: {}", sys_path_n, stbi_failure_reason());
			return MakeRef<Texture2D>(4, 4);
		}
		else
		{
			auto tex = Texture2D::Create(x, y, true);
			//tex->AssetPath(PathUtils::ExtractAssetPath(sys_path_n));
			//tex->Name(PathUtils::GetFileName(sys_path_n, true));
			u8* new_data = data;
			if (n != 4)
			{
				new_data = TextureUtils::ExpandImageDataToFourChannel(data, x * y * n, n);
				stbi_image_free(data);
			}
			Vector<u8*> mipmaps{ new_data };
			//all_mip_level = min(all_mip_level, 8);
			auto mip_level = tex->MipmapLevel();
			tex->SetPixelData(new_data,0);
			for (int i = 0; i < mip_level + 1; i++)
			{
				mipmaps.emplace_back(TextureUtils::DownSample(mipmaps.back(), x >> i, y >> i, 4));
				if (x >> i == 1)
					break;
			}
			for (int i = 0; i < mip_level + 1; i++)
			{
				tex->SetPixelData(mipmaps[i], i);
				DESTORY_PTRARR(mipmaps[i]);
			}
			tex->Name(PathUtils::GetFileName(sys_path_n));
			return tex;
		}
	}
}
