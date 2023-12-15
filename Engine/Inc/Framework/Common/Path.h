#pragma once
#ifndef __PATH_H__
#define __PATH_H__
#include "GlobalMarco.h"

namespace Ailu
{
#ifdef COMPANY_ENV
    static const String kEngineRootPath = "C:/AiluEngine/Engine/";
    static const String kEngineResRootPath = "C:/AiluEngine/Engine/Res/";
#else
    static const String kEngineRootPath = "F:/ProjectCpp/AiluEngine/Engine/";
    static const String kEngineResRootPath = "F:/ProjectCpp/AiluEngine/Engine/Res/";
#endif

    namespace EnginePath
    {
        static const String kEngineShaderPath = "Shaders/";
        static const String kEngineMaterialPath = "Materials/";
        static const String kEngineMeshPath = "Meshs/";
        static const String kEngineTexturePath = "Textures/";
    }

    namespace PathUtils
    {
        inline static bool IsSystemPath(const String& path)
        {
            return path.find_first_of(":") == 1;
        }

        //begin from Res/  eg: Engien/Res/aa/b -> aa/b
        static inline String ExtractAssetPath(const String& path)
        {
            auto p1 = path.find("Res/");
            auto p2 = path.find("Res\\");
            if (p1 != path.npos)
            {
                return path.substr(p1 + 4);
            }
            if (p2 != path.npos)
            {
                return path.substr(p2 + 5);
            }
            return path;
        }
    };

    static inline std::string GetResPath(const std::string& sub_path)
    {
        String path = sub_path;
        if (sub_path.starts_with("/"))
            path = path.substr(1);
        else if (sub_path.starts_with("\\"))
            path = path.substr(2);
        return kEngineResRootPath + path;
    }



    static std::string GetFileName(const std::string_view filePath, bool include_ext = false)
    {
        size_t found = filePath.find_last_of("/\\");
        size_t dot_pos = filePath.find_last_of(".");

        if (found != std::string::npos && dot_pos != std::string::npos)
        {
            if (include_ext)
                return std::string(filePath.substr(found + 1).data());
            else
            {
                size_t name_length = dot_pos - (found + 1);
                return std::string(filePath.substr(found + 1, name_length).data(), name_length);
            }
        }
        else
        {
            return std::string(filePath.data(), filePath.length());
        }
    }
}



#endif // !PATH_H__

