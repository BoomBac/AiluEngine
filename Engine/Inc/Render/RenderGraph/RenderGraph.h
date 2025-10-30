#pragma once
#ifndef __RENDER_GRAPH__
#define __RENDER_GRAPH__

#include "Render/Buffer.h"
#include "Render/ResourcePool.h"
#include "Render/Texture.h"
#include "RenderGraphFwd.h"

namespace Ailu::Render
{
    struct RenderingData;
}

namespace std
{
    template<>
    struct hash<Ailu::Render::RDG::RGHandle>
    {
        u64 operator()(const Ailu::Render::RDG::RGHandle &handle) const
        {
            return handle._id ^ handle._version;
        }
    };
}// namespace std

namespace Ailu::Render
{
    class CommandBuffer;
}

namespace Ailu::Render::RDG
{
    struct ResourceAccess
    {
        EResourceUsage _usage;
        ELoadStoreAction _load = ELoadStoreAction::kLoad;
        ELoadStoreAction _store = ELoadStoreAction::kStore;
        ClearValue _clear_value = ClearValue::Color(0.0f, 0.0f, 0.0f, 0.0f);
        u32 _mip_level = 0u;
        u32 _array_slice = 0u;

        bool isWrite() const
        {
            return static_cast<uint32_t>(_usage & (EResourceUsage::kWriteUAV |
                                                   EResourceUsage::kWriteRTV | EResourceUsage::kDSV |
                                                   EResourceUsage::kCopyDst)) != 0;
        }

        bool isRead() const
        {
            return static_cast<uint32_t>(_usage & (EResourceUsage::kReadSRV |
                                                   EResourceUsage::kCopySrc | EResourceUsage::kIndirectArgument)) != 0;
        }
    };

    class RenderGraphBuilder;
    class RenderGraph;

    using SetupFunction = std::function<void(RenderGraphBuilder &builder)>;
    using ExecuteFunction = std::function<void(RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)>;

    class AILU_API RenderPass
    {
    public:
        friend class RenderGraph;
        static void Reset(RenderPass *pass)
        {
            if (pass)
            {
                pass->_input_handles.clear();
                pass->_output_handles.clear();
                pass->_callback = nullptr;
                pass->_name = "noname";
            }
        }
        RenderPass() : RenderPass("noname") {};
        RenderPass(const String &name, EPassType type = EPassType::kGraphics) : _name(name), _type(type) {};
        void Read(RGHandle handle)
        {
            if (std::find(_input_handles.begin(), _input_handles.end(), handle) == _input_handles.end())
            {
                _input_handles.push_back(handle);
            }
        }
        void Write(RGHandle handle)
        {
            if (std::find(_output_handles.begin(), _output_handles.end(), handle) == _output_handles.end())
            {
                _output_handles.push_back(handle);
            }
        }
        void SetCallback(ExecuteFunction callback)
        {
            _callback = std::move(callback);
        }
        void Execute(RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
        {
            if (_callback)
            {
                _callback(graph, cmd, data);
            }
        }
        //private:
        String _name;
        EPassType _type;
        Vector<RGHandle> _input_handles; // 输入资源句柄
        Vector<RGHandle> _output_handles;// 输出资源句柄
        ExecuteFunction _callback;
    };

    class AILU_API RenderGraph
    {
        friend class RenderGraphBuilder;
    public:
        RenderGraph();
        ~RenderGraph();
        /// <summary>
        /// 只能在setup阶段调用
        /// </summary>
        /// <param name="desc"></param>
        /// <param name="name"></param>
        /// <returns></returns>
        RGHandle CreateResource(const TextureDesc &desc, const String &name);
        RGHandle CreateResource(const BufferDesc &desc, const String &name);
        RGHandle Import(GpuResource *external);
        GpuResource *Export(RGHandle handle);

        RGHandle GetTexture(const String &name);
        RGHandle GetBuffer(const String &name);

        // 添加一个渲染Pass
        void AddPass(const String &name, PassDesc desc, SetupFunction setup_func, ExecuteFunction executor);

        // 编译RenderGraph，解析资源依赖关系
        bool Compile();

        void EndFrame();

        // 执行RenderGraph
        void Execute(GraphicsContext &context, const RenderingData &data);

        template<typename T>
        T *Resolve(RGHandle handle)
        {
            if (auto node = GetResourceNode(handle); node != nullptr)
            {
                return dynamic_cast<T *>(node->GetResource());
            }
            return nullptr;
        }

    public:
        Vector<RenderPass> _debug_passes;
        bool _is_debug = false;

    private:
        struct ResourceNode;
        RGHandle *FindHandlePtr(RGHandle handle);
        void CreatePhysicalResources(RGHandle handle);
        Texture *CreatePhysicsTexture(RGHandle handle);
        GPUBuffer *CreatePhysicsBuffer(RGHandle handle);
        // 拓扑排序Pass执行顺序
        void TopologicalSort();

        // 插入资源屏障
        void InsertResourceBarriers(GraphicsContext *context);

