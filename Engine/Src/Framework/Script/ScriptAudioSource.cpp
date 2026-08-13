#include "Framework/Script/ScriptAudioSource.h"

#include "Audio/Audio.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"

namespace Ailu
{
    namespace
    {
        ECS::AudioSourceComponent *ResolveAudioSource(SceneManagement::Scene *scene, ECS::Entity entity)
        {
            return scene != nullptr && scene->IsValidEntity(entity) ?
                       scene->GetRegister().GetComponent<ECS::AudioSourceComponent>(entity) : nullptr;
        }
    }

    bool ScriptAudioSource::IsValid() const { return ResolveAudioSource(_scene, _entity) != nullptr; }
    void ScriptAudioSource::Play() const
    {
        auto *component = ResolveAudioSource(_scene, _entity);
        if (component == nullptr || component->_audio_event == Guid::EmptyGuid()) return;
        Stop();
        AudioPlayOptions options;
        options._owner = _entity;
        options._is_3d = component->_spatial;
        options._loop = component->_loop;
        options._volume = component->_volume;
        options._pitch = component->_pitch;
        options._priority = component->_priority;
        options._min_distance = component->_min_distance;
        options._max_distance = component->_max_distance;
        if (const auto *transform = _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity))
            options._position = transform->_position;
        component->_runtime_handle = Audio::PostEvent(component->_audio_event, options);
    }
    void ScriptAudioSource::Stop() const
    {
        auto *component = ResolveAudioSource(_scene, _entity);
        if (component == nullptr) return;
        if (component->_runtime_handle.IsValid()) Audio::Stop(component->_runtime_handle);
        component->_runtime_handle = AudioHandle::Invalid();
    }
    f32 ScriptAudioSource::GetVolume() const
    {
        const auto *component = ResolveAudioSource(_scene, _entity);
        return component != nullptr ? component->_volume : 0.0f;
    }
    void ScriptAudioSource::SetVolume(f32 volume) const
    {
        auto *component = ResolveAudioSource(_scene, _entity);
        if (component == nullptr) return;
        component->_volume = volume;
        if (component->_runtime_handle.IsValid()) Audio::SetVolume(component->_runtime_handle, volume);
    }
    f32 ScriptAudioSource::GetPitch() const
    {
        const auto *component = ResolveAudioSource(_scene, _entity);
        return component != nullptr ? component->_pitch : 0.0f;
    }
    void ScriptAudioSource::SetPitch(f32 pitch) const
    {
        auto *component = ResolveAudioSource(_scene, _entity);
        if (component == nullptr) return;
        component->_pitch = pitch;
        if (component->_runtime_handle.IsValid()) Audio::SetPitch(component->_runtime_handle, pitch);
    }
    bool ScriptAudioSource::IsLoop() const
    {
        const auto *component = ResolveAudioSource(_scene, _entity);
        return component != nullptr && component->_loop;
    }
    void ScriptAudioSource::SetLoop(bool loop) const
    {
        if (auto *component = ResolveAudioSource(_scene, _entity)) component->_loop = loop;
    }
}
