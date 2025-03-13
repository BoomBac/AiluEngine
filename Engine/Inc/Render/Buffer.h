#pragma once
#ifndef __BUFFER_H__
#define __BUFFER_H__

#include "GlobalMarco.h"
#include "PipelineState.h"
#include "GpuResource.h"
#include <functional>

namespace Ailu
{
    class CommandBuffer;
    namespace
    {
        static u32 s_global_buffer_index = 0u;
    }
    struct GPUBufferDesc
    {
        u32 _size = 0u;
        //for structured buffer
        u32 _element_num = 1u;
        bool _is_readable = false;
        bool _is_random_write = false;
        EALGFormat::EALGFormat _format = EALGFormat::kALGFormatUnknown;
    };
    class AILU_API IGPUBuffer
    {
    public:
        virtual ~IGPUBuffer() = default;
        virtual void Bind(CommandBuffer *cmd, u8 bind_slot, bool is_compute_pipeline = false) = 0;
        virtual u8 *ReadBack() = 0;
        virtual void ReadBack(u8 *dst, u32 size) = 0;
        virtual void ReadBackAsync(u8 *dst, u32 size, std::function<void()> on_complete) = 0;
    };
    class AILU_API GPUBuffer : public IGPUBuffer, public GpuResource
    {
    public:
        static GPUBuffer *Create(GPUBufferDesc desc, const String &name = std::format("common_buffer_{}", s_global_buffer_index++));
        GPUBuffer(GPUBufferDesc desc) : _desc(desc) {}
        virtual ~GPUBuffer() = default;
        virtual void Bind(CommandBuffer *cmd, u8 bind_slot, bool is_compute_pipeline = false){};
        virtual u8 *ReadBack() {return nullptr;};
        virtual void ReadBack(u8 *dst, u32 size) {};
        virtual void ReadBackAsync(u8 *dst, u32 size, std::function<void()> on_complete) {};
        [[nodiscard]] bool IsRandomAccess() const {return _desc._is_random_write;};
    protected:
        GPUBufferDesc _desc;
    };

    class AILU_API IVertexBuffer
    {
    public:
        static IVertexBuffer *Create(VertexBufferLayout layout, const String &name = std::format("vertex_buffer_{}", s_global_buffer_index++));
        virtual ~IVertexBuffer() = default;
        virtual void Bind(CommandBuffer *cmd, const VertexBufferLayout &pipeline_input_layout) = 0;
        virtual void SetLayout(VertexBufferLayout layout) = 0;
        virtual void SetStream(float *vertices, u32 size, u8 stream_index) = 0;
        virtual void SetStream(u8 *data, u32 size, u8 stream_index, bool dynamic) = 0;
        virtual void SetData(u8 *data, u32 size, u8 stream_index, u32 offset) = 0;
        virtual void SetName(const String &name) = 0;
        virtual u8 *GetStream(u8 index) = 0;
        [[nodiscard]] virtual const VertexBufferLayout &GetLayout() const = 0;
        virtual u32 GetVertexCount() const = 0;
    };

    class AILU_API IIndexBuffer
    {
    public:
        static IIndexBuffer *Create(u32 *indices, u32 count, const String &name = std::format("index_buffer_{}", s_global_buffer_index++), bool is_dynamic = false);
        virtual ~IIndexBuffer() = default;
        virtual void SetName(const String &name) = 0;
        virtual void Bind(CommandBuffer *cmd) = 0;
        [[nodiscard]] virtual u32 GetCount() const = 0;
        virtual u8 *GetData() = 0;
        virtual void SetData(u8 *data, u32 size) = 0;
        virtual void Resize(u32 new_size) = 0;
    };

    class IConstantBuffer
    {
    public:
        virtual ~IConstantBuffer() = default;
        virtual void Bind(CommandBuffer *cmd, u8 bind_slot, bool is_compute_pipeline) = 0;
        virtual u8 *GetData() = 0;
        [[nodiscard]] virtual u64 GetBufferSize() const = 0;
        virtual void Reset() = 0;
    };

    class ConstantBuffer : public IConstantBuffer,public GpuResource
    {
    public:
        static ConstantBuffer *Create(u32 size, bool compute_buffer = false, const String &name = std::format("const_buffer_{}", s_global_buffer_index++));
        static void Release(u8 *ptr);
        virtual ~ConstantBuffer() = default;
        virtual void Bind(CommandBuffer *cmd, u8 bind_slot, bool is_compute_pipeline = false) override{};
        virtual u8 *GetData() override {return _data;};
        [[nodiscard]]virtual u64 GetBufferSize() const override {return _size;};
        virtual void Reset() override{};
        template<typename T>
        static T *As(ConstantBuffer *buffer)
        {
            return reinterpret_cast<T *>(buffer->GetData());
        }
    protected:
        u8* _data = nullptr;
        u64 _size = 0u;
    };

}// namespace Ailu

#endif// !BUFFER_H__
