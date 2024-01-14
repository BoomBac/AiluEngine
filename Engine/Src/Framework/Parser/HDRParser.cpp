#include "pch.h"
#include "Framework/Parser/HDRParser.h"

//#ifndef STB_IMAGE_IMPLEMENTATION
//#define STB_IMAGE_IMPLEMENTATION
//#endif // !STB_IMAGE_IMPLEMENTATION

#include "Ext/stb/stb_image.h"

namespace Ailu
{
	Ref<Texture2D> HDRParser::Parser(const std::string_view& path)
	{
		return Ref<Texture2D>();
	}
	Ref<Texture2D> HDRParser::Parser(const std::string_view& path, u8 mip_level)
	{
		if (!IsHDRFormat(path))
			return nullptr;
			//stbi_set_flip_vertically_on_load(true);
		int w, h, n;
		float* data = stbi_loadf(path.data(), &w, &h, &n, 0);
		if (data == nullptr)
		{
			LOG_ERROR("Load {} failed: {}", path, stbi_failure_reason());
			return Texture2D::Create(4, 4, n,EALGFormat::kALGFormatR32G32B32_FLOAT);
		}
		else
		{
			if (n == 3)
			{
				auto tex = Texture2D::Create(w, h, n,EALGFormat::kALGFormatR32G32B32_FLOAT);
				tex->AssetPath(PathUtils::ExtractAssetPath(path.data()));
				tex->Name(PathUtils::GetFileName(path, true));
				//Vector<float*> mipmaps{ data };
				//mip_level = 1;
				//int size = x;
				//while (size)
				//{
				//	size >>= 1;
				//	mip_level++;
				//}
				//mip_level = mip_level > 9 ? 9 : mip_level;
				//for (int i = 0; i < mip_level - 1; i++)
				//{
				//	mipmaps.emplace_back(TextureUtils::DownSample(mipmaps.back(), x >> i, y >> i, 4));
				//	if (x >> i == 1)
				//		break;
				//}
				tex->FillData(reinterpret_cast<u8*>(data));
				return tex;
			}
			else
			{
				LOG_WARNING("Unsupported HDR format: {} with 4 channel", path);
			}
		}
	}
	Ref<TextureCubeMap> HDRParser::Parser(Vector<String>& paths)
	{
		return Ref<TextureCubeMap>();
	}
	bool HDRParser::IsHDRFormat(const std::string_view& path)
	{
		return path.find(".hdr") != std::string::npos || path.find(".HDR") != std::string::npos || path.find(".exr") != std::string::npos || path.find(".EXR") != std::string::npos;
	}
}
