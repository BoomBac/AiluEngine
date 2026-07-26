#include "Audio/Backend/NullAudioBackend.h"

namespace Ailu
{
    bool NullAudioBackend::Initialize(const AudioDeviceConfig &)
    {
        return true;
    }

    void NullAudioBackend::Shutdown() {}
    void NullAudioBackend::Update() {}
    BackendVoiceHandle NullAudioBackend::CreateVoice(const BackendVoiceCreateInfo &) { return BackendVoiceHandle{1u, 1u}; }
    void NullAudioBackend::DestroyVoice(BackendVoiceHandle) {}
    void NullAudioBackend::Play(BackendVoiceHandle) {}
    void NullAudioBackend::Stop(BackendVoiceHandle) {}
    void NullAudioBackend::Pause(BackendVoiceHandle) {}
    void NullAudioBackend::Resume(BackendVoiceHandle) {}
    void NullAudioBackend::SetVolume(BackendVoiceHandle, f32) {}
    void NullAudioBackend::SetPitch(BackendVoiceHandle, f32) {}
    void NullAudioBackend::SetPosition(BackendVoiceHandle, const Vector3f &) {}
    void NullAudioBackend::SetVelocity(BackendVoiceHandle, const Vector3f &) {}
    void NullAudioBackend::SetListener(const AudioListenerState &) {}
    void NullAudioBackend::SetBusVolume(EAudioBus, f32) {}
    void NullAudioBackend::SetBusMuted(EAudioBus, bool) {}
    bool NullAudioBackend::IsPlaying(BackendVoiceHandle) const { return false; }
}// namespace Ailu
