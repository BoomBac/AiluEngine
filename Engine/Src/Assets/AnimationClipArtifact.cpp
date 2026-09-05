#include "Assets/AnimationClipArtifact.h"

#include "Animation/TransformTrack.h"

#include <cstring>
#include <limits>

namespace Ailu
{
    namespace
    {
        enum : u32
        {
            kMetadataSection = 1u,
            kTracksSection = 2u,
            kPositionKeysSection = 3u,
            kRotationKeysSection = 4u,
            kScaleKeysSection = 5u,
            kSpriteFramesSection = 6u,
            kEventsSection = 7u,
        };

        template<typename T>
        void AppendValue(Vector<u8> &data, const T &value)
        {
            const size_t offset = data.size();
            data.resize(offset + sizeof(T));
            std::memcpy(data.data() + offset, &value, sizeof(T));
        }

        template<typename T>
        bool ReadValue(std::span<const u8> data, size_t &offset, T &value)
        {
            if (offset > data.size() || data.size() - offset < sizeof(T))
                return false;
            std::memcpy(&value, data.data() + offset, sizeof(T));
            offset += sizeof(T);
            return true;
        }

        void AppendString(Vector<u8> &data, const String &value)
        {
            const u32 size = static_cast<u32>(value.size());
            AppendValue(data, size);
            data.insert(data.end(), value.begin(), value.end());
        }

        bool ReadString(std::span<const u8> data, size_t &offset, String &value)
        {
            u32 size = 0u;
            if (!ReadValue(data, offset, size) || size > data.size() - offset)
                return false;
            value.assign(reinterpret_cast<const char *>(data.data() + offset), size);
            offset += size;
            return true;
        }

        void AppendGuid(Vector<u8> &data, const Guid &guid)
        {
            AppendString(data, guid.ToString());
        }

        bool ReadGuid(std::span<const u8> data, size_t &offset, Guid &guid)
        {
            String value;
            if (!ReadString(data, offset, value))
                return false;
            guid = Guid(value);
            return true;
        }

        void AppendVector(Vector<u8> &data, const Vector3f &value)
        {
            AppendValue(data, value.x);
            AppendValue(data, value.y);
            AppendValue(data, value.z);
        }

        bool ReadVector(std::span<const u8> data, size_t &offset, Vector3f &value)
        {
            return ReadValue(data, offset, value.x) && ReadValue(data, offset, value.y) && ReadValue(data, offset, value.z);
        }

        void AppendQuaternion(Vector<u8> &data, const Quaternion &value)
        {
            AppendValue(data, value.x);
            AppendValue(data, value.y);
            AppendValue(data, value.z);
            AppendValue(data, value.w);
        }

        bool ReadQuaternion(std::span<const u8> data, size_t &offset, Quaternion &value)
        {
            return ReadValue(data, offset, value.x) && ReadValue(data, offset, value.y) &&
                   ReadValue(data, offset, value.z) && ReadValue(data, offset, value.w);
        }

        void AddSection(AssetArtifactWriter &writer, u32 type, const Vector<u8> &data, u32 count, u32 stride)
        {
            writer.AddSection(type, data, count, stride);
        }

        bool ReadVectorKeys(std::span<const u8> data, Vector<AnimationVectorKeyArtifact> &keys)
        {
            size_t offset = 0u;
            while (offset < data.size())
            {
                AnimationVectorKeyArtifact key;
                if (!ReadValue(data, offset, key._time) || !ReadVector(data, offset, key._value))
                    return false;
                keys.emplace_back(key);
            }
            return offset == data.size();
        }

        bool ReadQuaternionKeys(std::span<const u8> data, Vector<AnimationQuaternionKeyArtifact> &keys)
        {
            size_t offset = 0u;
            while (offset < data.size())
            {
                AnimationQuaternionKeyArtifact key;
                if (!ReadValue(data, offset, key._time) || !ReadQuaternion(data, offset, key._value))
                    return false;
                keys.emplace_back(key);
            }
            return offset == data.size();
        }
    }

