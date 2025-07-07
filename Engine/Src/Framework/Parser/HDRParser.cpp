#include "pch.h"
#include "Framework/Parser/HDRParser.h"
#include "Framework/Common/StackTrace.h"
//#ifndef STB_IMAGE_IMPLEMENTATION
//#define STB_IMAGE_IMPLEMENTATION
//#endif // !STB_IMAGE_IMPLEMENTATION
#include "Ext/stb/stb_image.h"

#define TINYEXR_IMPLEMENTATION
#include "Ext/tinyexr.h"

using namespace Ailu::Render;

namespace Ailu
{
	Ref<CubeMap> HDRParser::Parser(Vector<String>& paths,const TextureImportSetting& import_settings)
	{
		return Ref<CubeMap>();
	}
	Ref<Texture2D> HDRParser::Parser(const WString& sys_path,const TextureImportSetting& import_settings)
	{
		TextureLoadData load_data;
		if (LoadTextureData(sys_path, load_data))
		{
			Texture2DInitializer desc;
            desc._width = load_data._width;
            desc._height = load_data._height;
            desc._is_linear = import_settings._is_srgb;
            desc._is_mipmap_chain = import_settings._generate_mipmap;
            desc._is_readble = import_settings._is_readble;
            desc._format = load_data._format;
			Ref<Texture2D> texture = Texture2D::Create(desc);
			texture->SetPixelData(load_data._data[0],0);
			return texture;
		}
		return nullptr;
	}
    bool HDRParser::Parser(const WString &sys_path, Ref<Texture2D> &texture,const TextureImportSetting& import_settings)
	{
		TextureLoadData load_data;
		if (LoadTextureData(sys_path, load_data))
		{
			Texture2DInitializer desc;
            desc._width = load_data._width;
            desc._height = load_data._height;
            desc._is_linear = import_settings._is_srgb;
            desc._is_mipmap_chain = import_settings._generate_mipmap;
            desc._is_readble = import_settings._is_readble;
            desc._format = load_data._format;
			texture->ReCreate(desc);
			texture->SetPixelData(load_data._data[0],0);
			return true;
		}
		return false;
	}
	bool HDRParser::LoadTextureData(const WString &sys_path,TextureLoadData& data)
	{
		//stbi_set_flip_vertically_on_load(true);
		i32 w,h,n;
		auto sys_path_n = ToChar(sys_path);
        f32 *raw_data = nullptr;
        if (su::EndWith(sys_path_n, "hdr"))
        {
            raw_data = stbi_loadf(sys_path_n.data(), &w, &h, &n, 0);
        }
        else if (su::EndWith(sys_path_n, "exr"))
        {
            const char *err = nullptr;
            if (int ret = LoadEXR(&raw_data, &w, &h, sys_path_n.data(), &err); ret != TINYEXR_SUCCESS)
            {
                LOG_ERROR("HDRParser::LoadTextureDat: Load {} failed: {}", sys_path_n, err);
                LOG_ERROR("{}", StackTrace::Capture());
                FreeEXRErrorMessage(err);
                return false;
            }
            else
                n = 4;//tinyexr会填充a通道即使没有
        }
        else
            LOG_ERROR("HDRParser::LoadTextureDat: Unsupported HDR format: {}", sys_path_n);
		if (raw_data == nullptr)
		{
			LOG_ERROR("Load {} failed: {}", sys_path_n, stbi_failure_reason());
			return false;
		}
		else
		{
			if (n == 3)
			{
				data._width = w;
				data._height = h;
				data._format = ETextureFormat::kRGBFloat;
				data._data.emplace_back(reinterpret_cast<u8*>(raw_data));
			}
			else
			{
				LOG_WARNING("Unsupported HDR format: {} with 4 channel", sys_path_n);
				return false;
			}
		}
		return true;
	}
}
