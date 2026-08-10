#pragma once
#ifndef __SCRIPT_ASSET_H__
#define __SCRIPT_ASSET_H__

#include "Objects/Object.h"
#include "generated/ScriptAsset.gen.h"

namespace Ailu
{
    ACLASS()
    class AILU_API ScriptAsset : public Object
    {
        GENERATED_BODY()

    public:
        ScriptAsset() = default;
        explicit ScriptAsset(String source_file) : _source_file(std::move(source_file)) {}

        const String &SourceFile() const { return _source_file; }
        void SourceFile(String source_file) { _source_file = std::move(source_file); }

    private:
        String _source_file;
    };
}

#endif
