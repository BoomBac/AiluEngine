#pragma once

#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/String.h"
#include "Render/Texture.h"

#include <functional>

namespace Ailu::Render
{
using ImageCaptureSessionId = u64;
using ImageCaptureReadbackId = u64;

enum class EImageCaptureOutputFormat : u8
{
    kDdsNative,
    kPng,
    kHdr
};

enum class EImageCaptureState : u8
{
    kPending,
    kCapturing,
    kFinalizing,
    kCompleted,
    kCancelled,
    kFailed
};

struct ImageCaptureSequenceDesc
{
    u32 _frame_count = 1u;
    u32 _frame_interval = 1u;
    EImageCaptureOutputFormat _output_format = EImageCaptureOutputFormat::kDdsNative;
    WString _output_directory;
    String _file_prefix = "capture";
    u64 _max_in_flight_bytes = 512ull * 1024ull * 1024ull;
    bool _write_manifest = true;
};

struct ImageCaptureTextureDesc
{
    String _debug_name;
    bool _all_sub_resources = true;
    u32 _mip_level = 0u;
    u32 _mip_count = 1u;
    u32 _array_slice = 0u;
    u32 _array_slice_count = 1u;
    bool _has_explicit_restore_state = false;
    EResourceState _restore_state = EResourceState::kCommon;
};

struct ImageCaptureSourceSignature
{
    EALGFormat _format = EALGFormat::kALGFormatUNKOWN;
    ETextureDimension _dimension = ETextureDimension::kUnknown;
    u32 _width = 0u;
    u32 _height = 0u;
    u32 _depth = 1u;
    u32 _mip_count = 1u;
    u32 _array_size = 1u;

    bool operator==(const ImageCaptureSourceSignature &) const = default;
};

struct ImageCaptureStatus
{
    ImageCaptureSessionId _session_id = 0u;
    EImageCaptureState _state = EImageCaptureState::kPending;
    u32 _requested_frame_count = 0u;
    u32 _submitted_frame_count = 0u;
    u32 _completed_frame_count = 0u;
    u32 _in_flight_count = 0u;
    u64 _in_flight_bytes = 0u;
    String _error;
};

struct ImageCaptureCpuSubresource
{
    const u8 *_data = nullptr;
    u64 _data_size = 0u;
    u32 _row_pitch = 0u;
    u32 _slice_pitch = 0u;
    u32 _width = 0u;
    u32 _height = 0u;
    u32 _depth = 1u;
    u32 _mip_level = 0u;
    u32 _array_slice = 0u;
};

struct ImageCaptureCpuImage
{
    EALGFormat _format = EALGFormat::kALGFormatUNKOWN;
    ETextureDimension _dimension = ETextureDimension::kUnknown;
    u32 _width = 0u;
    u32 _height = 0u;
    u32 _depth = 1u;
    u32 _mip_count = 1u;
    u32 _array_size = 1u;
    // Owns the compacted rows referenced by _sub_resources until encoding completes.
    Vector<Vector<u8>> _owned_data;
    Vector<ImageCaptureCpuSubresource> _sub_resources;
};

// This is deliberately a backend-neutral description of a mapped D3D12 readback. The DX12 layer fills it in,
// then ImageCaptureService compacts row padding before passing an ImageCaptureCpuImage to an encoder.
struct ImageCaptureReadbackSubresource
{
    u64 _offset = 0u;
    u64 _row_size = 0u;
    u32 _row_pitch = 0u;
    u32 _row_count = 0u;
    u32 _slice_pitch = 0u;
    u32 _width = 0u;
    u32 _height = 0u;
    u32 _depth = 1u;
    u32 _mip_level = 0u;
    u32 _array_slice = 0u;
};

struct ImageCaptureReadbackData
{
    ImageCaptureSessionId _session_id = 0u;
    ImageCaptureReadbackId _readback_id = 0u;
    u32 _capture_index = 0u;
    u64 _engine_frame_index = 0u;
    ImageCaptureSourceSignature _source;
    u32 _captured_mip_count = 1u;
    u32 _captured_array_size = 1u;
    bool _success = true;
    String _error;
    Vector<u8> _data;
    Vector<ImageCaptureReadbackSubresource> _sub_resources;
};

using ImageCaptureReadbackCallback = std::function<void(ImageCaptureReadbackData &&data)>;

} // namespace Ailu::Render
