#include "Render/RenderGraph/RenderGraph.h"
#include "Render/GraphicsContext.h"
#include "Render/CommandBuffer.h"
#include "Render/FrameResource.h"
#include "Framework/Common/Profiler.h"

#include "Framework/Common/FileManager.h"



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
            for (auto& it: _transient_resources)
            {
                auto& [id, node] = it;
                if (node._is_tex)
                    res_mgr.FreeTexture(node._pool_tex_handle);
                else
                    res_mgr.FreeBuffer(node._pool_buffer_handle);
            }
            _transient_resources.clear();
        };

        RGHandle RenderGraph::CreateResource(const TextureDesc &desc, const String &name)
        {
            std::lock_guard lock(_mutex);
            const static auto s_hasher = Math::ALHash::Hasher<TextureDesc>();
            ResourceNode node(desc,name);
            if (auto it = _transient_tex_handles.find(name); it != _transient_tex_handles.end())
            {
                if (s_hasher(desc) != s_hasher(_transient_resources[it->second._id]._tex_desc))
                {
                    LOG_WARNING("RenderGraph::CreateResource: same name {} but different texture desc,replace the old one", name);
                    _transient_resources[it->second._id] = node;
                }
                return it->second;
            }
            auto handle = RGHandle(s_next_handle_id++);
            _transient_tex_handles[name] = handle;
            node._handle_ptr = &_transient_tex_handles[name];
            _transient_resources[handle._id] = node;
            return handle;
        }

        RGHandle RenderGraph::CreateResource(const BufferDesc &desc, const String &name)
        {
            std::lock_guard lock(_mutex);
            const static auto s_hasher = Math::ALHash::Hasher<BufferDesc>();
            ResourceNode node(desc, name);
            if (auto it = _transient_buffer_handles.find(name); it != _transient_buffer_handles.end())
            {
                if (s_hasher(desc) != s_hasher(_transient_resources[it->second._id]._buffer_desc))
                {
                    LOG_WARNING("RenderGraph::CreateResource: same name {} but different texture desc,replace the old one", name);
                    _transient_resources[it->second._id] = node;
                }
                return it->second;
            }
            auto handle = RGHandle(s_next_handle_id++);
            _transient_buffer_handles[name] = handle;
            node._handle_ptr = &_transient_buffer_handles[name];
            _transient_resources[handle._id] = node;
            return handle;
        }


        RGHandle *RenderGraph::FindHandlePtr(RGHandle handle)
        {
            if (auto it = _transient_resources.find(handle._id); it != _transient_resources.end())
            {
                return it->second._handle_ptr;
            }
            if (auto it = _external_resources.find(handle._id); it != _external_resources.end())
            {
                return it->second._handle_ptr;
            }
            return nullptr;
        }

        void RenderGraph::CreatePhysicalResources(RGHandle handle)
        {
            if (auto node = GetResourceNode(handle); node != nullptr)
            {
                if (node->_is_transient)
                {
                    if (node->_is_tex)
                        CreatePhysicsTexture(handle);
                    else
                        CreatePhysicsBuffer(handle);
                }
            }
            else
                AL_ASSERT(false);
        }

        Texture *RenderGraph::CreatePhysicsTexture(RGHandle handle)
        {
            if (auto it = _transient_resources.find(handle._id); it != _transient_resources.end())
            {
                auto& node = it->second;
                if (!node._is_allocated)
                {
                    node._pool_tex_handle = FrameResourceManager::Get().AllocTexture(it -> second._tex_desc);
                    node._pool_tex_handle._res->Name(node._name);
                    node._is_allocated = true;
                }
                return node._pool_tex_handle._res;
            }
            AL_ASSERT(false);
            return nullptr;
        }

        GPUBuffer *RenderGraph::CreatePhysicsBuffer(RGHandle handle)
        {
            if (auto it = _transient_resources.find(handle._id); it != _transient_resources.end())
            {
                auto &node = it->second;
                if (!node._is_allocated)
                {
                    node._pool_buffer_handle = FrameResourceManager::Get().AllocBuffer(it->second._buffer_desc);
                    node._pool_tex_handle._res->Name(node._name);
                    node._is_allocated = true;
                }
                return node._pool_buffer_handle._res;
            }
            AL_ASSERT(false);
            return nullptr;
        }


        RGHandle RenderGraph::Import(GpuResource *external)
        {
            auto res_type = external->GetResourceType();
            if (res_type == EGpuResType::kVertexBuffer || res_type == EGpuResType::kIndexBUffer || res_type == EGpuResType::kConstBuffer || res_type == EGpuResType::kGraphicsPSO)
            {
                AL_ASSERT_MSG(false, "RenderGraph::Import: Unsupported resource type!");
            }
            bool is_tex = res_type == EGpuResType::kTexture || res_type == EGpuResType::kRenderTexture;
            auto& external_pool = is_tex ? _external_tex_handles : _external_buffer_handles;
            std::lock_guard lock(_mutex);
            if (auto it = external_pool.find(external->Name()); it != external_pool.end())
            {
                auto &existing = _external_resources[it->second._id];
                if (existing._extern_raw_res != external)
                    LOG_WARNING("RenderGraph::Import: External resource name conflict, re-binding [{}]", external->Name());
                existing._extern_raw_res = external;
                return it->second;
            }

            RGHandle handle(s_next_handle_id++);
            ResourceNode node(external->Name());
            node._extern_raw_res = external;
            node._is_tex = res_type == EGpuResType::kTexture || res_type == EGpuResType::kRenderTexture;
            node._is_render_output = res_type == EGpuResType::kRenderTexture;
            node._is_external = true;
            node._is_transient = false;
            external_pool[node._name] = handle;
            node._handle_ptr = &external_pool[node._name];
            _external_resources[handle._id] = node;
            return handle;
        }

        GpuResource* RenderGraph::Export(RGHandle handle)
        {
            GpuResource *out_res{nullptr};
            if (auto it = _external_resources.find(handle._id); it != _external_resources.end())
                return it->second._extern_raw_res;
            if (auto it = _transient_resources.find(handle._id); it != _transient_resources.end())
            {
                out_res = it->second._is_tex ? (GpuResource*)CreatePhysicsTexture(handle) : CreatePhysicsBuffer(handle);
                it->second._is_transient = false;
                it->second._is_external = true;
                auto &&old_handle = std::move(_transient_resources[handle._id]);
                if (it->second._is_tex)
                {
                    _external_tex_handles[it->second._name] = handle;
                    old_handle._handle_ptr = &_external_tex_handles[it->second._name];
                }
                else
                {
                    _external_buffer_handles[it->second._name] = handle;
                    old_handle._handle_ptr = &_external_buffer_handles[it->second._name];
                }
                _external_resources[handle._id] = old_handle;
                _transient_resources.erase(it);
                return out_res;
            }
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
                    auto node = GetResourceNode(output);
                    if (node->_first_write == -1)
                        node->_first_write = i;
                }
                for (RGHandle& input: pass->_input_handles)
                {
                    auto node = GetResourceNode(input);
                    node->_last_read = std::max<i16>(node->_last_read, (i16) i);
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
                ExportToFile(L"render_graph.dot");
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
                        CreatePhysicalResources(handle);
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
        void RenderGraph::ExportToFile(const WString &filename)
        {
            std::string buffer;

            buffer.append("digraph RenderGraph {\n");
            buffer.append("    rankdir=LR;\n");// 从左到右布局，可选
            buffer.append("    node [shape=box, style=filled, fillcolor=lightgray];\n");
            u32 index = 0u;
            for (auto &p: _debug_passes)
            {
                String pass_name = std::format("{}({})", p._name, index);
                if (p._type == EPassType::kGraphics)
                    buffer.append(std::format("    \"{}\" [style=\"rounded,filled\", fillcolor=lightgreen, color=lightgreen, fontcolor=black];\n", pass_name));
                else if (p._type == EPassType::kCompute)
                    buffer.append(std::format("    \"{}\" [style=\"rounded,filled\", fillcolor=lightblue, color=lightblue, fontcolor=black];\n", pass_name));
                else if (p._type == EPassType::kAsyncCompute)
                    buffer.append(std::format("    \"{}\" [style=\"rounded,filled\", fillcolor=orange, color=orange, fontcolor=black];\n", pass_name));
                else
                    buffer.append(std::format("    \"{}\" [style=\"rounded,filled\", fillcolor=yellow, color=yellow, fontcolor=black];\n", pass_name));
                for (auto input: p._input_handles)
                {
                    auto *res = GetResourceNode(input);
                    if (res)
                    {
                        String res_name = std::format("{}_v{}", res->_name, input._version);
                        buffer.append(std::format("    \"{}\" -> \"{}\";\n", res_name, pass_name));
                        buffer.append(std::format("    \"{}\" [shape=box, style=filled, fillcolor=lightgray, color=gray, fontcolor=black];\n", res_name));// 资源节点（灰色）
                    }
                }
                for (auto output: p._output_handles)
                {
                    auto *res = GetResourceNode(output);
                    if (res)
                    {
                        String res_name = std::format("{}_v{}", res->_name, output._version);
                        buffer.append(std::format("    \"{}\" -> \"{}\";\n", pass_name, res_name));
                        buffer.append(std::format("    \"{}\" [shape=box, style=filled, fillcolor=lightgray, color=gray, fontcolor=black];\n", res_name));
                    }
                }
                index++;
            }

            buffer.append("}\n");

            FileManager::WriteFile(filename, false, buffer);
            LOG_INFO(L"CRenderGraph::ExportToFile: Output to {},go https://dreampuf.github.io/GraphvizOnline/ to have a view", filename);
        }

    }// namespace ::Render::RDG
}
