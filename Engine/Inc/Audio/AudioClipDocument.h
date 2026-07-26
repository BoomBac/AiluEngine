#pragma once
#ifndef __AUDIO_CLIP_DOCUMENT_H__
#define __AUDIO_CLIP_DOCUMENT_H__

#include "Assets/AssetDocument.h"
#include "Audio/AudioTypes.h"
#include "generated/AudioClipDocument.gen.h"

namespace Ailu
{
    ACLASS()
    class AILU_API AudioClipDocument : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        AssetDocumentHeader _header;
        APROPERTY()
        String _source_file;
        APROPERTY()
        EAudioLoadMode _load_mode = EAudioLoadMode::kMemory;
        APROPERTY()
        EAudioChannelMode _channel_mode = EAudioChannelMode::kAuto;
        APROPERTY()
        bool _force_mono = false;
    };
}// namespace Ailu

#endif// __AUDIO_CLIP_DOCUMENT_H__
