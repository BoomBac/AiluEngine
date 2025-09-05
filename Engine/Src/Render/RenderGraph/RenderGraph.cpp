#include "Render/RenderGraph/RenderGraph.h"
#include "Render/GraphicsContext.h"
#include "Render/CommandBuffer.h"
#include "Render/FrameResource.h"
#include "Framework/Common/Profiler.h"



namespace Ailu
{
    namespace Render::RDG
    {
        RenderGraph::RenderGraph()
        {
        }

        RenderGraph::~RenderGraph()
        {
        }

        void RenderGraph::EndFrame()
        {
            _transient_tex_handles.clear();
            _transient_buffer_handles.clear();
            _passes.clear();
            auto &res_mgr = FrameResourceManager::Get();
            for (auto p: _sorted_passes)
                _pass_pool.Release(p);
            for (const auto &handle: _transient_tex_pool_handles)
                res_mgr.FreeTexture(handle.second);
            for (const auto &handle: _transient_buffer_pool_handles)
                res_mgr.FreeBuffer(handle.second);
            _transient_buffer_pool_handles.clear();
            _transient_tex_pool_handles.clear();
        };

        RGHandle RenderGraph::GetOrCreate(const TextureDesc &desc, const String &name)
        {
            const static auto s_hasher = Math::ALHash::Hasher<TextureDesc>();
            auto it = _transient_tex_handles.find(name);
            if (it != _transient_tex_handles.end())
            {
                AL_ASSERT_MSG(s_hasher(desc) == s_hasher(it->second._tex_desc), "TextureDesc mismatch!");
                return it->second;
            }
            auto handle = RGHandle(desc, name);
            _transient_tex_handles[name] = handle;
            return RGHandle(desc,name);
        }

        RGHandle RenderGraph::GetOrCreate(const BufferDesc &desc, const String &name)
        {
            const static auto s_hasher = Math::ALHash::Hasher<BufferDesc>();
            auto it = _transient_buffer_handles.find(name);
            if (it != _transient_buffer_handles.end())
            {
                AL_ASSERT_MSG(s_hasher(desc) == s_hasher(it->second._buffer_desc), "BufferDesc mismatch!");
                return it->second;
            }
            auto handle = RGHandle(desc, name);
            _transient_buffer_handles[name] = handle;
            return handle;
        }


        Texture *RenderGraph::CreatePhysicsTexture(const TextureDesc &desc, const String &name)
        {
            if (_transient_tex_pool_handles.contains(name))
                return _transient_tex_pool_handles.at(name)._res;
            auto it = FrameResourceManager::Get().AllocTexture(desc);
            _transient_tex_pool_handles.emplace(name, it);
            _transient_tex_handles[name]._res = it._res;
            return it._res;
        }

        GPUBuffer *RenderGraph::CreatePhysicsBuffer(const BufferDesc &desc, const String &name)
        {
            if (_transient_buffer_pool_handles.contains(name))
                return _transient_buffer_pool_handles.at(name)._res;
            auto it = FrameResourceManager::Get().AllocBuffer(desc);
            _transient_buffer_pool_handles.emplace(name, it);
            _transient_buffer_handles[name]._res = it._res;
            return it._res;
        }


        RGHandle RenderGraph::Import(GpuResource *external, String *name)
        {
            auto res_type = external->GetResourceType();
            if (res_type == EGpuResType::kVertexBuffer || res_type == EGpuResType::kIndexBUffer || res_type == EGpuResType::kConstBuffer || res_type == EGpuResType::kGraphicsPSO)
            {
                AL_ASSERT_MSG(false, "RenderGraph::Import: Unsupported resource type!");
            }
            bool is_tex = res_type == EGpuResType::kTexture || res_type == EGpuResType::kRenderTexture;
            auto& external_pool = is_tex ? _external_tex_handles : _external_buffer_handles;
            const String &external_name = name ? *name : external->Name();
            if (external_pool.contains(external_name))
            {
                //LOG_WARNING("RenderGraph::Import: External resource {} already exists!", external_name);
                return external_pool.at(external_name);
            }
            RGHandle handle;
            handle._res = external;
            handle._is_tex = res_type == EGpuResType::kTexture || res_type == EGpuResType::kRenderTexture;
            handle._is_render_output = res_type == EGpuResType::kRenderTexture;
            handle._is_external = true;
            handle._is_transient = false;
            handle._name = external_name;
            external_pool[external_name] = handle;
            return handle;
        }

        GpuResource* RenderGraph::Export(const RGHandle &handle)
        {
            GpuResource *out_res{nullptr};
            if (handle._is_external)
            {
                return handle._res;
            }
            if (auto it = _transient_tex_handles.find(handle._name); it != _transient_tex_handles.end())
            {
                out_res = it->second._res;
                it->second._is_transient = false;
                it->second._is_external = true;
                _external_tex_handles[handle._name] = it->second;
                _transient_tex_handles.erase(it);
                return out_res;
            }
            if (auto it = _transient_buffer_handles.find(handle._name); it != _transient_buffer_handles.end())
            {
                out_res = it->second._res;
                it->second._is_transient = false;
                it->second._is_external = true;
                _external_buffer_handles[handle._name] = it->second;
                _transient_buffer_handles.erase(it);
                return out_res;
            }
            LOG_WARNING("RenderGraph::Export: Handle {} is not transient!", handle._name);
            return out_res;
        }

