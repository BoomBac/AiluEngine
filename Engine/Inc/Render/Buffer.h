#pragma once
#ifndef __BUFFER_H__
#define __BUFFER_H__

#include "Framework/Common/Allocator.hpp"
#include "GlobalMarco.h"
#include "GpuResource.h"
#include "PipelineState.h"
#include "Framework/Common/Misc.h"
#include <functional>

namespace Ailu::Render
{
    class CommandBuffer;
    namespace
    {
        static u32 s_global_buffer_index = 0u;
    }
    enum EGPUBufferTarget
    {
        kVertex = 1,
        kIndex = 2,
        kCopySource = 4,
        kCopyDestination = 8,
        kStructured = 0x10,
        //
        // Summary:
        //     GraphicsBuffer can be used as a raw byte-address buffer.
        kRaw = 0x20,
        //
        // Summary:
        //     GraphicsBuffer can be used as an append-consume buffer.
        kAppend = 0x40,
        kCounter = 0x80,
        kIndirectArguments = 0x100,
        kConstant = 0x200
    };

    struct DispatchArguments
    {
        u32 _thread_group_num_x;
        u32 _thread_group_num_y;
        u32 _thread_group_num_z;
    };

    struct DrawArguments
    {
        u32 _vertex_count_per_instance;
        u32 _instance_count;
        u32 _start_vertex_location;
        u32 _start_instance_location;
    };

    struct DrawIndexedArguments
    {
        u32 _index_count_per_instance;
        u32 _instance_count;
        i32 _base_vertex_location;
        u32 _start_index_location;
        u32 _start_instance_location;
    };

    struct GPUBufferDesc
    {
        u32 _size = 0u;
        //for structured buffer
        u32 _element_num = 1u;
        u32 _element_size = 0u;
        bool _is_readable = false;
        bool _is_random_write = true;
        EALGFormat::EALGFormat _format = EALGFormat::kALGFormatUNKOWN;
        EGPUBufferTarget _target = EGPUBufferTarget::kConstant;
    };
    class AILU_API GPUBuffer : public GpuResource
    {
    public:
        static GPUBuffer *Create(GPUBufferDesc desc, const String &name = std::format("common_buffer_{}", s_global_buffer_index++));
        static GPUBuffer *Create(EGPUBufferTarget target, u32 element_size, u32 element_num, const String &name = std::format("common_buffer_{}", s_global_buffer_index++));
        GPUBuffer(GPUBufferDesc desc) : _desc(desc)
        {
            _desc._is_random_write |= static_cast<bool>(desc._target & EGPUBufferTarget::kAppend);
            _res_type = EGpuResType::kBuffer;
        }
        virtual ~GPUBuffer() = default;
        virtual void ReadBack(u8 *dst, u32 size) {};
        virtual void ReadBackAsync(u8 *dst, u32 size, std::function<void()> on_complete) {};
        [[nodiscard]] bool IsRandomAccess() const { return _desc._is_random_write; };
        /// @brief  异步获取append/consume中元素个数，可能会延迟数帧
        /// @return
        virtual void GetCounter(std::function<void(u32)> callback) { };
        virtual void SetCounter(u32 counter) { _counter = counter; };
        template<typename T>
        void SetData(const Vector<T> &data)
        {
            u64 data_size = sizeof(T) * data.size();
            AL_ASSERT(data_size <= _mem_size);
            AL_ASSERT(sizeof(T) == _desc._element_size);
            if (_data)
            {
                AL_FREE(_data);
                _data = nullptr;
            }
            _fill_data_size = data_size;
            _data = reinterpret_cast<u8 *>(AL_ALLOC(T, data.size()));
            memcpy(_data, data.data(), data_size);
            _counter = (u32) data.size();
            OnDataChanged();
        }
        void SetData(const u8 *data, u32 size)
        {
            AL_ASSERT(size <= _mem_size);
            if (_data)
            {
                AL_FREE(_data);
                _data = nullptr;
            }
            _fill_data_size = size;
            _data = reinterpret_cast<u8 *>(AL_ALLOC(u8, size));
            memcpy(_data, data, size);
            _counter = size / _desc._element_size;
            OnDataChanged();
        }

    protected:
        virtual void OnDataChanged() {};

    protected:
        GPUBufferDesc _desc;
        //for append/consume buffer
        u32 _counter = 0u;
        u8 *_data = nullptr;
        //每次设置data时的实际大小，使用容量拷贝时可能会有越界错误
        u64 _fill_data_size = 0u;
    };

    class VertexBuffer : public GpuResource
    {
    public:
        static VertexBuffer *Create(VertexBufferLayout layout, const String &name = std::format("vertex_buffer_{}", s_global_buffer_index++));
        VertexBuffer(VertexBufferLayout layout);
        virtual ~VertexBuffer() = default;
        void SetStream(u8 *data, u32 size, u8 stream_index, bool is_dynamic);
        void SetData(u8 *data, u32 size, u8 stream_index, u32 offset);
        u8 *GetStream(u8 index) { return _mapped_data[index]; };
        void SetLayout(VertexBufferLayout layout) { _buffer_layout = std::move(layout); };
        [[nodiscard]] const VertexBufferLayout &GetLayout() const { return _buffer_layout; };
        u32 GetVertexCount() const { return _vertices_count; };

    protected:
        VertexBufferLayout _buffer_layout;
        u32 _vertices_count;
        //只读
        struct StreamData
        {
            u8 *_data;
            u64 _size;
            bool _is_dynamic;
        };
        Vector<StreamData> _stream_data;
        //just for dynamic buffer
        Vector<u8 *> _mapped_data;
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
        [[nodiscard]] u32 GetCount() const { return _count; };
        u8 *GetData() { return _data; };
        void SetData(u8 *data, u32 size);
        virtual void Resize(u32 new_size) = 0;

    protected:
        u32 _capacity;//最大可容纳的个数
        u32 _count;   //当前索引个数
        bool _is_dynamic;
        u8 *_data = nullptr;
    };

    class ConstantBuffer : public GpuResource
    {
    public:
        static ConstantBuffer *Create(u32 size, const String &name = std::format("const_buffer_{}", s_global_buffer_index++));
        static void Release(ConstantBuffer *ptr);
        ConstantBuffer() { _res_type = EGpuResType::kConstBuffer; };
        virtual ~ConstantBuffer() = default;
        virtual u8 *GetData() { return _data; };
        void SetData(const u8 *data, u32 size = 0u)
        {
            size = size == 0u ? (u32) _mem_size : std::min<u32>(size, (u32) _mem_size);
            memcpy(_data, data, size);
        }
        virtual void Reset() {};
        template<typename T>
        static T *As(ConstantBuffer *buffer)
        {
            return reinterpret_cast<T *>(buffer->GetData());
        }

    protected:
        u8 *_data = nullptr;
    };

    class ConstBufferPool : public Ailu::Core::NonCopyble
    {
    public:
        static void Init();
        static void ShutDown();
        static ConstantBuffer* Acquire(u32 size);
    private:
        struct ConstbufferNode
        {
            ConstantBuffer *_buffer;
            u64 _access_frame_count = 0u;
        };
        std::multimap<u32,ConstbufferNode> _buffer_pool;
    };

}// namespace Ailu

#endif// !BUFFER_H__
