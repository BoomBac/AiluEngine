#include "pch.h"
#include "Framework/Parser/HDRParser.h"

//#ifndef STB_IMAGE_IMPLEMENTATION
//#define STB_IMAGE_IMPLEMENTATION
//#endif // !STB_IMAGE_IMPLEMENTATION

#include "Ext/stb/stb_image.h"

namespace Ailu
{
	Ref<CubeMap> HDRParser::Parser(Vector<String>& paths)
	{
		return Ref<CubeMap>();
	}
	Ref<Texture2D> HDRParser::Parser(const WString& sys_path)
	{
		//stbi_set_flip_vertically_on_load(true);
		int w, h, n;
		auto sys_path_n = ToChar(sys_path);
		float* data = stbi_loadf(sys_path_n.data(), &w, &h, &n, 0);
		if (data == nullptr)
		{
			LOG_ERROR("Load {} failed: {}", sys_path_n, stbi_failure_reason());
			return Texture2D::Create(4, 4,false,ETextureFormat::kRGBAFloat);
		}
		else
		{
			if (n == 3)
			{
				auto tex = Texture2D::Create(w, h, false, ETextureFormat::kRGBAFloat);
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
				tex->SetPixelData(reinterpret_cast<u8*>(data),0);
				return tex;
			}
			else
			{
				LOG_WARNING("Unsupported HDR format: {} with 4 channel", sys_path_n);
			}
		}
		return nullptr;
	}
}
