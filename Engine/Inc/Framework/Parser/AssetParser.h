#pragma once
#ifndef __ASSET_PARSER_H__
#define __ASSET_PARSER_H__
#include "FbxParser.h"

namespace Ailu
{
    // 主模板定义
    template<EResourceType res_type, EMeshLoader loader_type>
    class TStaticAssetLoader;

    template<>
    class TStaticAssetLoader <EResourceType::kStaticMesh, EMeshLoader::kFbx>
    {
    public:
        static Scope<FbxParser> GetParser() { return MakeScope<FbxParser>(); }
    };

}


#endif // !ASSET_PARSER_H__

