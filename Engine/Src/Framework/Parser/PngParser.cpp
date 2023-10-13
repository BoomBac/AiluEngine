#include "pch.h"
#include "Framework/Parser/PngParser.h"
#define STB_IMAGE_IMPLEMENTATION
#include "Ext/stb/stb_image.h"



namespace Ailu
{
	Ref<Texture2D> PngParser::Parser(const std::string_view& path)
	{
		int x, y, n;
		uint8_t* data = stbi_load(path.data(), &x, &y, &n, 0);
		if (data == nullptr)
		{
			LOG_ERROR("Load {} failed: {}", path, stbi_failure_reason());
			return Texture2D::Create(4, 4, EALGFormat::kALGFormatR8G8B8A8_UNORM);
		}
		else
		{
			auto tex = Texture2D::Create(x, y, EALGFormat::kALGFormatR8G8B8A8_UNORM);
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
}
