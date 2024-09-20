#pragma once
#ifndef __ASSET_PARSER_H__
#define __ASSET_PARSER_H__
#include "DDSParser.h"
#include "FbxParser.h"
#include "HDRParser.h"
#include "PngParser.h"

namespace Ailu
{
    template<EResourceType res_type, EMeshLoader loader_type>
    class TStaticMeshLoader;

    template<>
    class TStaticMeshLoader <EResourceType::kStaticMesh, EMeshLoader::kFbx>
    {
    public:
        static Scope<FbxParser> GetParser() { return MakeScope<FbxParser>(); }
    };

    template<EResourceType res_type, EImageLoader loader_type>
    class TStaticTextureLoader;
    template<>
    class TStaticTextureLoader <EResourceType::kImage, EImageLoader::kPNG>
    {
    public:
        static Scope<FbxParser> GetParser() { return MakeScope<FbxParser>(); }
    };

    template<EResourceType res_type, typename... LoaderType>
    class TStaticAssetLoader;

    template<>
    class TStaticAssetLoader<EResourceType::kStaticMesh, EMeshLoader>
    {
    public:
        static Scope<IMeshParser> GetParser(EMeshLoader loader) 
        { 
            if (loader == EMeshLoader::kFbx) return MakeScope<FbxParser>();
            else return nullptr;
        }
    };

    template<>
    class TStaticAssetLoader<EResourceType::kImage, EImageLoader>
    {
    public:
        static Scope<ITextureParser> GetParser(EImageLoader loader)
        {
            if (loader == EImageLoader::kPNG) return MakeScope<PngParser>();
            else if (loader == EImageLoader::kHDR) return MakeScope<HDRParser>();
            else if (loader == EImageLoader::kDDS) return MakeScope<DDSParser>();
            else
            {
                AL_ASSERT(true);
                return nullptr;
            }
        }
    };
}


#endif // !ASSET_PARSER_H__

