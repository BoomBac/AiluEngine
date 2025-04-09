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
    class AILU_API GPUBuffer : public GpuResource
    {
    public:
        static GPUBuffer *Create(GPUBufferDesc desc, const String &name = std::format("common_buffer_{}", s_global_buffer_index++));
        GPUBuffer(GPUBufferDesc desc) : _desc(desc) {
            _res_type = EGpuResType::kBuffer;
        }
        virtual ~GPUBuffer() = default;
        virtual void ReadBack(u8 *dst, u32 size) {};
        virtual void ReadBackAsync(u8 *dst, u32 size, std::function<void()> on_complete) {};
        [[nodiscard]] bool IsRandomAccess() const {return _desc._is_random_write;};
    protected:
        GPUBufferDesc _desc;
    };

    struct BindParamsVB : public BindParams
    {
        const VertexBufferLayout* _layout;
    };

    class VertexBuffer : public GpuResource
    {
    public:
        static VertexBuffer *Create(VertexBufferLayout layout, const String &name = std::format("vertex_buffer_{}", s_global_buffer_index++));
        VertexBuffer(VertexBufferLayout layout);
        virtual ~VertexBuffer() = default;
        void SetStream(u8 *data, u32 size, u8 stream_index, bool is_dynamic);
        void SetData(u8 *data, u32 size, u8 stream_index, u32 offset);
		u8* GetStream(u8 index) {return _mapped_data[index];};
        void SetLayout(VertexBufferLayout layout) {_buffer_layout = std::move(layout);};
        [[nodiscard]] const VertexBufferLayout &GetLayout() const {return _buffer_layout;};
        u32 GetVertexCount() const {return _vertices_count;};
    protected:
        VertexBufferLayout _buffer_layout;
        u32 _vertices_count;
        //只读
        struct StreamData
        {
            u8* _data;
            u64 _size;
            bool _is_dynamic;
        };
        Vector<StreamData> _stream_data;
        //just for dynamic buffer
        Vector<u8*> _mapped_data;
        std::map<String, u8> _buffer_layout_indexer;
    };

    class IndexBuffer : public GpuResource
    {
    public:
        static IndexBuffer *Create(u32 *indices, u32 count, const String &name = std::format("index_buffer_{}", s_global_buffer_index++), bool is_dynamic = false);
        IndexBuffer(u32 *indices, u32 count, bool is_dynamic);
        virtual ~IndexBuffer() = default;
        /// @brief 返回当前索引的数量
        /// @return 
        [[nodiscard]] u32 GetCount() const {return _count;};
        u8 *GetData() {return _data;};
        void SetData(u8 *data, u32 size);
        virtual void Resize(u32 new_size) = 0;
    protected:
        u32 _capacity;//最大可容纳的个数
        u32 _count;//当前索引个数
        bool _is_dynamic;
        u8* _data = nullptr;
    };

    class ConstantBuffer : public GpuResource
    {
    public:
        static ConstantBuffer *Create(u32 size, bool compute_buffer = false, const String &name = std::format("const_buffer_{}", s_global_buffer_index++));
        static void Release(ConstantBuffer* ptr);
        ConstantBuffer() {_res_type = EGpuResType::kConstBuffer;};
        virtual ~ConstantBuffer() = default;
        virtual u8 *GetData() {return _data;};
        void SetData(const u8* data,u32 size = 0u)
        {
            size = size == 0u? (u32)_mem_size : std::min<u32>(size,(u32)_mem_size);
            memcpy(_data,data,size);
        }
        virtual void Reset() {};
        template<typename T>
        static T *As(ConstantBuffer *buffer)
        {
            return reinterpret_cast<T *>(buffer->GetData());
        }
    protected:
        u8* _data = nullptr;
    };

}// namespace Ailu

#endif// !BUFFER_H__
