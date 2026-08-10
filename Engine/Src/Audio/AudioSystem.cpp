#include "Audio/AudioSystem.h"
#include "Framework/Math/QuaternionMatrix.h"

namespace Ailu::ECS
{
    void AudioSystem::Update(Register &r, f32 delta_time)
    {
        AudioDevice *device = Audio::GetDevice();
        if (device == nullptr)
            return;

        AudioListenerState listener;
        bool has_listener = false;
        u32 listener_index = 0u;
        for (auto &component: r.View<AudioListenerComponent>())
        {
            const Entity entity = r.GetEntity<AudioListenerComponent>(listener_index++);
            if (!r.IsEntityEnabled(entity) || !r.IsComponentEnabled<AudioListenerComponent>(entity))
                continue;
            const auto *transform = r.GetComponent<TransformComponent>(entity);
            if (transform == nullptr)
                continue;

            listener._position = transform->_position;
            listener._forward = Vector3f(transform->_world_matrix[2][0], transform->_world_matrix[2][1], transform->_world_matrix[2][2]);
            listener._up = Vector3f(transform->_world_matrix[1][0], transform->_world_matrix[1][1], transform->_world_matrix[1][2]);
            listener._velocity = delta_time > 0.0f ? (transform->_position - transform->_prev_position) / delta_time : Vector3f::kZero;
            has_listener = true;
            break;
        }

        if (!has_listener)
        {
            u32 camera_index = 0u;
            for (auto &camera: r.View<CCamera>())
            {
                (void) camera;
                const Entity entity = r.GetEntity<CCamera>(camera_index++);
                if (!r.IsEntityEnabled(entity) || !r.IsComponentEnabled<CCamera>(entity))
                    continue;
                const auto *transform = r.GetComponent<TransformComponent>(entity);
                if (transform == nullptr)
                    continue;
                listener._position = transform->_position;
                listener._forward = Vector3f(transform->_world_matrix[2][0], transform->_world_matrix[2][1], transform->_world_matrix[2][2]);
                listener._up = Vector3f(transform->_world_matrix[1][0], transform->_world_matrix[1][1], transform->_world_matrix[1][2]);
                has_listener = true;
                break;
            }
        }

        if (has_listener)
            device->SetListener(listener);

        u32 source_index = 0u;
        for (auto &source: r.View<AudioSourceComponent>())
        {
            const Entity entity = r.GetEntity<AudioSourceComponent>(source_index++);
            if (!r.IsEntityEnabled(entity) || !r.IsComponentEnabled<AudioSourceComponent>(entity))
                continue;
            const auto *transform = r.GetComponent<TransformComponent>(entity);
            const Vector3f position = transform != nullptr ? transform->_position : Vector3f::kZero;

            if (source._play_on_awake && !source._runtime_handle.IsValid() && source._audio_event != Guid::EmptyGuid())
            {
                AudioPlayOptions options;
                options._owner = entity;
                options._position = position;
                options._is_3d = source._spatial;
                options._loop = source._loop;
                options._volume = source._volume;
                options._pitch = source._pitch;
                options._priority = source._priority;
                options._min_distance = source._min_distance;
                options._max_distance = source._max_distance;
                source._runtime_handle = Audio::PostEvent(source._audio_event, options);
            }

            if (source._runtime_handle.IsValid() && device->IsValid(source._runtime_handle))
            {
                device->SetPosition(source._runtime_handle, position);
                if (transform != nullptr && delta_time > 0.0f)
                    device->SetVelocity(source._runtime_handle, (transform->_position - transform->_prev_position) / delta_time);
            }
            else
            {
                source._runtime_handle = AudioHandle::Invalid();
            }
        }

        Audio::Update(delta_time);
    }
}// namespace Ailu::ECS
