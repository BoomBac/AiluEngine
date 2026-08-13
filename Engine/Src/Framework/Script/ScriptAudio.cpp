#include "Framework/Script/ScriptAudio.h"

#include "Audio/Audio.h"

namespace Ailu
{
    bool ScriptAudio::PlayOneShot(const ScriptAssetValue &clip, const Vector3f &position)
    {
        if (clip._guid == Guid::EmptyGuid()) return false;

        AudioPlayOptions options;
        options._is_3d = true;
        options._position = position;
        return Audio::Play(clip._guid, options).IsValid();
    }

    void ScriptAudio::StopMusic() { Audio::StopMusic(); }
}
