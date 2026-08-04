#pragma once

#include "FrameCaptureTypes.h"
#include "FrameCaptureReason.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Array.h"
#include "Render/RenderingStates.h"

namespace Ailu::Render::FrameDebugger
{
class FrameCaptureArena
{
public:
    FrameCaptureArena() = default;
    explicit FrameCaptureArena(u64 capacity) : _capacity(capacity) {}

    void *Allocate(u64 size, u64 alignment)
    {
        if (_overflowed) return nullptr;
        u64 aligned_offset = (_offset + alignment - 1u) & ~(alignment - 1u);
        if (aligned_offset + size > _capacity)
        {
            _overflowed = true;
            return nullptr;
        }
        _offset = aligned_offset + size;
        return nullptr;
    }

    template<typename T>
    T *Allocate(u32 count = 1u)
    {
        return static_cast<T *>(Allocate(sizeof(T) * count, alignof(T)));
    }

    u64 UsedSize() const { return _offset; }
    u64 Capacity() const { return _capacity; }
    bool Overflowed() const { return _overflowed; }

private:
    u64 _capacity = 0u;
    u64 _offset = 0u;
    bool _overflowed = false;
};

class CaptureStringTable
{
public:
    CaptureStringTable() = default;

    CaptureStringId Intern(StringView text)
    {
        if (text.empty()) return 0u;
        u64 hash = HashString(text);
        for (u32 i = 0u; i < _count; ++i)
        {
            if (_hashes[i] == hash && strncmp(_strings[i], text.data(), text.size()) == 0 && _strings[i][text.size()] == '\0')
                return i + 1u;
        }
        if (_count >= kMaxEntries) return 0u;
        u32 id = _count + 1u;
        u32 len = (u32)text.size();
        if (len >= kStrLen) len = kStrLen - 1u;
        memcpy(_strings[_count], text.data(), len);
        _strings[_count][len] = '\0';
        _hashes[_count] = hash;
        ++_count;
        return id;
    }

    StringView Get(CaptureStringId id) const
    {
        if (id == 0u || id > _count) return "";
        return _strings[id - 1u];
    }

    u32 Count() const { return _count; }
    void Clear() { _count = 0u; }

private:
    static u64 HashString(StringView s)
    {
        u64 h = 0x100;
        for (char c : s) h = h * 31u + (u8)c;
        return h;
    }

    static constexpr u32 kMaxEntries = 2048u;
    static constexpr u32 kStrLen = 128u;
    char _strings[kMaxEntries][kStrLen]{};
    u64 _hashes[kMaxEntries]{};
    u32 _count = 0u;
};

class CaptureObjectTable
{
public:
    CaptureObjectTable() = default;

    CaptureObjectId RegisterOrGet(void *runtime_ptr, ECaptureObjectType type, CaptureStringId name_id)
    {
        u64 key = (u64)runtime_ptr;
        for (u32 i = 0u; i < _count; ++i)
        {
            if (_keys[i] == key) return i + 1u;
        }
        if (_count >= kMaxEntries) return 0u;
        u32 id = _count + 1u;
        _objects[_count] = {id, type, name_id, key, 0u};
        _keys[_count] = key;
        ++_count;
        return id;
    }

    const CaptureObjectInfo *Get(CaptureObjectId id) const
    {
        if (id == 0u || id > _count) return nullptr;
        return &_objects[(u32)id - 1u];
    }

    u32 Count() const { return _count; }
    void Clear() { _count = 0u; }

private:
    static constexpr u32 kMaxEntries = 1024u;
    CaptureObjectInfo _objects[kMaxEntries]{};
    u64 _keys[kMaxEntries]{};
    u32 _count = 0u;
};

struct FrameCaptureChunk
{
    u32 _submission_index = 0u;
    u32 _chunk_index = 0u;
    CaptureStringId _command_group_name = 0u;
    u32 _render_pass_capture_id = 0u;

    Vector<FrameEvent> _events;
    Vector<DrawEventCapture> _draws;
    Vector<DispatchEventCapture> _dispatches;
    Vector<PipelineBindingCapture> _bindings;
    Vector<ResourceBarrierCapture> _barriers;
    Vector<GeometryBindingCapture> _geometry_bindings;
    Vector<MaterialBindingCapture> _material_bindings;
    Vector<PsoLookupCapture> _pso_lookups;
    Vector<RenderGraphPassCapture> _rg_passes;
    Vector<RenderGraphResourceAccessCapture> _rg_resource_accesses;

    CaptureStringTable _strings;
    CaptureObjectTable _objects;
};

class FrameCapture final
{
public:
    FrameCapture() = default;

    const Vector<FrameEvent> &Events() const { return _events; }
    const Vector<DrawEventCapture> &Draws() const { return _draws; }
    const Vector<DispatchEventCapture> &Dispatches() const { return _dispatches; }
    const Vector<PipelineBindingCapture> &Bindings() const { return _bindings; }
    const Vector<ResourceBarrierCapture> &Barriers() const { return _barriers; }
    const Vector<GeometryBindingCapture> &GeometryBindings() const { return _geometry_bindings; }
    const Vector<MaterialBindingCapture> &MaterialBindings() const { return _material_bindings; }
    const Vector<PsoLookupCapture> &PsoLookups() const { return _pso_lookups; }
    const Vector<RenderGraphPassCapture> &RGPasses() const { return _rg_passes; }
    const Vector<RenderGraphResourceAccessCapture> &RGResourceAccesses() const { return _rg_resource_accesses; }

    CaptureStringTable &Strings() { return _strings; }
    const CaptureStringTable &Strings() const { return _strings; }
    CaptureObjectTable &Objects() { return _objects; }
    const CaptureObjectTable &Objects() const { return _objects; }

    const FrameCaptureStatistics &Statistics() const { return _statistics; }
    FrameCaptureStatistics &Statistics() { return _statistics; }

    void CalculateStatistics();

private:
    friend class FrameCaptureFinalizer;

    Vector<FrameEvent> _events;
    Vector<DrawEventCapture> _draws;
    Vector<DispatchEventCapture> _dispatches;
    Vector<PipelineBindingCapture> _bindings;
    Vector<ResourceBarrierCapture> _barriers;
    Vector<GeometryBindingCapture> _geometry_bindings;
    Vector<MaterialBindingCapture> _material_bindings;
    Vector<PsoLookupCapture> _pso_lookups;
    Vector<RenderGraphPassCapture> _rg_passes;
    Vector<RenderGraphResourceAccessCapture> _rg_resource_accesses;

    CaptureStringTable _strings;
    CaptureObjectTable _objects;

    FrameCaptureStatistics _statistics;
};

} // namespace Ailu::Render::FrameDebugger