    bool BuildAnimationClipArtifact(const AnimationClip &clip, AnimationClipArtifact &out_artifact)
    {
        out_artifact = {};
        out_artifact._desc._clip_name = clip.Name();
        out_artifact._desc._skeleton_guid = clip.SkeletonGuid();
        out_artifact._desc._preview_mesh_guid = clip.PreviewMeshGuid();
        out_artifact._desc._frame_count = clip.FrameCount();
        out_artifact._desc._duration = clip.Duration();
        out_artifact._desc._frame_rate = clip.FrameRate();
        out_artifact._desc._frame_duration = clip.FrameDuration();
        out_artifact._desc._is_looping = clip.IsLooping();
        out_artifact._desc._root_motion = clip.GetRootMotionSettings();
        out_artifact._desc._track_count = clip.Size();
        out_artifact._desc._sprite_frame_count = static_cast<u32>(clip.SpriteTrack().Frames().size());
        out_artifact._desc._event_count = static_cast<u32>(clip.Events().size());

        out_artifact._tracks.reserve(clip.Size());
        for (u32 index = 0u; index < clip.Size(); ++index)
        {
            const TransformTrack &track = clip.GetTrackAtIndex(index);
            AnimationClipTrackArtifact track_artifact;
            track_artifact._joint_index = clip.GetIdAtIndex(index);
            track_artifact._position_offset = static_cast<u32>(out_artifact._position_keys.size());
            track_artifact._rotation_offset = static_cast<u32>(out_artifact._rotation_keys.size());
            track_artifact._scale_offset = static_cast<u32>(out_artifact._scale_keys.size());
            for (u32 key_index = 0u; key_index < track.GetPositionTrack().Size(); ++key_index)
            {
                const auto &key = track.GetPositionTrack()[key_index];
                out_artifact._position_keys.push_back({key._time, TrackHelpers::ToVector(key)});
            }
            for (u32 key_index = 0u; key_index < track.GetRotationTrack().Size(); ++key_index)
            {
                const auto &key = track.GetRotationTrack()[key_index];
                out_artifact._rotation_keys.push_back({key._time, TrackHelpers::ToQuaternion(key)});
            }
            for (u32 key_index = 0u; key_index < track.GetScaleTrack().Size(); ++key_index)
            {
                const auto &key = track.GetScaleTrack()[key_index];
                out_artifact._scale_keys.push_back({key._time, TrackHelpers::ToVector(key)});
            }
            track_artifact._position_count = static_cast<u32>(out_artifact._position_keys.size()) - track_artifact._position_offset;
            track_artifact._rotation_count = static_cast<u32>(out_artifact._rotation_keys.size()) - track_artifact._rotation_offset;
            track_artifact._scale_count = static_cast<u32>(out_artifact._scale_keys.size()) - track_artifact._scale_offset;
            out_artifact._tracks.emplace_back(track_artifact);
        }
        for (const auto &frame : clip.SpriteTrack().Frames())
            out_artifact._sprite_frames.push_back({frame._time, frame._sprite});
        for (const auto &event : clip.Events())
            out_artifact._events.push_back({event._time, event._event_id, event._kind});
        return true;
    }

    bool SerializeAnimationClipArtifact(const AnimationClipArtifact &artifact, const AssetArtifactKey &key,
                                         Vector<u8> &out_data)
    {
        Vector<u8> metadata;
        AppendString(metadata, artifact._desc._clip_name);
        AppendGuid(metadata, artifact._desc._skeleton_guid);
        AppendGuid(metadata, artifact._desc._preview_mesh_guid);
        AppendValue(metadata, artifact._desc._frame_count);
        AppendValue(metadata, artifact._desc._duration);
        AppendValue(metadata, artifact._desc._frame_rate);
        AppendValue(metadata, artifact._desc._frame_duration);
        AppendValue(metadata, static_cast<u8>(artifact._desc._is_looping));
        AppendValue(metadata, static_cast<u8>(artifact._desc._root_motion._enabled));
        AppendString(metadata, artifact._desc._root_motion._root_bone_name);
        AppendValue(metadata, static_cast<u8>(artifact._desc._root_motion._translation_mode));
        AppendValue(metadata, static_cast<u8>(artifact._desc._root_motion._rotation_mode));
        AppendValue(metadata, artifact._desc._track_count);
        AppendValue(metadata, artifact._desc._sprite_frame_count);
        AppendValue(metadata, artifact._desc._event_count);

        Vector<u8> tracks;
        for (const auto &track : artifact._tracks)
        {
            AppendValue(tracks, track._joint_index);
            AppendValue(tracks, track._position_offset);
            AppendValue(tracks, track._position_count);
            AppendValue(tracks, track._rotation_offset);
            AppendValue(tracks, track._rotation_count);
            AppendValue(tracks, track._scale_offset);
            AppendValue(tracks, track._scale_count);
        }
        Vector<u8> position_keys, rotation_keys, scale_keys;
        for (const auto &key_value : artifact._position_keys)
        {
            AppendValue(position_keys, key_value._time);
            AppendVector(position_keys, key_value._value);
        }
        for (const auto &key_value : artifact._rotation_keys)
        {
            AppendValue(rotation_keys, key_value._time);
            AppendQuaternion(rotation_keys, key_value._value);
        }
        for (const auto &key_value : artifact._scale_keys)
        {
            AppendValue(scale_keys, key_value._time);
            AppendVector(scale_keys, key_value._value);
        }
        Vector<u8> sprite_frames;
        for (const auto &frame : artifact._sprite_frames)
        {
            AppendValue(sprite_frames, frame._time);
            AppendGuid(sprite_frames, frame._sprite);
        }
        Vector<u8> events;
        for (const auto &event : artifact._events)
        {
            AppendValue(events, event._time);
            AppendValue(events, event._event_id);
            AppendValue(events, static_cast<u8>(event._kind));
        }

        AssetArtifactWriter writer;
        AddSection(writer, kMetadataSection, metadata, 1u, 0u);
        AddSection(writer, kTracksSection, tracks, static_cast<u32>(artifact._tracks.size()), 26u);
        AddSection(writer, kPositionKeysSection, position_keys, static_cast<u32>(artifact._position_keys.size()), 16u);
        AddSection(writer, kRotationKeysSection, rotation_keys, static_cast<u32>(artifact._rotation_keys.size()), 20u);
        AddSection(writer, kScaleKeysSection, scale_keys, static_cast<u32>(artifact._scale_keys.size()), 16u);
        AddSection(writer, kSpriteFramesSection, sprite_frames, static_cast<u32>(artifact._sprite_frames.size()), 0u);
        AddSection(writer, kEventsSection, events, static_cast<u32>(artifact._events.size()), 9u);
        return writer.Serialize(EAssetArtifactType::kAnimationClip, kAnimationClipArtifactVersion, key, out_data);
    }