        RGHandle RenderGraph::GetTexture(const String &name)
        {
            if (_external_tex_handles.find(name) != _external_tex_handles.end())
            {
                return _external_tex_handles[name];
            }
            if (_transient_tex_handles.find(name) != _transient_tex_handles.end())
            {
                return _transient_tex_handles[name];
            }
            return RGHandle(0u);
        }

        RGHandle RenderGraph::GetBuffer(const String &name)
        {
            if (_external_buffer_handles.find(name) != _external_buffer_handles.end())
            {
                return _external_buffer_handles[name];
            }
            if (_transient_buffer_handles.find(name) != _transient_buffer_handles.end())
            {
                return _transient_buffer_handles[name];
            }
            return RGHandle(0u);
        }

        void RenderGraph::AddPass(const String &name, PassDesc desc, SetupFunction setup_func, ExecuteFunction executor)
        {
            RenderPass *pass = _pass_pool.Alloc();
            pass->_name = name;
            pass->_type = desc._type;
            RenderGraphBuilder builder(*this, *pass);
            setup_func(builder);
            builder.SetCallback(executor);
            _passes.push_back(pass);
        }

        bool RenderGraph::Compile()
        {
            //if (_is_compiled)
            //    return true;
            _sorted_passes.clear();

            // 1. 收集所有资源的生产者（支持多生产者）
            HashMap<RGHandle, Vector<RenderPass *>> _res_producers;
            for (RenderPass *pass: _passes)
            {
                for (auto &output: pass->_output_handles)
                {
                    _res_producers[output].push_back(pass);
                }
            }

            // 2. 构建 Pass 之间的依赖图
            HashMap<RenderPass *, Vector<RenderPass *>> pass_dependencies;// producer -> consumers
            HashMap<RenderPass *, u32> in_degrees;                        // consumer -> count

            for (RenderPass *consumer: _passes)
            {
                in_degrees[consumer] = 0u;
                for (const RGHandle &input: consumer->_input_handles)
                {
                    auto it = _res_producers.find(input);
                    if (it != _res_producers.end())
                    {
                        for (RenderPass *producer: it->second)
                        {
                            if (producer != consumer)
                            {
                                pass_dependencies[producer].push_back(consumer);
                                in_degrees[consumer]++;
                            }
                            else
                            {
                                LOG_ERROR("RenderGraph::Compile: Pass {} is reading from itself!", consumer->_name);
                            }
                        }
                    }
                }
            }

            // 3. 拓扑排序（Kahn's algorithm）
            Queue<RenderPass *> q;
            for (auto &[pass, degree]: in_degrees)
            {
                if (degree == 0)
                    q.push(pass);
            }

            while (!q.empty())
            {
                RenderPass *current = q.front();
                q.pop();
                _sorted_passes.push_back(current);
                auto &dpes = pass_dependencies[current];
                for (RenderPass *dependent: dpes)
                {
                    u32 in_degree = in_degrees[dependent];
                    if (--in_degrees[dependent] == 0)
                    {
                        q.push(dependent);
                    }
                }
            }

            // 4. 检查是否存在循环依赖
            if (_sorted_passes.size() != _passes.size())
            {
                LOG_ERROR("RenderGraph::Compile: Dependency graph contains a cycle! Sort failed.");
                // 你也可以在这里抛异常或触发调试断点
            }

            for (i32 i = 0; i < (i32) _sorted_passes.size(); ++i)
            {
                RenderPass *pass = _sorted_passes[i];
                for (RGHandle& output: pass->_output_handles)
                {
                    if (output._first_write == -1)
                        output._first_write = i;
                }
                for (RGHandle& input: pass->_input_handles)
                {
                    input._last_read = std::max<i16>(input._last_read, (i16) i);
                }
            }
            if (_is_debug)
            {
                _debug_passes.clear();
                LOG_INFO("sorted_passes: ")
                for (const RenderPass *pass: _sorted_passes)
                {
                    LOG_INFO("pass: {}", pass->_name);
                    _debug_passes.push_back(*pass);
                }
                _is_debug = false;
            }
            _is_compiled = true;
            return true;
        }

        void RenderGraph::Execute(GraphicsContext& context, const RenderingData &data)
        {
            if (!_is_compiled && !Compile())
                return;
            for (auto *pass: _sorted_passes)
            {
                {
                    CPUProfileBlock cb(pass->_name);
                    for (auto &handle: pass->_output_handles)
                    {
                        if (handle._is_transient && handle._res == nullptr)
                        {
                            if (handle._is_tex)
                                handle._res = CreatePhysicsTexture(handle._tex_desc, handle._name);
                            else
                                handle._res = CreatePhysicsBuffer(handle._buffer_desc, handle._name);
                        }
                    }
                    auto cmd = CommandBufferPool::Get(pass->_name);
                    cmd->SetRenderGraph(this);
                    {
                        GpuProfileBlock pass_block(cmd.get(), pass->_name);
                        pass->Execute(*this, cmd.get(), data);
                    }
                    context.ExecuteCommandBuffer(cmd);
                    CommandBufferPool::Release(cmd);
                }
            }
            // TODO: 插入资源屏障
            // TODO: 执行各Pass
        }

        void RenderGraph::TopologicalSort()
        {
            // TODO: 实现拓扑排序
        }

        void RenderGraph::InsertResourceBarriers(GraphicsContext *context)
        {
            // TODO: 实现资源屏障插入
        }
    }// namespace ::Render::RDG
}
