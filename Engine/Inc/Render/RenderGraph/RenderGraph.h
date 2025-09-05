#pragma once
#ifndef __RENDER_GRAPH__
#define __RENDER_GRAPH__

#include "Render/Buffer.h"
#include "Render/ResourcePool.h"
#include "Render/Texture.h"

namespace Ailu::Render
{
    struct RenderingData;
}
namespace Ailu::Render::RDG
{
    struct RGHandle
    {
    public:
        static u64 Hash(const RGHandle &handle)
        {
            static std::hash<String> static_hasher;
            return handle._id ^ handle._version ^ static_hasher(handle._name);
        }
        bool operator==(const RGHandle &other) const
        {
            return Hash(*this) == Hash(other);
        }
        RGHandle(u32 id = s_global_id++) : _id(id), _version(0u), _res(nullptr) {};
        RGHandle(const TextureDesc &desc, StringView name) : _id(0u), _version(0u), _tex_desc(desc), _name(name), _is_transient(true), _res(nullptr)
        {
            _is_tex = true;
            _is_render_output = ((bool) desc._is_color_target) | ((bool) desc._is_depth_target);
            _is_external = false;
        };
        RGHandle(const BufferDesc &desc, StringView name) : _id(0u), _version(0u), _buffer_desc(desc), _name(name), _is_transient(true), _res(nullptr)
        {
            _is_tex = false;
            _is_render_output = false;
            _is_external = false;
        };
        String UniqueName() const
        {
            return std::format("{}({})", _name, _version);
        }
        bool IsValid() const { return _id != 0u; }
    public:
        String _name;
        u32 _id;
        u32 _version;
        union
        {
            TextureDesc _tex_desc;
            BufferDesc _buffer_desc;
        };
        GpuResource *_res;
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
        inline static std::atomic<u32> s_global_id = 0u;
    };
}// namespace Ailu::Render::RDG
namespace std
{
    template<>
    struct hash<Ailu::Render::RDG::RGHandle>
    {
        u64 operator()(const Ailu::Render::RDG::RGHandle &handle) const
        {
            return Ailu::Render::RDG::RGHandle::Hash(handle);
        }
    };
}// namespace std

namespace Ailu::Render
{
    class CommandBuffer;
}

namespace Ailu::Render::RDG
{
    enum class EPassType : u8
    {
        kGraphics,
        kCompute,
        kAsyncCompute,
        kCopy
    };
    struct AILU_API PassDesc
    {
        EPassType _type = EPassType::kGraphics;
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
    public:
        RenderGraph();
        ~RenderGraph();
        /// <summary>
        /// 只能在setup阶段调用
        /// </summary>
        /// <param name="desc"></param>
        /// <param name="name"></param>
        /// <returns></returns>
        RGHandle GetOrCreate(const TextureDesc &desc, const String &name);
        RGHandle GetOrCreate(const BufferDesc &desc, const String &name);
        Texture *CreatePhysicsTexture(const TextureDesc &desc, const String &name);
        GPUBuffer *CreatePhysicsBuffer(const BufferDesc &desc, const String &name);
        RGHandle Import(GpuResource *external, String *name = nullptr);
        GpuResource *Export(const RGHandle &handle);

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
        T *Resolve(const RGHandle &handle)
        {
            T *res{nullptr};
            if (handle._res)
            {
                res = dynamic_cast<T *>(handle._res);
                return res;
            }
            if constexpr (std::is_base_of<Texture, T>::value)
            {
                if (auto it = _transient_tex_pool_handles.find(handle._name); it != _transient_tex_pool_handles.end())
                {
                    res = dynamic_cast<T *>(it->second._res);
                }
            }
            else if constexpr (std::is_base_of<GPUBuffer, T>::value)
            {
                if (auto it = _transient_buffer_pool_handles.find(handle._name); it != _transient_buffer_pool_handles.end())
                {
                    res = dynamic_cast<T *>(it->second._res);
                }
            }
            else {}
            return res;
        }

    public:
        Vector<RenderPass> _debug_passes;
        bool _is_debug = false;

    private:
        // 拓扑排序Pass执行顺序
        void TopologicalSort();

        // 插入资源屏障
        void InsertResourceBarriers(GraphicsContext *context);

    private:
    private:
        using PassPool = TResourcePool<RenderPass>;
        using TexturePool = THashableResourcePool<TextureDesc, Texture>;
        using BufferPool = THashableResourcePool<BufferDesc, GPUBuffer>;
        Vector<RenderPass *> _passes;
        Vector<RenderPass *> _sorted_passes;
        //存储所有临时资源，重新编译前清空，setup阶段就分配的句柄，execute阶段直接根据句柄创建或者获取物理资源
        HashMap<String, RGHandle> _transient_tex_handles;
        HashMap<String, RGHandle> _transient_buffer_handles;
        //外部资源句柄，通常是从外部传入的资源，RenderGraph不会管理其生命周期，新资源会覆盖旧资源
        HashMap<String, RGHandle> _external_tex_handles;
        HashMap<String, RGHandle> _external_buffer_handles;
        //存储执行池内资源的句柄，每帧结束释放
        HashMap<String, TexturePool::PoolResourceHandle> _transient_tex_pool_handles;
        HashMap<String, BufferPool::PoolResourceHandle> _transient_buffer_pool_handles;
        PassPool _pass_pool{64u};
        bool _is_compiled = false;
    };

    class AILU_API RenderGraphBuilder
    {
    public:
        RenderGraphBuilder(RenderGraph &graph, RenderPass &pass) : _graph(&graph), _pass(&pass) {};
        void Read(const RGHandle &handle)
        {
            _pass->Read(handle);
        }
        RGHandle GetTexture(const String &name)
        {
            return _graph->GetTexture(name);
        }
        RGHandle GetBuffer(const String &name)
        {
            return _graph->GetBuffer(name);
        }
        RGHandle Read(TextureDesc desc, const String &name)
        {
            auto handle = _graph->GetOrCreate(desc, name);
            _pass->Read(handle);
            return handle;
        }
        RGHandle Import(GpuResource *res, String *name = nullptr)
        {
            return _graph->Import(res, name);
        }
        RGHandle Write(TextureDesc desc, const String &name)
        {
            auto handle = _graph->GetOrCreate(desc, name);
            _pass->Write(handle);
            return handle;
        }
        void Write(RGHandle &handle)
        {
            handle._version += handle._is_transient;
            _pass->Write(handle);
        }
        void ReadWrite(RGHandle &handle)
        {
            _pass->Read(handle);
            handle._version += handle._is_transient;
            _pass->Write(handle);
        }
        void SetCallback(ExecuteFunction callback)
        {
            _pass->SetCallback(std::move(callback));
        }
        RGHandle AllocTexture(const TextureDesc &desc, const String &name)
        {
            return _graph->GetOrCreate(desc, name);
        }
        RGHandle AllocBuffer(const BufferDesc &desc, const String &name)
        {
            return _graph->GetOrCreate(desc, name);
        }

    private:
        RenderGraph *_graph;
        RenderPass *_pass;
    };
}// namespace Ailu::Render::RDG

#endif// __RENDER_GRAPH__
