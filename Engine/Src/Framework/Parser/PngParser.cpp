#include "Framework/Parser/PngParser.h"
#include "Framework/Common/Log.h"
#include "pch.h"
#define STB_IMAGE_IMPLEMENTATION
#include "Ext/stb/stb_image.h"
#include "Framework/Common/Path.h"

using namespace Ailu::Render;

namespace Ailu
{
    Ref<CubeMap> PngParser::Parser(Vector<String> &paths,const TextureImportSetting& import_settings)
    {
        if (paths.size() != 6)
        {
            LOG_ERROR("{}: load cubemap at {} failed,make sure input 6 textures!", Modules::Parser, paths.empty() ? "none" : paths[0]);
            return nullptr;
        }
        int x, y, n;
        Vector<u8 *> datas;
        for (int i = 0; i < 6; ++i)
        {
            u8 *data = stbi_load(paths[i].data(), &x, &y, &n, 0);
            u8 *expand_data = nullptr;
            if (data == nullptr)
            {
                LOG_ERROR("{}: Load {} failed: {}", Modules::Parser, paths[i], stbi_failure_reason());
                return nullptr;
            }
            else
            {
                if (n != 4)
                {
                    expand_data = TextureUtils::ExpandImageDataToFourChannel(data, x * y * n, n);
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
        auto tex = CubeMap::Create(x, false, ETextureFormat::kRGBA32);
        return tex;
    }
    
    Ref<Texture2D> PngParser::Parser(const WString &sys_path,const TextureImportSetting& import_settings)
    {
        TextureLoadData load_data;
        if (!LoadTextureData(sys_path, load_data))
        {
            return Texture2D::Create(4,4,ETextureFormat::kRGBA32);
        }
        else
        {
            TextureDesc desc;
            desc._width = load_data._width;
            desc._height = load_data._height;
            desc._is_linear = import_settings._is_srgb;
            desc._mip_num = import_settings._generate_mipmap? Texture::MaxMipmapCount(desc._width,desc._height) : 1;
            desc._is_readable = import_settings._is_readble;
            desc._format = ConvertTextureFormatToPixelFormat(ETextureFormat::kRGBA32);
            if (desc._mip_num > 1)
            {
                auto mip_level = desc._mip_num - 1;
                for (int i = 0; i < mip_level; i++)
                {
                    load_data._data.emplace_back(TextureUtils::DownSample(load_data._data.back(), desc._width >> i, desc._height >> i, 4));
                    if (desc._width >> i == 1)
                        break;
                }
            }
            auto tex = Texture2D::Create(desc);
            for(u16 i = 0; i < load_data._data.size(); ++i)
            {
                tex->SetPixelData(load_data._data[i], i);
            }
            tex->Name(PathUtils::GetFileName(ToChar(sys_path)));
            return tex;
        }
    }
    bool PngParser::Parser(const WString &sys_path, Ref<Texture2D> &texture,const TextureImportSetting& import_settings)
    {
        TextureLoadData load_data;
        if (!LoadTextureData(sys_path, load_data))
        {
            return false;
        }
        else
        {
            TextureDesc desc;
            desc._width = load_data._width;
            desc._height = load_data._height;
            desc._is_linear = import_settings._is_srgb;
            desc._mip_num = import_settings._generate_mipmap? Texture::MaxMipmapCount(desc._width,desc._height) : 1;
            desc._is_readable = import_settings._is_readble;
            desc._format = ConvertTextureFormatToPixelFormat(ETextureFormat::kRGBA32);
            texture->ReCreate(desc);
            for(u16 i = 0; i < load_data._data.size(); ++i)
            {
                texture->SetPixelData(load_data._data[i], i);
            }
            return true;
        }
    }

    bool PngParser::LoadTextureData(const WString &sys_path, TextureLoadData &data)
    {
		int x, y, n;
        String sys_path_n = ToChar(sys_path);
        u8 *raw_data = stbi_load(sys_path_n.data(), &x, &y, &n, 0);
        if (raw_data != nullptr)
        {
            u8 *new_data = raw_data;
            if (n != 4)
            {
                new_data = TextureUtils::ExpandImageDataToFourChannel(raw_data, x * y * n, n);
                stbi_image_free(raw_data);
            }
            data._width = x;
            data._height = y;
            data._data.emplace_back(new_data);
            return true;
        }
        else
        {
            LOG_ERROR("PngParser::LoadTextureData: Load {} failed: {}", sys_path_n, stbi_failure_reason());
            return false;
        }
    }
}// namespace Ailu