    bool DeserializeAnimationClipArtifact(std::span<const u8> data, const AssetArtifactKey &key,
                                          AnimationClipArtifact &out_artifact)
    {
        AssetArtifactReader reader;
        if (!reader.Load(data, key, EAssetArtifactType::kAnimationClip))
            return false;
        out_artifact = {};
        const auto metadata = reader.GetSectionData(kMetadataSection);
        size_t offset = 0u;
        u8 is_looping = 0u;
        u8 root_motion_enabled = 0u;
        u8 root_motion_translation = 0u;
        u8 root_motion_rotation = 0u;
        if (!ReadString(metadata, offset, out_artifact._desc._clip_name) ||
            !ReadGuid(metadata, offset, out_artifact._desc._skeleton_guid) ||
            !ReadGuid(metadata, offset, out_artifact._desc._preview_mesh_guid) ||
            !ReadValue(metadata, offset, out_artifact._desc._frame_count) ||
            !ReadValue(metadata, offset, out_artifact._desc._duration) ||
            !ReadValue(metadata, offset, out_artifact._desc._frame_rate) ||
            !ReadValue(metadata, offset, out_artifact._desc._frame_duration) ||
            !ReadValue(metadata, offset, is_looping) ||
            !ReadValue(metadata, offset, root_motion_enabled) ||
            !ReadString(metadata, offset, out_artifact._desc._root_motion._root_bone_name) ||
            !ReadValue(metadata, offset, root_motion_translation) ||
            !ReadValue(metadata, offset, root_motion_rotation) ||
            !ReadValue(metadata, offset, out_artifact._desc._track_count) ||
            !ReadValue(metadata, offset, out_artifact._desc._sprite_frame_count) ||
            !ReadValue(metadata, offset, out_artifact._desc._event_count) || offset != metadata.size())
            return false;
        out_artifact._desc._is_looping = is_looping != 0u;
        out_artifact._desc._root_motion._enabled = root_motion_enabled != 0u;
        out_artifact._desc._root_motion._translation_mode =
            static_cast<ERootMotionTranslationMode>(root_motion_translation);
        out_artifact._desc._root_motion._rotation_mode = static_cast<ERootMotionRotationMode>(root_motion_rotation);

        const auto tracks = reader.GetSectionData(kTracksSection);
        offset = 0u;
        while (offset < tracks.size())
        {
            AnimationClipTrackArtifact track;
            if (!ReadValue(tracks, offset, track._joint_index) || !ReadValue(tracks, offset, track._position_offset) ||
                !ReadValue(tracks, offset, track._position_count) || !ReadValue(tracks, offset, track._rotation_offset) ||
                !ReadValue(tracks, offset, track._rotation_count) || !ReadValue(tracks, offset, track._scale_offset) ||
                !ReadValue(tracks, offset, track._scale_count))
                return false;
            out_artifact._tracks.emplace_back(track);
        }
        if (out_artifact._tracks.size() != out_artifact._desc._track_count ||
            !ReadVectorKeys(reader.GetSectionData(kPositionKeysSection), out_artifact._position_keys) ||
            !ReadQuaternionKeys(reader.GetSectionData(kRotationKeysSection), out_artifact._rotation_keys) ||
            !ReadVectorKeys(reader.GetSectionData(kScaleKeysSection), out_artifact._scale_keys))
            return false;
        const auto is_range_valid = [](u32 offset, u32 count, size_t size)
        {
            return static_cast<size_t>(offset) <= size && static_cast<size_t>(count) <= size - offset;
        };
        for (const auto &track : out_artifact._tracks)
        {
            if (!is_range_valid(track._position_offset, track._position_count, out_artifact._position_keys.size()) ||
                !is_range_valid(track._rotation_offset, track._rotation_count, out_artifact._rotation_keys.size()) ||
                !is_range_valid(track._scale_offset, track._scale_count, out_artifact._scale_keys.size()))
                return false;
        }

        const auto sprite_frames = reader.GetSectionData(kSpriteFramesSection);
        offset = 0u;
        while (offset < sprite_frames.size())
        {
            AnimationSpriteFrameArtifact frame;
            if (!ReadValue(sprite_frames, offset, frame._time) || !ReadGuid(sprite_frames, offset, frame._sprite))
                return false;
            out_artifact._sprite_frames.emplace_back(frame);
        }
        const auto events = reader.GetSectionData(kEventsSection);
        offset = 0u;
        while (offset < events.size())
        {
            AnimationEventArtifact event;
            u8 kind = 0u;
            if (!ReadValue(events, offset, event._time) || !ReadValue(events, offset, event._event_id) ||
                !ReadValue(events, offset, kind))
                return false;
            event._kind = static_cast<EAnimationEventKind>(kind);
            out_artifact._events.emplace_back(event);
        }
        return out_artifact._sprite_frames.size() == out_artifact._desc._sprite_frame_count &&
               out_artifact._events.size() == out_artifact._desc._event_count;
    }

