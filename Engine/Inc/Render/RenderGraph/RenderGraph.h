#pragma once
#ifndef __RENDER_GRAPH__
#define __RENDER_GRAPH__

#include "Render/Buffer.h"
#include "Render/ResourcePool.h"
#include "Render/Texture.h"
#include "RenderGraphFwd.h"

#include <algorithm>

namespace Ailu::Render
{
    struct RenderingData;
}

namespace std
{
    template<>
    struct hash<Ailu::Render::RDG::RGHandle>
    {
        std::size_t operator()(const Ailu::Render::RDG::RGHandle &handle) const
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
        EResourceUsage _usage = EResourceUsage::kNone;
        ELoadStoreAction _load = ELoadStoreAction::kLoad;
        ELoadStoreAction _store = ELoadStoreAction::kStore;
        ClearValue _clear_value = ClearValue::Color(0.0f, 0.0f, 0.0f, 0.0f);
        u32 _mip_level = 0u;
        u32 _mip_count = 1u;
        u32 _array_slice = 0u;
        u32 _array_slice_count = 1u;
        bool _all_sub_resources = true;

        static ResourceAccess All(EResourceUsage usage)
        {
            ResourceAccess access;
            access._usage = usage;
            return access;
        }

        static ResourceAccess MipRange(EResourceUsage usage, u32 mip_level, u32 mip_count, u32 array_slice = 0u, u32 array_slice_count = 1u)
        {
            ResourceAccess access;
            access._usage = usage;
            access._mip_level = mip_level;
            access._mip_count = mip_count;
            access._array_slice = array_slice;
            access._array_slice_count = array_slice_count;
            access._all_sub_resources = false;
            return access;
        }

        bool isWrite() const
        {
            return static_cast<uint32_t>(_usage & (EResourceUsage::kWriteUAV |
                                                   EResourceUsage::kWriteRTV | EResourceUsage::kDSV |
                                                   EResourceUsage::kCopyDst)) != 0;
        }

        bool isRead() const
        {
            return static_cast<uint32_t>(_usage & (EResourceUsage::kReadSRV |
                                                   EResourceUsage::kCopySrc | EResourceUsage::kIndirectArgument |
                                                   EResourceUsage::kRaytracingAccel)) != 0;
        }
    };

    struct ResourceAccessRecord
    {
        RGHandle _handle;
        ResourceAccess _access;
    };

    class RenderGraphBuilder;
    class RenderGraph;
    class RenderPass;

    using SetupFunction = std::function<void(RenderGraphBuilder &builder)>;
    using ExecuteFunction = std::function<void(RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)>;

    struct CompiledResourceBarrier
    {
        RGHandle _handle{};
        EResourceState _before = EResourceState::kCommon;
        EResourceState _after = EResourceState::kCommon;
        u32 _sub_resource = kTotalSubRes;
    };
    struct CompiledRenderPass
    {
        RenderPass *_pass = nullptr;
        Vector<CompiledResourceBarrier> _pre_barriers;
        Vector<CompiledResourceBarrier> _post_barriers;
        u32 _submission_index = 0u;
        bool _allow_parallel_recording = true;
    };

    struct RenderGraphCompileStats
    {
        u32 _pass_count = 0u;
        u32 _compiled_pass_count = 0u;
        u32 _resource_count = 0u;
        u32 _resource_version_count = 0u;
        u32 _dependency_edge_count = 0u;
        u32 _barrier_count = 0u;
    };

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
                pass->_input_accesses.clear();
                pass->_output_accesses.clear();
                pass->_input_access_records.clear();
                pass->_output_access_records.clear();
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
        HashMap<RGHandle, ResourceAccess> _input_accesses;
        HashMap<RGHandle, ResourceAccess> _output_accesses;
        Vector<ResourceAccessRecord> _input_access_records;
        Vector<ResourceAccessRecord> _output_access_records;
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
        RGHandle Import(GpuResource *external, EResourceState initial_state);
        GpuResource *Export(RGHandle handle);

        RGHandle GetTexture(const String &name);
        RGHandle GetBuffer(const String &name);

        // 添加一个渲染Pass
        void AddPass(const String &name, PassDesc desc, SetupFunction setup_func, ExecuteFunction executor);