        ResourceNode *GetResourceNode(RGHandle handle)
        {
            if (_external_resources.contains(handle._id))
                return &_external_resources[handle._id];
            if (_transient_resources.contains(handle._id))
                return &_transient_resources[handle._id];
            return nullptr;
        }
        //export to a .dot file for graphviz
        void ExportToFile(const WString &filename);
    private:
    private:
        using PassPool = TResourcePool<RenderPass>;
        using TexturePool = THashableResourcePool<TextureDesc, Texture>;
        using BufferPool = THashableResourcePool<BufferDesc, GPUBuffer>;
        struct ResourceNode
        {
        public:
            ResourceNode() {}
            ResourceNode(StringView name):_name(name) {};
            ResourceNode(const TextureDesc &desc, StringView name) : _tex_desc(desc), _name(name), _is_transient(true)
            {
                _is_tex = true;
                _is_render_output = ((bool) desc._is_color_target) | ((bool) desc._is_depth_target);
                _is_external = false;
            };
            ResourceNode(const BufferDesc &desc, StringView name) :_buffer_desc(desc), _name(name), _is_transient(true)
            {
                _is_tex = false;
                _is_render_output = false;
                _is_external = false;
            };
            GpuResource* GetResource() const
            {
                if (_is_external)
                    return _extern_raw_res;
                if (_is_tex)
                    return _pool_tex_handle._res;
                else
                    return _pool_buffer_handle._res;
            }
            String _name;
            union
            {
                TextureDesc _tex_desc;
                BufferDesc _buffer_desc;
            };
            union
            {
                TexturePool::PoolResourceHandle _pool_tex_handle;
                BufferPool::PoolResourceHandle _pool_buffer_handle;
                GpuResource * _extern_raw_res;
            };
            i16 _first_write = -1;
            i16 _last_read = -1;
            union
            {
                struct
                {
                    u32 _is_tex : 1;
                    u32 _is_valid : 1;
                    u32 _is_render_output : 1;
                    u32 _is_transient : 1;
                    u32 _is_external : 1;
                };
                u32 _flag;
            };
            bool _is_allocated = false;//是否创建了物理资源
            RGHandle *_handle_ptr = nullptr;
        };
        inline static std::atomic<u32> s_next_handle_id = 0u;
        std::mutex _mutex;
        Vector<RenderPass *> _passes;
        Vector<RenderPass *> _sorted_passes;
        //存储所有临时资源，重新编译前清空，setup阶段就分配的句柄，execute阶段直接根据句柄创建或者获取物理资源
        HashMap<String, RGHandle> _transient_tex_handles;
        HashMap<String, RGHandle> _transient_buffer_handles;
        HashMap<u32, ResourceNode> _transient_resources;
        //外部资源句柄，通常是从外部传入的资源，RenderGraph不会管理其生命周期，新资源会覆盖旧资源
        HashMap<String, RGHandle> _external_tex_handles;
        HashMap<String, RGHandle> _external_buffer_handles;
        HashMap<u32, ResourceNode> _external_resources;
        PassPool _pass_pool{64u};
        bool _is_compiled = false;
    };

    class AILU_API RenderGraphBuilder
    {
    public:
        RenderGraphBuilder(RenderGraph &graph, RenderPass &pass) : _graph(&graph), _pass(&pass) {};
        void Read(RGHandle handle,ResourceAccess accessor)
        {
            _pass->Read(handle);
        }

        [[nodiscard]] RGHandle Write(RGHandle handle, ResourceAccess accessor)
        {
            if (auto handle_ptr = _graph->FindHandlePtr(handle); handle_ptr != nullptr)
            {
                handle_ptr->_version++;
                RGHandle new_handle = *handle_ptr;

                _pass->Write(new_handle);
                return new_handle;
            }
            else
            {
                LOG_ERROR("RenderGraphBuilder::Write: Invalid handle!");
                return RGHandle{0u};
            }
        }

        void Read(RGHandle handle,EResourceUsage usage = EResourceUsage::kReadSRV)
        {
            ResourceAccess accessor;
            accessor._usage = usage;
            Read(handle, accessor);
        }

        [[nodiscard]] RGHandle Write(RGHandle handle, EResourceUsage usage = EResourceUsage::kWriteRTV)
        {
            ResourceAccess accessor;
            accessor._usage = usage;
            return Write(handle, accessor);
        }

        RGHandle GetTexture(const String &name)
        {
            return _graph->GetTexture(name);
        }
        RGHandle GetBuffer(const String &name)
        {
            return _graph->GetBuffer(name);
        }

        RGHandle Import(GpuResource *res)
        {
            return _graph->Import(res);
        }

        void SetCallback(ExecuteFunction callback)
        {
            _pass->SetCallback(std::move(callback));
        }
        RGHandle AllocTexture(const TextureDesc &desc, const String &name)
        {
            return _graph->CreateResource(desc, name);
        }
        RGHandle AllocBuffer(const BufferDesc &desc, const String &name)
        {
            return _graph->CreateResource(desc, name);
        }

    private:
        RenderGraph *_graph;
        RenderPass *_pass;
    };
}// namespace Ailu::Render::RDG

#endif// __RENDER_GRAPH__
