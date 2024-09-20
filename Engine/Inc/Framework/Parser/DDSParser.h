#pragma once
#ifndef __DDS_PARASE_H__
#define __DDS_PARASE_H__
#include "Framework/Interface/IParser.h"

namespace Ailu
{
    class DDSParser : public ITextureParser
    {
    public:
        Ref<CubeMap> Parser(Vector<String> &paths) final;
        Ref<Texture2D> Parser(const WString &sys_path) final;
    };
}// namespace Ailu


#endif// !__HDR_PARASE_H__