    Ref<AnimationClip> CreateAnimationClipFromArtifact(const AnimationClipArtifact &artifact)
    {
        Ref<AnimationClip> clip = MakeRef<AnimationClip>();
        clip->Name(artifact._desc._clip_name);
        clip->FrameCount(artifact._desc._frame_count);
        clip->Duration(artifact._desc._duration);
        clip->FrameRate(artifact._desc._frame_rate);
        clip->FrameDuration(artifact._desc._frame_duration);
        clip->IsLooping(artifact._desc._is_looping);
        clip->SetRootMotionSettings(artifact._desc._root_motion);
        clip->SkeletonGuid(artifact._desc._skeleton_guid);
        clip->PreviewMeshGuid(artifact._desc._preview_mesh_guid);
        clip->StartTime(0.0f);
        clip->EndTime(artifact._desc._duration);

        for (const auto &track_artifact : artifact._tracks)
        {
            auto &track = (*clip)[track_artifact._joint_index];
            auto &position_track = track.GetPositionTrack();
            position_track.Resize(track_artifact._position_count);
            for (u32 i = 0u; i < track_artifact._position_count; ++i)
            {
                const auto &key = artifact._position_keys[track_artifact._position_offset + i];
                auto frame = TrackHelpers::FromVector(key._value);
                frame._time = key._time;
                position_track[i] = frame;
            }
            auto &rotation_track = track.GetRotationTrack();
            rotation_track.Resize(track_artifact._rotation_count);
            for (u32 i = 0u; i < track_artifact._rotation_count; ++i)
            {
                const auto &key = artifact._rotation_keys[track_artifact._rotation_offset + i];
                auto frame = TrackHelpers::FromQuaternion(key._value);
                frame._time = key._time;
                rotation_track[i] = frame;
            }
            auto &scale_track = track.GetScaleTrack();
            scale_track.Resize(track_artifact._scale_count);
            for (u32 i = 0u; i < track_artifact._scale_count; ++i)
            {
                const auto &key = artifact._scale_keys[track_artifact._scale_offset + i];
                auto frame = TrackHelpers::FromVector(key._value);
                frame._time = key._time;
                scale_track[i] = frame;
            }
        }
        for (const auto &frame : artifact._sprite_frames)
            clip->SpriteTrack().AddFrame(SpriteKeyFrame{frame._time, frame._sprite});
        for (const auto &event : artifact._events)
            clip->AddEvent(AnimationEvent{event._time, event._event_id, event._kind});
        if (artifact._desc._duration <= 0.0f)
            clip->RecalculateDuration();
        return clip;
    }
}
