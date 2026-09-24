#include "Render/ImageCapture/ImageCaptureService.h"

#include "Framework/Common/Log.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/ImageCapture/DdsCaptureEncoder.h"
#include "Render/ImageCapture/PngCaptureEncoder.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <format>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace Ailu::Render
{
namespace
{
constexpr u32 kMaxTargetWaitFrames = 300u;

struct ManifestFrame
{
    u32 _capture_index = 0u;
    u64 _engine_frame_index = 0u;
    String _file;
};

struct CaptureSession
{
    ImageCaptureSequenceDesc _desc;
    ImageCaptureStatus _status;
    WString _output_directory;
    WString _manifest_path;
    bool _has_sequence_start_frame = false;
    u64 _sequence_start_frame = 0u;
    bool _has_attempted_frame = false;
    u64 _last_attempted_frame = 0u;
    bool _has_seen_target = false;
    u64 _last_seen_target_frame = 0u;
    bool _has_checked_wait_frame = false;
    u64 _last_checked_wait_frame = 0u;
    u32 _target_wait_frames = 0u;
    bool _has_source_signature = false;
    ImageCaptureSourceSignature _source_signature;
    std::unordered_map<ImageCaptureReadbackId, u64> _in_flight;
    Vector<ManifestFrame> _frames;
};

struct ManifestSnapshot
{
    ImageCaptureSequenceDesc _desc;
    ImageCaptureStatus _status;
    bool _has_source_signature = false;
    ImageCaptureSourceSignature _source_signature;
    Vector<ManifestFrame> _frames;
};

std::mutex s_mutex;
std::mutex s_manifest_mutex;
std::unordered_map<ImageCaptureSessionId, std::unique_ptr<CaptureSession>> s_sessions;
ImageCaptureSessionId s_next_session_id = 1u;
ImageCaptureReadbackId s_next_readback_id = 1u;
bool s_shutdown = false;

String JsonEscape(StringView value)
{
    String result;
    result.reserve(value.size() + 8u);
    for (const char c: value)
    {
        switch (c)
        {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result.push_back(c); break;
        }
    }
    return result;
}

const char *OutputFormatName(EImageCaptureOutputFormat format)
{
    switch (format)
    {
    case EImageCaptureOutputFormat::kDdsNative: return "dds_native";
    case EImageCaptureOutputFormat::kPng: return "png";
    case EImageCaptureOutputFormat::kHdr: return "hdr";
    default: return "unknown";
    }
}

const char *StateName(EImageCaptureState state)
{
    switch (state)
    {
    case EImageCaptureState::kPending: return "pending";
    case EImageCaptureState::kCapturing: return "capturing";
    case EImageCaptureState::kFinalizing: return "finalizing";
    case EImageCaptureState::kCompleted: return "completed";
    case EImageCaptureState::kCancelled: return "cancelled";
    case EImageCaptureState::kFailed: return "failed";
    default: return "unknown";
    }
}

ImageCaptureSourceSignature GetSourceSignature(Texture *texture)
{
    ImageCaptureSourceSignature signature;
    if (texture == nullptr)
        return signature;

    signature._format = texture->PixelFormat();
    signature._dimension = texture->Dimension();
    signature._width = texture->Width();
    signature._height = texture->Height();
    signature._mip_count = std::max<u32>(1u, texture->MipmapLevel());
    if (const auto *texture_3d = dynamic_cast<const Texture3D *>(texture); texture_3d != nullptr)
        signature._depth = std::max<u32>(1u, texture_3d->Depth());
    if (const auto *render_texture = dynamic_cast<const RenderTexture *>(texture); render_texture != nullptr)
    {
        const u32 slice_count = render_texture->ArraySlice();
        if (signature._dimension == ETextureDimension::kTex2DArray)
            signature._array_size = std::max<u32>(1u, slice_count);
        else if (signature._dimension == ETextureDimension::kCubeArray)
            signature._array_size = std::max<u32>(1u, slice_count);
    }
    return signature;
}

void SetFailedLocked(CaptureSession &session, String error)
{
    if (session._status._state == EImageCaptureState::kCancelled)
        return;
    session._status._state = EImageCaptureState::kFailed;
    session._status._error = std::move(error);
}

void ReleaseReadbackLocked(CaptureSession &session, ImageCaptureReadbackId readback_id)
{
    if (auto in_flight = session._in_flight.find(readback_id); in_flight != session._in_flight.end())
    {
        session._status._in_flight_bytes -= in_flight->second;
        session._in_flight.erase(in_flight);
        session._status._in_flight_count = static_cast<u32>(session._in_flight.size());
    }
}

void ReleaseReadback(ImageCaptureSessionId session_id, ImageCaptureReadbackId readback_id)
{
    std::lock_guard lock(s_mutex);
    if (auto iter = s_sessions.find(session_id); iter != s_sessions.end())
        ReleaseReadbackLocked(*iter->second, readback_id);
}

bool WriteManifest(const WString &path, const ManifestSnapshot &session)
{
    try
    {
        std::ofstream output(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
        if (!output)
            return false;

        const auto &source = session._has_source_signature ? session._source_signature : ImageCaptureSourceSignature{};
        output << "{\n"
               << "  \"state\": \"" << StateName(session._status._state) << "\",\n"
               << "  \"output_format\": \"" << OutputFormatName(session._desc._output_format) << "\",\n"
               << "  \"frame_count\": " << session._desc._frame_count << ",\n"
               << "  \"frame_interval\": " << session._desc._frame_interval << ",\n"
               << "  \"width\": " << source._width << ",\n"
               << "  \"height\": " << source._height << ",\n"
               << "  \"format\": " << static_cast<u32>(source._format) << ",\n"
               << "  \"mip_count\": " << source._mip_count << ",\n"
               << "  \"array_size\": " << source._array_size << ",\n"
               << "  \"frames\": [\n";
        for (u32 index = 0u; index < session._frames.size(); ++index)
        {
            const auto &frame = session._frames[index];
            output << "    {\"capture_index\": " << frame._capture_index
                   << ", \"engine_frame\": " << frame._engine_frame_index
                   << ", \"file\": \"" << JsonEscape(frame._file) << "\"}"
                   << (index + 1u == session._frames.size() ? "\n" : ",\n");
        }
        output << "  ],\n"
               << "  \"error\": \"" << JsonEscape(session._status._error) << "\"\n"
               << "}\n";
        return output.good();
    }
    catch (const std::exception &)
    {
        return false;
    }
}

bool WriteManifestForSession(ImageCaptureSessionId session_id)
{
    std::lock_guard manifest_lock(s_manifest_mutex);
    WString manifest_path;
    ManifestSnapshot snapshot;
    {
        std::lock_guard lock(s_mutex);
        auto iter = s_sessions.find(session_id);
        if (iter == s_sessions.end())
            return false;
        const CaptureSession &session = *iter->second;
        manifest_path = session._manifest_path;
        snapshot._desc = session._desc;
        snapshot._status = session._status;
        snapshot._has_source_signature = session._has_source_signature;
        snapshot._source_signature = session._source_signature;
        snapshot._frames = session._frames;
    }
    return WriteManifest(manifest_path, snapshot);
}

bool BuildCpuImage(const ImageCaptureReadbackData &data, ImageCaptureCpuImage &image, String &error)
{
    if (!data._success)
    {
        error = data._error.empty() ? "GPU readback failed" : data._error;
        return false;
    }
    if (data._sub_resources.empty() || data._source._format == EALGFormat::kALGFormatUNKOWN)
    {
        error = "GPU readback returned no subresources";
        return false;
    }

    image._format = data._source._format;
    image._dimension = data._source._dimension;
    image._width = data._sub_resources.front()._width;
    image._height = data._sub_resources.front()._height;
    image._depth = data._sub_resources.front()._depth;
    image._mip_count = std::max<u32>(1u, data._captured_mip_count);
    image._array_size = std::max<u32>(1u, data._captured_array_size);

    image._owned_data.reserve(data._sub_resources.size());
    image._sub_resources.reserve(data._sub_resources.size());
    for (const auto &source: data._sub_resources)
    {
        if (source._row_pitch == 0u || source._row_size == 0u || source._row_count == 0u || source._slice_pitch == 0u)
        {
            error = "GPU readback returned an invalid footprint";
            return false;
        }

        const u64 source_size = static_cast<u64>(source._slice_pitch) * source._depth;
        const u64 last_row_end = static_cast<u64>(source._row_pitch) * (source._row_count - 1u) + source._row_size;
        if (source._offset > data._data.size() || source_size > data._data.size() - source._offset ||
            last_row_end > data._data.size() - source._offset ||
            source._row_size > std::numeric_limits<u32>::max())
        {
            error = "GPU readback footprint exceeds mapped data";
            return false;
        }

        const u64 compact_size = static_cast<u64>(source._row_size) * source._row_count * source._depth;
        auto &destination = image._owned_data.emplace_back();
        destination.resize(static_cast<size_t>(compact_size));
        for (u32 depth = 0u; depth < source._depth; ++depth)
        {
            for (u32 row = 0u; row < source._row_count; ++row)
            {
                const u64 source_offset = source._offset + static_cast<u64>(depth) * source._slice_pitch +
                                           static_cast<u64>(row) * source._row_pitch;
                const u64 destination_offset = (static_cast<u64>(depth) * source._row_count + row) * source._row_size;
                if (source_offset + source._row_size > data._data.size())
                {
                    error = "GPU readback row exceeds mapped data";
                    return false;
                }
                memcpy(destination.data() + destination_offset, data._data.data() + source_offset,
                       static_cast<size_t>(source._row_size));
            }
        }

        ImageCaptureCpuSubresource &subresource = image._sub_resources.emplace_back();
        subresource._data = destination.data();
        subresource._data_size = compact_size;
        subresource._row_pitch = static_cast<u32>(source._row_size);
        subresource._slice_pitch = static_cast<u32>(source._row_size * source._row_count);
        subresource._width = source._width;
        subresource._height = source._height;
        subresource._depth = source._depth;
        subresource._mip_level = source._mip_level;
        subresource._array_slice = source._array_slice;
    }
    return true;
}
} // namespace

ImageCaptureSessionId ImageCaptureService::BeginSequence(const ImageCaptureSequenceDesc &desc)
{
    ImageCaptureSessionId session_id = 0u;
    WString output_directory;
    bool unsupported_output_format = false;
    const WString root = desc._output_directory.empty() ? L"Saved/RenderCapture" : desc._output_directory;
    {
        std::lock_guard lock(s_mutex);
        if (s_shutdown)
            return 0u;

        session_id = s_next_session_id++;
        auto session = std::make_unique<CaptureSession>();
        session->_desc = desc;
        session->_desc._frame_count = std::max<u32>(1u, desc._frame_count);
        session->_desc._frame_interval = std::max<u32>(1u, desc._frame_interval);
        session->_desc._max_in_flight_bytes = desc._max_in_flight_bytes == 0u ?
            512ull * 1024ull * 1024ull : desc._max_in_flight_bytes;
        if (session->_desc._file_prefix.empty())
            session->_desc._file_prefix = "capture";
        session->_status._session_id = session_id;
        session->_status._state = EImageCaptureState::kPending;
        session->_status._requested_frame_count = session->_desc._frame_count;
        session->_output_directory = std::filesystem::path(root) / std::format(L"session_{}", session_id);
        session->_manifest_path = std::filesystem::path(session->_output_directory) / L"capture.json";
        output_directory = session->_output_directory;
        unsupported_output_format = session->_desc._output_format != EImageCaptureOutputFormat::kDdsNative &&
                                    session->_desc._output_format != EImageCaptureOutputFormat::kPng;
        s_sessions.emplace(session_id, std::move(session));
    }

    try
    {
        std::filesystem::create_directories(std::filesystem::path(output_directory));
    }
    catch (const std::exception &exception)
    {
        ReportError(session_id, std::format("filesystem create failed: {}", exception.what()));
    }
    if (unsupported_output_format)
        ReportError(session_id, "Unsupported capture output format (only DDS and PNG are implemented)");

    const auto status = GetStatus(session_id);
    LOG_INFO("[ImageCapture] Session {} started: {} frames, interval {}", session_id,
             status._requested_frame_count, desc._frame_interval == 0u ? 1u : desc._frame_interval);
    return session_id;
}

ImageCaptureSessionId ImageCaptureService::CaptureOnce(CommandBuffer &cmd, Texture *texture,
                                                       const ImageCaptureTextureDesc &texture_desc,
                                                       const ImageCaptureSequenceDesc &sequence_desc)
{
    ImageCaptureSequenceDesc desc = sequence_desc;
    desc._frame_count = 1u;
    desc._frame_interval = 1u;
    const ImageCaptureSessionId session_id = BeginSequence(desc);
    if (session_id != 0u)
        TryCapture(session_id, cmd, texture, texture_desc);
    return session_id;
}

bool ImageCaptureService::TryCapture(ImageCaptureSessionId session_id, CommandBuffer &cmd, Texture *texture,
                                     const ImageCaptureTextureDesc &texture_desc)
{
    std::lock_guard lock(s_mutex);
    auto session_iter = s_sessions.find(session_id);
    if (session_iter == s_sessions.end())
        return false;
    CaptureSession &session = *session_iter->second;
    if (session._status._state != EImageCaptureState::kPending &&
        session._status._state != EImageCaptureState::kCapturing)
        return false;
    if (texture == nullptr || !texture->NativeResource())
    {
        SetFailedLocked(session, "invalid texture");
        LOG_ERROR("[ImageCapture] Session {} failed: invalid texture", session_id);
        return false;
    }
    if (session._status._submitted_frame_count >= session._status._requested_frame_count)
    {
        session._status._state = EImageCaptureState::kFinalizing;
        return false;
    }

    const u64 engine_frame_index = GraphicsContext::Get().GetFrameCount();
    session._has_seen_target = true;
    session._last_seen_target_frame = engine_frame_index;
    session._target_wait_frames = 0u;
    if (session._has_attempted_frame && session._last_attempted_frame == engine_frame_index)
        return false;
    session._has_attempted_frame = true;
    session._last_attempted_frame = engine_frame_index;
    if (!session._has_sequence_start_frame)
    {
        session._has_sequence_start_frame = true;
        session._sequence_start_frame = engine_frame_index;
    }
    else if (engine_frame_index < session._sequence_start_frame ||
             engine_frame_index - session._sequence_start_frame < session._desc._frame_interval ||
             (engine_frame_index - session._sequence_start_frame) % session._desc._frame_interval != 0u)
    {
        return false;
    }

    const ImageCaptureSourceSignature source = GetSourceSignature(texture);
    if (source._format == EALGFormat::kALGFormatUNKOWN || source._width == 0u || source._height == 0u)
    {
        SetFailedLocked(session, "unsupported or invalid texture descriptor");
        LOG_ERROR("[ImageCapture] Session {} failed: unsupported or invalid texture descriptor", session_id);
        return false;
    }

    const u32 capture_index = session._status._submitted_frame_count++;
    const ImageCaptureReadbackId readback_id = s_next_readback_id++;
    const ImageCaptureReadbackCallback callback = [session_id, readback_id](ImageCaptureReadbackData &&data)
    {
        data._session_id = session_id;
        data._readback_id = readback_id;
        ImageCaptureService::CompleteReadback(std::move(data));
    };
    if (!cmd.ReadbackTextureInternal(texture, texture_desc, session_id, readback_id, capture_index,
                                     engine_frame_index, callback))
    {
        SetFailedLocked(session, "readback command allocation failed");
        LOG_ERROR("[ImageCapture] Session {} failed: readback command allocation failed", session_id);
        return false;
    }
    session._status._state = EImageCaptureState::kCapturing;
    return true;
}

void ImageCaptureService::Cancel(ImageCaptureSessionId session_id)
{
    std::lock_guard lock(s_mutex);
    if (auto iter = s_sessions.find(session_id); iter != s_sessions.end())
    {
        if (iter->second->_status._state == EImageCaptureState::kCompleted ||
            iter->second->_status._state == EImageCaptureState::kFailed ||
            iter->second->_status._state == EImageCaptureState::kCancelled)
            return;
        iter->second->_status._state = EImageCaptureState::kCancelled;
        LOG_INFO("[ImageCapture] Session {} cancelled", session_id);
    }
}

ImageCaptureStatus ImageCaptureService::GetStatus(ImageCaptureSessionId session_id)
{
    std::lock_guard lock(s_mutex);
    if (auto iter = s_sessions.find(session_id); iter != s_sessions.end())
        return iter->second->_status;

    ImageCaptureStatus status;
    status._session_id = session_id;
    status._state = EImageCaptureState::kFailed;
    status._error = "invalid session";
    return status;
}

void ImageCaptureService::Tick()
{
    std::lock_guard lock(s_mutex);
    const u64 engine_frame_index = GraphicsContext::Get().GetFrameCount();
    for (auto &[session_id, session]: s_sessions)
    {
        if ((session->_status._state == EImageCaptureState::kPending ||
             session->_status._state == EImageCaptureState::kCapturing) &&
            (!session->_has_checked_wait_frame || session->_last_checked_wait_frame != engine_frame_index))
        {
            session->_has_checked_wait_frame = true;
            session->_last_checked_wait_frame = engine_frame_index;
            if (!session->_has_seen_target || session->_last_seen_target_frame != engine_frame_index)
                ++session->_target_wait_frames;
            else
                session->_target_wait_frames = 0u;
            if (session->_target_wait_frames >= kMaxTargetWaitFrames)
            {
                SetFailedLocked(*session, "Capture target was not found for 300 frames");
                LOG_ERROR("[ImageCapture] Session {} failed: target was not found for 300 frames", session_id);
            }
        }
        if (session->_status._state == EImageCaptureState::kCapturing &&
            session->_status._submitted_frame_count >= session->_status._requested_frame_count)
            session->_status._state = EImageCaptureState::kFinalizing;
        if (session->_status._state == EImageCaptureState::kFinalizing && session->_in_flight.empty() &&
            session->_status._completed_frame_count == session->_status._requested_frame_count)
        {
            session->_status._state = EImageCaptureState::kCompleted;
            LOG_INFO("[ImageCapture] Session {} completed", session_id);
        }
    }
}

void ImageCaptureService::Shutdown()
{
    std::lock_guard lock(s_mutex);
    if (s_shutdown)
        return;
    s_shutdown = true;
    for (auto &[session_id, session]: s_sessions)
    {
        if (session->_status._state != EImageCaptureState::kCompleted &&
            session->_status._state != EImageCaptureState::kFailed)
            session->_status._state = EImageCaptureState::kCancelled;
        (void)session_id;
    }
    s_sessions.clear();
}

bool ImageCaptureService::PrepareReadback(ImageCaptureSessionId session_id, ImageCaptureReadbackId readback_id,
                                          u64 byte_size)
{
    std::lock_guard lock(s_mutex);
    auto iter = s_sessions.find(session_id);
    if (iter == s_sessions.end() || byte_size == 0u)
        return false;
    CaptureSession &session = *iter->second;
    if (session._in_flight.contains(readback_id))
        return true;
    if (session._status._state == EImageCaptureState::kFailed ||
        session._status._state == EImageCaptureState::kCancelled ||
        session._status._state == EImageCaptureState::kCompleted)
        return false;
    if (session._status._in_flight_bytes > session._desc._max_in_flight_bytes ||
        byte_size > session._desc._max_in_flight_bytes - session._status._in_flight_bytes)
    {
        SetFailedLocked(session, "Image capture readback budget exceeded");
        LOG_ERROR("[ImageCapture] Session {} failed: readback budget exceeded", session_id);
        return false;
    }
    session._in_flight.emplace(readback_id, byte_size);
    session._status._in_flight_count = static_cast<u32>(session._in_flight.size());
    session._status._in_flight_bytes += byte_size;
    return true;
}

void ImageCaptureService::FailReadback(ImageCaptureSessionId session_id, ImageCaptureReadbackId readback_id,
                                       String error)
{
    std::lock_guard lock(s_mutex);
    if (auto iter = s_sessions.find(session_id); iter != s_sessions.end())
    {
        auto &session = *iter->second;
        ReleaseReadbackLocked(session, readback_id);
        SetFailedLocked(session, std::move(error));
    }
}

void ImageCaptureService::ReportError(ImageCaptureSessionId session_id, String error)
{
    std::lock_guard lock(s_mutex);
    if (auto iter = s_sessions.find(session_id); iter != s_sessions.end())
    {
        SetFailedLocked(*iter->second, std::move(error));
        LOG_ERROR("[ImageCapture] Session {} failed: {}", session_id, iter->second->_status._error);
    }
}

void ImageCaptureService::CompleteReadback(ImageCaptureReadbackData &&data)
{
    ImageCaptureSequenceDesc desc;
    WString output_directory;
    bool should_encode = false;
    {
        std::lock_guard lock(s_mutex);
        auto iter = s_sessions.find(data._session_id);
        if (iter == s_sessions.end())
            return;
        CaptureSession &session = *iter->second;
        if (session._status._state == EImageCaptureState::kCancelled ||
            session._status._state == EImageCaptureState::kFailed)
        {
            ReleaseReadbackLocked(session, data._readback_id);
            return;
        }
        if (!data._success)
        {
            ReleaseReadbackLocked(session, data._readback_id);
            SetFailedLocked(session, data._error.empty() ? "GPU readback failed" : data._error);
            return;
        }
        if (session._has_source_signature && session._source_signature != data._source)
        {
            ReleaseReadbackLocked(session, data._readback_id);
            SetFailedLocked(session, "source descriptor changed during capture");
            LOG_ERROR("[ImageCapture] Session {} failed: source descriptor changed during capture", data._session_id);
            return;
        }
        if (!session._has_source_signature)
        {
            session._has_source_signature = true;
            session._source_signature = data._source;
        }
        desc = session._desc;
        output_directory = session._output_directory;
        should_encode = true;
    }

    if (!should_encode)
        return;

    ImageCaptureCpuImage image;
    String error;
    if (!BuildCpuImage(data, image, error))
    {
        ReportError(data._session_id, std::move(error));
        ReleaseReadback(data._session_id, data._readback_id);
        return;
    }

    const bool want_png = desc._output_format == EImageCaptureOutputFormat::kPng;
    const String extension = want_png ? ".png" : ".dds";
    const String file_name = std::format("{}_{:04}{}", desc._file_prefix, data._capture_index, extension);
    const WString file_path = std::filesystem::path(output_directory) / file_name;
    DdsCaptureEncoder dds_encoder;
    PngCaptureEncoder png_encoder;
    ImageCaptureEncoder &encoder = want_png ? static_cast<ImageCaptureEncoder &>(png_encoder)
                                             : static_cast<ImageCaptureEncoder &>(dds_encoder);
    if (!encoder.Encode(image, file_path, error))
    {
        ReportError(data._session_id, std::format("{} encode failed: {}", want_png ? "PNG" : "DDS", error));
        ReleaseReadback(data._session_id, data._readback_id);
        return;
    }

    bool write_manifest = false;
    {
        std::lock_guard lock(s_mutex);
        auto iter = s_sessions.find(data._session_id);
        if (iter == s_sessions.end())
            return;
        CaptureSession &session = *iter->second;
        if (session._status._state == EImageCaptureState::kCancelled ||
            session._status._state == EImageCaptureState::kFailed)
        {
            ReleaseReadbackLocked(session, data._readback_id);
            return;
        }
        session._status._completed_frame_count++;
        session._frames.push_back({data._capture_index, data._engine_frame_index, file_name});
        write_manifest = desc._write_manifest;
        ReleaseReadbackLocked(session, data._readback_id);
        if (session._status._state == EImageCaptureState::kCapturing &&
            session._status._submitted_frame_count >= session._status._requested_frame_count)
            session._status._state = EImageCaptureState::kFinalizing;
        if (session._status._state == EImageCaptureState::kFinalizing && session._in_flight.empty() &&
            session._status._completed_frame_count == session._status._requested_frame_count)
        {
            session._status._state = EImageCaptureState::kCompleted;
            LOG_INFO("[ImageCapture] Session {} completed", data._session_id);
        }
    }
    if (write_manifest && !WriteManifestForSession(data._session_id))
    {
        ReportError(data._session_id, "filesystem write failed for capture manifest");
        return;
    }
    LOG_INFO("[ImageCapture] Session {} frame {} saved: {}", data._session_id, data._capture_index, file_name);
}
} // namespace Ailu::Render