        // 编译RenderGraph，解析资源依赖关系
        bool Compile();

        const RenderGraphCompileStats &GetCompileStats() const { return _compile_stats; }

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

        // Every GPU access to a RenderGraph resource must be covered by the current pass declaration.  Persistent
        // shader-global registries must not retain RenderGraph resources across passes, cameras, or graph executions.
        bool ContainsResource(const GpuResource *resource) const;

    public:
        Vector<RenderPass> _debug_passes;
        bool _is_debug = false;

    private:
        struct ResourceNode;
        RGHandle *FindHandlePtr(RGHandle handle);
        void ResetResourceNodeVersions(ResourceNode &node);
        void ReleaseTransientResource(ResourceNode &node);
        void CreatePhysicalResources(RGHandle handle);
        Texture *CreatePhysicsTexture(RGHandle handle);
        GPUBuffer *CreatePhysicsBuffer(RGHandle handle);
        bool PrepareResources();
        bool CompileResourceBarriers();
        bool ValidateResourceAccesses();
        EResourceState InitialResourceState(const ResourceNode &node) const;
        Vector<u32> ResolveBarrierSubResources(RGHandle handle, const ResourceAccess &access) const;

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
        void ExportTimelineToFile(const WString &filename);
    private:
    private:
        using PassPool = TResourcePool<RenderPass>;
        using TexturePool = THashableResourcePool<TextureDesc, Texture>;
        using BufferPool = THashableResourcePool<BufferDesc, GPUBuffer>;
        struct ResourceVersion
        {
            RGHandle _handle;
            RenderPass* _producer = nullptr;
            Vector<RenderPass*> _consumers;
            i16 _first_use = -1;
            i16 _last_use = -1;
        };
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
                _mip_count = std::max<u32>(1u, desc._mip_num);
                switch (desc._dimension)
                {
                case ETextureDimension::kCube:
                    _array_slice_count = 6u;
                    break;
                case ETextureDimension::kCubeArray:
                    _array_slice_count = std::max<u32>(1u, desc._array_size) * 6u;
                    break;
                case ETextureDimension::kTex2DArray:
                    _array_slice_count = std::max<u32>(1u, desc._array_size);
                    break;
                default:
                    _array_slice_count = 1u;
                    break;
                }
                _is_depth_resource = desc._is_depth_target;
                // Transient resources are leased from FrameResourceManager.  The pool contract is that a
                // lease starts and ends in COMMON; the compiled graph owns every transition from that point.
                _initial_state = EResourceState::kCommon;
            };
            ResourceNode(const BufferDesc &desc, StringView name) :_buffer_desc(desc), _name(name), _is_transient(true)
            {
                _is_tex = false;
                _is_render_output = false;
                _is_external = false;
                _mip_count = 1u;
                _array_slice_count = 1u;
                _is_depth_resource = false;
                // D3D12 ignores a buffer's initial state and creates it in COMMON.  Keep the graph's
                // structural contract identical to the physical resource contract.
                _initial_state = EResourceState::kCommon;
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
            EResourceState _initial_state = EResourceState::kCommon;
            u32 _mip_count = 1u;
            u32 _array_slice_count = 1u;
            bool _is_depth_resource = false;
            Vector<ResourceVersion> _versions;
        };
        inline static std::atomic<u32> s_next_handle_id = 0u;
        std::mutex _mutex;
        Vector<RenderPass *> _passes;
        Vector<RenderPass *> _sorted_passes;
        Vector<CompiledRenderPass> _compiled_passes;
        //存储所有临时资源，重新编译前清空，setup阶段就分配的句柄，execute阶段直接根据句柄创建或者获取物理资源
        HashMap<String, RGHandle> _transient_tex_handles;
        HashMap<String, RGHandle> _transient_buffer_handles;
        HashMap<u32, ResourceNode> _transient_resources;
        //外部资源句柄，通常是从外部传入的资源，RenderGraph不会管理其生命周期，新资源会覆盖旧资源
        HashMap<String, RGHandle> _external_tex_handles;
        HashMap<String, RGHandle> _external_buffer_handles;
        HashMap<u32, ResourceNode> _external_resources;
        PassPool _pass_pool{64u};
        RenderGraphCompileStats _compile_stats;
        bool _is_compiled = false;
    };

    class AILU_API RenderGraphBuilder
    {
    public:
        RenderGraphBuilder(RenderGraph &graph, RenderPass &pass) : _graph(&graph), _pass(&pass) {};
        void Read(RGHandle handle,ResourceAccess accessor)
        {
            if (auto node = _graph->GetResourceNode(handle); node != nullptr)
            {
                if (handle._version >= node->_versions.size())
                {
                    LOG_ERROR("RenderGraphBuilder::Read: invalid version!");
                    return;
                }

                auto& ver = node->_versions[handle._version];
                ver._consumers.push_back(_pass);

                _pass->Read(handle);
                _pass->_input_access_records.push_back({handle, accessor});
                if (auto it = _pass->_input_accesses.find(handle); it != _pass->_input_accesses.end())
                {
                    it->second._usage = it->second._usage | accessor._usage;
                    it->second._load = accessor._load;
                    it->second._store = accessor._store;
                    it->second._clear_value = accessor._clear_value;
                    it->second._mip_level = accessor._mip_level;
                    it->second._mip_count = accessor._mip_count;
                    it->second._array_slice = accessor._array_slice;
                    it->second._array_slice_count = accessor._array_slice_count;
                    it->second._all_sub_resources = it->second._all_sub_resources && accessor._all_sub_resources;
                }
                else
                {
                    _pass->_input_accesses.emplace(handle, accessor);
                }
                return;
            }
            LOG_ERROR("RenderGraphBuilder::Read: Attempted to read from a resource that does not exist in the graph.");
        }

        [[nodiscard]] RGHandle Write(RGHandle handle, ResourceAccess accessor)
        {
            if (auto node = _graph->GetResourceNode(handle); node != nullptr)
            {
                u32 new_version = ++node->_handle_ptr->_version;
                RGHandle new_handle = *node->_handle_ptr;
                node->_versions.emplace_back();
                auto& ver = node->_versions.back();
                ver._handle = new_handle;
                ver._producer = _pass;
                _pass->Write(new_handle);
                _pass->_output_access_records.push_back({new_handle, accessor});
                if (auto it = _pass->_output_accesses.find(new_handle); it != _pass->_output_accesses.end())
                {
                    it->second._usage = it->second._usage | accessor._usage;
                    it->second._load = accessor._load;
                    it->second._store = accessor._store;
                    it->second._clear_value = accessor._clear_value;
                    it->second._mip_level = accessor._mip_level;
                    it->second._mip_count = accessor._mip_count;
                    it->second._array_slice = accessor._array_slice;
                    it->second._array_slice_count = accessor._array_slice_count;
                    it->second._all_sub_resources = it->second._all_sub_resources && accessor._all_sub_resources;
                }
                else
                {
                    _pass->_output_accesses.emplace(new_handle, accessor);
                }
                return new_handle;
            }
            LOG_ERROR("RenderGraphBuilder::Write: Attempted to write to a resource that does not exist in the graph.");
            return RGHandle(0u);
        }

        void Read(RGHandle handle,EResourceUsage usage = EResourceUsage::kReadSRV)
        {
            ResourceAccess accessor;
            accessor._usage = usage;
            Read(handle, accessor);
        }

        void ReadRange(RGHandle handle, EResourceUsage usage, u32 mip_level, u32 mip_count, u32 array_slice = 0u, u32 array_slice_count = 1u)
        {
            Read(handle, ResourceAccess::MipRange(usage, mip_level, mip_count, array_slice, array_slice_count));
        }

        [[nodiscard]] RGHandle Write(RGHandle handle, EResourceUsage usage = EResourceUsage::kWriteRTV)
        {
            ResourceAccess accessor;
            accessor._usage = usage;
            return Write(handle, accessor);
        }

        [[nodiscard]] RGHandle WriteRange(RGHandle handle, EResourceUsage usage, u32 mip_level, u32 mip_count,
                                          u32 array_slice = 0u, u32 array_slice_count = 1u)
        {
            return Write(handle, ResourceAccess::MipRange(usage, mip_level, mip_count, array_slice, array_slice_count));
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
        RGHandle Import(GpuResource *res, EResourceState initial_state)
        {
            return _graph->Import(res, initial_state);
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
