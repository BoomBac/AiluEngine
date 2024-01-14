#include "pch.h"
#include "Framework/Parser/PngParser.h"
#include "Framework/Common/LogMgr.h"
#define STB_IMAGE_IMPLEMENTATION
#include "Ext/stb/stb_image.h"
#include "Framework/Common/Path.h"



namespace Ailu
{
	Ref<Texture2D> PngParser::Parser(const std::string_view& path)
	{
		int x, y, n;
		auto asset_path = PathUtils::ExtractAssetPath(path.data());
		uint8_t* data = stbi_load(path.data(), &x, &y, &n, 0);
		if (data == nullptr)
		{
			LOG_ERROR("Load {} failed: {}", path, stbi_failure_reason());
			auto tex = Texture2D::Create(4, 4);
			tex->Name("Placeholder");
			return tex;
		}
		else
		{
			auto tex = Texture2D::Create(x, y);
			tex->AssetPath(asset_path);
			tex->Name(PathUtils::GetFileName(path,true));
			uint8_t* new_data = nullptr;
			if (n == 3)
			{
				new_data = TextureUtils::ExpandImageDataToFourChannel(data, x * y * 3);
				stbi_image_free(data);
				tex->FillData(std::move(new_data));
			}
			else
			{
				tex->FillData(std::move(data));
				//texture会调用delete[]释放data，使用stbi原始数据使用malloc申请空间，可能会有问题
				//stbi_image_free(data);
			}
			return tex;
		}
	}
	Ref<Texture2D> PngParser::Parser(const std::string_view& path, u8 mip_level)
	{
		int x, y, n;
		uint8_t* data = stbi_load(path.data(), &x, &y, &n, 0);
		if (data == nullptr)
		{
			LOG_ERROR("Load {} failed: {}", path, stbi_failure_reason());
			return Texture2D::Create(4, 4);
		}
		else
		{
			auto tex = Texture2D::Create(x, y);
			tex->AssetPath(PathUtils::ExtractAssetPath(path.data()));
			tex->Name(PathUtils::GetFileName(path, true));
			uint8_t* new_data = data;
			if (n == 3)
			{
				new_data = TextureUtils::ExpandImageDataToFourChannel(data, x * y * 3);
				stbi_image_free(data);
			}
			Vector<u8*> mipmaps{ new_data };
			mip_level = 1;
			int size = x;
			while (size)
			{
				size >>= 1;
				mip_level++;
			}
			mip_level = mip_level > 9 ? 9 : mip_level;
			for (int i = 0; i < mip_level - 1; i++)
			{
				mipmaps.emplace_back(TextureUtils::DownSample(mipmaps.back(), x >> i, y >> i, 4));
				if (x >> i == 1)
					break;
			}
			tex->FillData(mipmaps);
			return tex;
		}
	}
	Ref<TextureCubeMap> PngParser::Parser(Vector<String>& paths)
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
				if (n == 3)
				{
					expand_data = TextureUtils::ExpandImageDataToFourChannel(data, x * y * 3);
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
		auto tex = TextureCubeMap::Create(x, y, EALGFormat::kALGFormatR8G8B8A8_UNORM);
		tex->FillData(datas);
		return tex;
	}
}
