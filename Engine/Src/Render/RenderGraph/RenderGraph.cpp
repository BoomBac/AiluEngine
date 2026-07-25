#include "Render/RenderGraph/RenderGraph.h"
#include "Framework/Common/Hash.hpp"
#include "Render/GraphicsContext.h"
#include "Render/CommandBuffer.h"
#include "Render/FrameResource.h"
#include "Framework/Common/Profiler.h"

#include "Framework/Common/FileManager.h"

#include <algorithm>
#include <unordered_set>



namespace Ailu
{
    namespace Render::RDG
    {
        namespace
        {
            bool HasUsage(EResourceUsage usage, EResourceUsage flag)
            {
                return static_cast<u32>(usage & flag) != 0u;
            }

            EResourceState UsageToResourceState(EResourceUsage usage)
            {
                if (HasUsage(usage, EResourceUsage::kWriteUAV))
                    return EResourceState::kUnorderedAccess;
                if (HasUsage(usage, EResourceUsage::kWriteRTV))
                    return EResourceState::kRenderTarget;
                if (HasUsage(usage, EResourceUsage::kDSV))
                    return EResourceState::kDepthWrite;
                if (HasUsage(usage, EResourceUsage::kCopyDst))
                    return EResourceState::kCopyDest;
                if (HasUsage(usage, EResourceUsage::kCopySrc))
                    return EResourceState::kCopySource;
                if (HasUsage(usage, EResourceUsage::kIndirectArgument))
                    return EResourceState::kIndirectArgument;
                if (HasUsage(usage, EResourceUsage::kRaytracingAccel))
                    return EResourceState::kRaytracingAccelerationStructure;
                if (HasUsage(usage, EResourceUsage::kReadSRV))
                    return EResourceState::kAllShaderResource;
                return EResourceState::kCommon;
            }
        }

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
                ReleaseTransientResource(node);
            }
            _transient_resources.clear();
            for (auto &[id, node]: _external_resources)
                ResetResourceNodeVersions(node);
            _sorted_passes.clear();
            _is_compiled = false;
        };

        void RenderGraph::ResetResourceNodeVersions(ResourceNode &node)
        {
            if (node._handle_ptr == nullptr)
                return;

            node._handle_ptr->_version = 0u;
            node._versions.clear();
            node._versions.emplace_back();
            node._versions[0]._handle = *node._handle_ptr;
            node._versions[0]._producer = nullptr;
            node._first_write = -1;
            node._last_read = -1;
        }

        void RenderGraph::ReleaseTransientResource(ResourceNode &node)
        {
            if (!node._is_allocated)
                return;

            auto &res_mgr = FrameResourceManager::Get();
            if (node._is_tex)
                res_mgr.FreeTexture(node._pool_tex_handle);
            else
                res_mgr.FreeBuffer(node._pool_buffer_handle);
            node._is_allocated = false;
        }

        RGHandle RenderGraph::CreateResource(const TextureDesc &desc, const String &name)
        {
            std::lock_guard lock(_mutex);
            const static auto s_hasher = Math::ALHash::Hasher<TextureDesc>();
            ResourceNode node(desc,name);
            node._extern_raw_res = nullptr;
            node._pool_buffer_handle._key_hash = 0u;
            node._pool_buffer_handle._res = nullptr;
            if (auto it = _transient_tex_handles.find(name); it != _transient_tex_handles.end())
            {
                auto &existing = _transient_resources[it->second._id];
                if (s_hasher(desc) != s_hasher(existing._tex_desc))
                {
                    LOG_WARNING("RenderGraph::CreateResource: same name {} but different texture desc,replace the old one", name);
                    ReleaseTransientResource(existing);
                    auto *handle_ptr = existing._handle_ptr;
                    existing = node;
                    existing._handle_ptr = handle_ptr;
                    ResetResourceNodeVersions(existing);
                }
                return it->second;
            }
            auto handle = RGHandle(s_next_handle_id++);
            _transient_tex_handles[name] = handle;
            node._handle_ptr = &_transient_tex_handles[name];
            ResetResourceNodeVersions(node);
            _transient_resources[handle._id] = node;
            return handle;
        }

        RGHandle RenderGraph::CreateResource(const BufferDesc &desc, const String &name)
        {
            std::lock_guard lock(_mutex);
            const static auto s_hasher = Math::ALHash::Hasher<BufferDesc>();
            ResourceNode node(desc, name);
            node._extern_raw_res = nullptr;
            node._pool_buffer_handle._key_hash = 0u;
            node._pool_buffer_handle._res = nullptr;
            if (auto it = _transient_buffer_handles.find(name); it != _transient_buffer_handles.end())
            {
                auto &existing = _transient_resources[it->second._id];
                if (s_hasher(desc) != s_hasher(existing._buffer_desc))
                {
                    LOG_WARNING("RenderGraph::CreateResource: same name {} but different texture desc,replace the old one", name);
                    ReleaseTransientResource(existing);
                    auto *handle_ptr = existing._handle_ptr;
                    existing = node;
                    existing._handle_ptr = handle_ptr;
                    ResetResourceNodeVersions(existing);
                }
                return it->second;
            }
            auto handle = RGHandle(s_next_handle_id++);
            _transient_buffer_handles[name] = handle;
            node._handle_ptr = &_transient_buffer_handles[name];
            ResetResourceNodeVersions(node);
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
                    node._pool_buffer_handle._res->Name(node._name);
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
            if (res_type == EGpuResType::kVertexBuffer || res_type == EGpuResType::kIndexBuffer || res_type == EGpuResType::kConstBuffer || res_type == EGpuResType::kGraphicsPSO)
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
            ResetResourceNodeVersions(node);
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
            _is_compiled = false;
        }

        bool RenderGraph::Compile()
        {
            PROFILE_BLOCK_CPU("RenderGraph::Compile")

            _sorted_passes.clear();

            HashMap<RenderPass*, Vector<RenderPass*>> pass_dependencies;
            HashMap<RenderPass*, std::unordered_set<RenderPass*>> unique_dependencies;
            HashMap<RenderPass*, u32> in_degrees;

            // 初始化
            for (auto* pass : _passes)
                in_degrees[pass] = 0;

            const auto build_dependencies = [&](auto &resources)
            {
                for (auto& [id, node] : resources)
                {
                    for (auto& ver : node._versions)
                    {
                        if (!ver._producer)
                            continue;

                        for (auto* consumer : ver._consumers)
                        {
                            if (consumer == ver._producer)
                                continue;

                            auto &deps = unique_dependencies[ver._producer];
                            if (deps.insert(consumer).second)
                            {
                                pass_dependencies[ver._producer].push_back(consumer);
                                in_degrees[consumer]++;
                            }
                        }
                    }
                }
            };

            build_dependencies(_transient_resources);
            build_dependencies(_external_resources);

            // 拓扑排序
            Queue<RenderPass*> q;

            for (auto& [pass, deg] : in_degrees)
                if (deg == 0) q.push(pass);

            while (!q.empty())
            {
                auto* p = q.front(); q.pop();
                _sorted_passes.push_back(p);

                for (auto* dep : pass_dependencies[p])
                {
                    if (--in_degrees[dep] == 0)
                        q.push(dep);
                }
            }

            if (_sorted_passes.size() != _passes.size())
            {
                LOG_ERROR("RenderGraph::Compile: cycle detected!");
                return false;
            }

            //记录 version 生命周期（为 aliasing 准备）
            for (i32 i = 0; i < (i32)_sorted_passes.size(); ++i)
            {
                auto* pass = _sorted_passes[i];

                for (auto& h : pass->_output_handles)
                {
                    auto* node = GetResourceNode(h);
                    auto& ver = node->_versions[h._version];
                    if (ver._first_use == -1)
                        ver._first_use = i;
                }

                for (auto& h : pass->_input_handles)
                {
                    auto* node = GetResourceNode(h);
                    auto& ver = node->_versions[h._version];
                    ver._last_use = std::max<i16>(ver._last_use, i);
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
                ExportTimelineToFile(L"render_graph_timeline.dot");
            }

            _is_compiled = true;
            return true;
        }

        void RenderGraph::Execute(GraphicsContext& context, const RenderingData &data)
        {
            if (!_is_compiled && !Compile())
                return;
            PROFILE_BLOCK_CPU("RenderGraph::Execute")
            for (auto *pass: _sorted_passes)
            {
                {
                    PROFILE_BLOCK_CPU(pass->_name)
                    for (auto &handle: pass->_output_handles)
                    {
                        CreatePhysicalResources(handle);
                    }
                    auto cmd = CommandBufferPool::Get(pass->_name);
                    cmd->SetRenderGraph(this);
                    for (const auto &[handle, access]: pass->_input_accesses)
                    {
                        if (auto *res = Resolve<GpuResource>(handle); res != nullptr)
                        {
                            //LOG_INFO("Pass {} accessing resource {} with usage {}", pass->_name, res->Name(), (u32)access._usage);
                            cmd->StateTransition(res, UsageToResourceState(access._usage));
                        }
                        else
                        {
                            LOG_ERROR("RenderGraph::Execute: Failed to resolve resource for handle {}.{} in pass {}", handle._id, handle._version, pass->_name);
                        }
                    }
                    for (const auto &[handle, access]: pass->_output_accesses)
                    {
                        if (auto *res = Resolve<GpuResource>(handle); res != nullptr)
                        {
                            //LOG_INFO("Pass {} accessing resource {} with usage {}", pass->_name, res->Name(), (u32)access._usage);
                            cmd->StateTransition(res, UsageToResourceState(access._usage));
                        }
                    }
                    {
                        PROFILE_BLOCK_GPU(cmd.get(), pass->_name)
                        pass->Execute(*this, cmd.get(), data);
                    }
                    for (const auto &[handle, access]: pass->_output_accesses)
                    {
                        auto *res = Resolve<GpuResource>(handle);
                        if (HasUsage(access._usage, EResourceUsage::kWriteUAV) && res != nullptr)
                            cmd->InsertUAVBarrier(res);
                    }
                    context.ExecuteCommandBuffer(cmd);
                    CommandBufferPool::Release(cmd);
                }
            }
        }
        void RenderGraph::ExportToFile(const WString &filename)
        {
            std::string buffer;

            const auto escape_dot = [](StringView text)
            {
                String escaped;
                escaped.reserve(text.size() + 16u);
                for (char c: text)
                {
                    switch (c)
                    {
                    case '\\': escaped += "\\\\"; break;
                    case '"': escaped += "\\\""; break;
                    case '\n': escaped += "\\n"; break;
                    case '\r': break;
                    default: escaped.push_back(c); break;
                    }
                }
                return escaped;
            };
            const auto escape_html = [](StringView text)
            {
                String escaped;
                escaped.reserve(text.size() + 16u);
                for (char c: text)
                {
                    switch (c)
                    {
                    case '&': escaped += "&amp;"; break;
                    case '<': escaped += "&lt;"; break;
                    case '>': escaped += "&gt;"; break;
                    case '"': escaped += "&quot;"; break;
                    case '\n': escaped += "<BR ALIGN=\"LEFT\"/>"; break;
                    case '\r': break;
                    default: escaped.push_back(c); break;
                    }
                }
                return escaped;
            };
            const auto join_strings = [](const Vector<String> &items, StringView separator)
            {
                if (items.empty())
                    return String("none");
                String result;
                for (u32 i = 0; i < items.size(); ++i)
                {
                    if (i != 0u)
                        result += separator;
                    result += items[i];
                }
                return result;
            };
            const auto pass_type_name = [](EPassType type)
            {
                switch (type)
                {
                case EPassType::kGraphics: return String("Graphics");
                case EPassType::kCompute: return String("Compute");
                case EPassType::kAsyncCompute: return String("AsyncCompute");
                case EPassType::kCopy: return String("Copy");
                case EPassType::kRayTracing: return String("RayTracing");
                default: return String("Unknown");
                }
            };
            const auto pass_type_color = [](EPassType type)
            {
                switch (type)
                {
                case EPassType::kGraphics: return String("#D8F5D0");
                case EPassType::kCompute: return String("#D8E9FF");
                case EPassType::kAsyncCompute: return String("#FFE4BF");
                case EPassType::kCopy: return String("#FFF3A6");
                case EPassType::kRayTracing: return String("#E5D0F5");
                default: return String("#E5E5E5");
                }
            };
            const auto load_store_name = [](ELoadStoreAction action)
            {
                switch (action)
                {
                case ELoadStoreAction::kLoad: return String("Load");
                case ELoadStoreAction::kStore: return String("Store");
                case ELoadStoreAction::kClear: return String("Clear");
                case ELoadStoreAction::kNotCare: return String("DontCare");
                default: return String("Unknown");
                }
            };
            const auto usage_name = [join_strings](EResourceUsage usage)
            {
                Vector<String> tokens;
                if (HasUsage(usage, EResourceUsage::kReadSRV))
                    tokens.emplace_back("ReadSRV");
                if (HasUsage(usage, EResourceUsage::kWriteUAV))
                    tokens.emplace_back("WriteUAV");
                if (HasUsage(usage, EResourceUsage::kWriteRTV))
                    tokens.emplace_back("WriteRTV");
                if (HasUsage(usage, EResourceUsage::kDSV))
                    tokens.emplace_back("Depth");
                if (HasUsage(usage, EResourceUsage::kCopySrc))
                    tokens.emplace_back("CopySrc");
                if (HasUsage(usage, EResourceUsage::kCopyDst))
                    tokens.emplace_back("CopyDst");
                if (HasUsage(usage, EResourceUsage::kIndirectArgument))
                    tokens.emplace_back("IndirectArg");
                if (HasUsage(usage, EResourceUsage::kRaytracingAccel))
                    tokens.emplace_back("RTAS");
                return join_strings(tokens, "|");
            };
            const auto build_access_label = [&](const ResourceAccess &access, bool is_write)
            {
                Vector<String> parts;
                parts.emplace_back(usage_name(access._usage));
                if (is_write || access._load != ELoadStoreAction::kLoad || access._store != ELoadStoreAction::kStore)
                    parts.emplace_back(std::format("{}->{}", load_store_name(access._load), load_store_name(access._store)));
                if (access._mip_level != 0u)
                    parts.emplace_back(std::format("mip {}", access._mip_level));
                if (access._array_slice != 0u)
                    parts.emplace_back(std::format("slice {}", access._array_slice));
                return join_strings(parts, ", ");
            };
            const auto build_resource_desc = [&](const ResourceNode &node)
            {
                Vector<String> parts;
                parts.emplace_back(node._is_external ? "External" : "Transient");
                parts.emplace_back(node._is_tex ? "Texture" : "Buffer");
                if (node._is_tex)
                {
                    if (node._tex_desc._fixed_size)
                        parts.emplace_back(std::format("{}x{}x{}", node._tex_desc._width, node._tex_desc._height, node._tex_desc._depth));
                    else
                        parts.emplace_back(std::format("scale {:.2f}x{:.2f}", node._tex_desc._scale_w, node._tex_desc._scale_h));
                    parts.emplace_back(std::format("mip {}", node._tex_desc._mip_num));
                    if (node._tex_desc._array_size > 0u)
                        parts.emplace_back(std::format("array {}", node._tex_desc._array_size));
                    if (node._tex_desc._is_color_target)
                        parts.emplace_back("RT");
                    if (node._tex_desc._is_depth_target)
                        parts.emplace_back("Depth");
                    if (node._tex_desc._is_random_access)
                        parts.emplace_back("UAV");
                }
                else
                {
                    parts.emplace_back(std::format("size {}", node._buffer_desc._size));
                    parts.emplace_back(std::format("elem {}x{}", node._buffer_desc._element_num, node._buffer_desc._element_size));
                    if (node._buffer_desc._is_create_srv)
                        parts.emplace_back("SRV");
                    if (node._buffer_desc._is_create_uav)
                        parts.emplace_back("UAV");
                }
                return join_strings(parts, " | ");
            };
            const auto build_pass_resource_list = [&](const RenderPass *pass, bool input)
            {
                Vector<String> items;
                const auto &accesses = input ? pass->_input_accesses : pass->_output_accesses;
                for (const auto &[handle, access]: accesses)
                {
                    if (const auto *node = GetResourceNode(handle); node != nullptr)
                    {
                        items.emplace_back(std::format("{} v{} [{}]", node->_name, handle._version, build_access_label(access, !input)));
                    }
                }
                std::sort(items.begin(), items.end());
                return join_strings(items, "\n");
            };
            const auto pass_node_id = [](u32 index)
            {
                return std::format("pass_{}", index);
            };
            const auto version_node_id = [](u32 res_id, u32 version)
            {
                return std::format("res_{}_v{}", res_id, version);
            };
            const auto physical_node_id = [](u32 res_id)
            {
                return std::format("phys_{}", res_id);
            };

            HashMap<const RenderPass *, u32> pass_indices;
            for (u32 i = 0; i < _sorted_passes.size(); ++i)
                pass_indices[_sorted_passes[i]] = i;

            struct TimelineCell
            {
                bool _read = false;
                bool _write = false;
            };
            HashMap<u32, Vector<TimelineCell>> timelines;
            const auto ensure_timeline = [&](u32 res_id) -> Vector<TimelineCell> &
            {
                auto &timeline = timelines[res_id];
                if (timeline.empty())
                    timeline.resize(_sorted_passes.size());
                return timeline;
            };
            for (u32 pass_index = 0; pass_index < _sorted_passes.size(); ++pass_index)
            {
                const auto *pass = _sorted_passes[pass_index];
                for (const auto &[handle, access]: pass->_input_accesses)
                {
                    auto &timeline = ensure_timeline(handle._id);
                    timeline[pass_index]._read = timeline[pass_index]._read || access.isRead() || !access.isWrite();
                }
                for (const auto &[handle, access]: pass->_output_accesses)
                {
                    auto &timeline = ensure_timeline(handle._id);
                    timeline[pass_index]._write = timeline[pass_index]._write || access.isWrite() || !access.isRead();
                }
            }

            Vector<std::pair<u32, const ResourceNode *>> resources;
            resources.reserve(_transient_resources.size() + _external_resources.size());
            for (const auto &[id, node]: _transient_resources)
                resources.emplace_back(id, &node);
            for (const auto &[id, node]: _external_resources)
                resources.emplace_back(id, &node);
            std::sort(resources.begin(), resources.end(), [](const auto &lhs, const auto &rhs)
            {
                if (lhs.second->_name == rhs.second->_name)
                    return lhs.first < rhs.first;
                return lhs.second->_name < rhs.second->_name;
            });

            buffer.append("digraph RenderGraph {\n");
            buffer.append("    rankdir=LR;\n");
            buffer.append("    graph [fontname=\"Consolas\", nodesep=0.35, ranksep=0.45, pad=0.2, splines=ortho];\n");
            buffer.append("    node [fontname=\"Consolas\", shape=plain];\n");
            buffer.append("    edge [fontname=\"Consolas\", color=\"#7A7A7A\", arrowsize=0.7];\n");

            buffer.append("    legend [label=<\n");
            buffer.append("    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n");
            buffer.append("        <TR><TD BGCOLOR=\"#2D2D2D\"><FONT COLOR=\"white\"><B>Legend</B></FONT></TD><TD BGCOLOR=\"#D8E9FF\">Read</TD><TD BGCOLOR=\"#D8F5D0\">Write</TD><TD BGCOLOR=\"#B8E6C1\">Read/Write</TD><TD BGCOLOR=\"#EFEFEF\">Unused</TD></TR>\n");
            buffer.append("    </TABLE>>];\n");

            buffer.append("    subgraph cluster_pass_overview {\n");
            buffer.append("        label=\"Pass Overview\";\n");
            buffer.append("        color=\"#8FAADC\";\n");
            buffer.append("        style=rounded;\n");
            for (u32 i = 0; i < _sorted_passes.size(); ++i)
            {
                const auto *pass = _sorted_passes[i];
                const String pass_id = pass_node_id(i);
                const String reads = build_pass_resource_list(pass, true);
                const String writes = build_pass_resource_list(pass, false);
                buffer.append(std::format(
                    "        {} [label=<"
                    "<TABLE BORDER=\"1\" CELLBORDER=\"0\" CELLSPACING=\"0\">"
                    "<TR><TD BGCOLOR=\"{}\"><B>#{} {}</B></TD></TR>"
                    "<TR><TD ALIGN=\"LEFT\">Type: {} | in {} | out {}</TD></TR>"
                    "<TR><TD ALIGN=\"LEFT\">Reads:<BR ALIGN=\"LEFT\"/>{}</TD></TR>"
                    "<TR><TD ALIGN=\"LEFT\">Writes:<BR ALIGN=\"LEFT\"/>{}</TD></TR>"
                    "</TABLE>>];\n",
                    pass_id,
                    pass_type_color(pass->_type),
                    i,
                    escape_html(pass->_name),
                    pass_type_name(pass->_type),
                    pass->_input_accesses.size(),
                    pass->_output_accesses.size(),
                    escape_html(reads),
                    escape_html(writes)));
                if (i + 1u < _sorted_passes.size())
                    buffer.append(std::format("        {} -> {} [style=dashed, color=\"#B0B0B0\", arrowhead=normal, label=\"order\"];\n", pass_id, pass_node_id(i + 1u)));
            }
            buffer.append("    }\n");

            buffer.append("    subgraph cluster_logical_versions {\n");
            buffer.append("        label=\"Logical Resource Versions\";\n");
            buffer.append("        color=\"#9BC995\";\n");
            buffer.append("        style=rounded;\n");
            for (const auto &[res_id, node]: resources)
            {
                if (node->_versions.empty())
                    continue;

                const auto timeline_it = timelines.find(res_id);
                const bool has_timeline = timeline_it != timelines.end();
                bool used = false;
                if (has_timeline)
                {
                    for (const auto &cell: timeline_it->second)
                    {
                        if (cell._read || cell._write)
                        {
                            used = true;
                            break;
                        }
                    }
                }
                if (!used)
                    continue;

                for (const auto &version: node->_versions)
                {
                    const bool has_edges = version._producer != nullptr || !version._consumers.empty();
                    if (!has_edges)
                        continue;

                    Vector<String> consumer_names;
                    for (const auto *consumer: version._consumers)
                    {
                        if (auto it = pass_indices.find(consumer); it != pass_indices.end())
                            consumer_names.emplace_back(std::format("#{} {}", it->second, consumer->_name));
                    }
                    std::sort(consumer_names.begin(), consumer_names.end());

                    String producer = "Imported / Initial";
                    if (version._producer != nullptr)
                    {
                        if (auto it = pass_indices.find(version._producer); it != pass_indices.end())
                            producer = std::format("#{} {}", it->second, version._producer->_name);
                        else
                            producer = version._producer->_name;
                    }

                    i32 first_use = version._first_use;
                    i32 last_use = version._last_use;
                    if (version._producer != nullptr)
                    {
                        if (auto it = pass_indices.find(version._producer); it != pass_indices.end())
                            first_use = first_use < 0 ? static_cast<i32>(it->second) : std::min(first_use, static_cast<i32>(it->second));
                    }
                    for (const auto *consumer: version._consumers)
                    {
                        if (auto it = pass_indices.find(consumer); it != pass_indices.end())
                            last_use = std::max(last_use, static_cast<i32>(it->second));
                    }

                    buffer.append(std::format(
                        "        {} [label=<"
                        "<TABLE BORDER=\"1\" CELLBORDER=\"0\" CELLSPACING=\"0\">"
                        "<TR><TD BGCOLOR=\"#E8F5E3\"><B>{} v{}</B></TD></TR>"
                        "<TR><TD ALIGN=\"LEFT\">{}</TD></TR>"
                        "<TR><TD ALIGN=\"LEFT\">Producer: {}</TD></TR>"
                        "<TR><TD ALIGN=\"LEFT\">Consumers:<BR ALIGN=\"LEFT\"/>{}</TD></TR>"
                        "<TR><TD ALIGN=\"LEFT\">Version span: {} -&gt; {}</TD></TR>"
                        "</TABLE>>];\n",
                        version_node_id(res_id, version._handle._version),
                        escape_html(node->_name),
                        version._handle._version,
                        escape_html(build_resource_desc(*node)),
                        escape_html(producer),
                        escape_html(join_strings(consumer_names, "\n")),
                        first_use >= 0 ? std::format("#{}", first_use) : String("n/a"),
                        last_use >= 0 ? std::format("#{}", last_use) : String("n/a")));
                    buffer.append(std::format(
                        "        {} -> {} [style=dashed, color=\"#8C8C8C\", arrowhead=none];\n",
                        physical_node_id(res_id),
                        version_node_id(res_id, version._handle._version)));
                }
            }
            buffer.append("    }\n");

            buffer.append("    subgraph cluster_physical_lifetime {\n");
            buffer.append("        label=\"Physical Resource Lifetime\";\n");
            buffer.append("        color=\"#D6A77A\";\n");
            buffer.append("        style=rounded;\n");
            for (const auto &[res_id, node]: resources)
            {
                const auto timeline_it = timelines.find(res_id);
                if (timeline_it == timelines.end())
                    continue;

                const auto &timeline = timeline_it->second;
                i32 first_active = -1;
                i32 last_active = -1;
                String header_cells;
                String use_cells;
                for (u32 pass_index = 0; pass_index < timeline.size(); ++pass_index)
                {
                    header_cells += std::format("<TD BGCOLOR=\"#F3F3F3\"><B>{}</B></TD>", pass_index);
                    const auto &cell = timeline[pass_index];
                    String bg = "#EFEFEF";
                    String marker = "-";
                    if (cell._read && cell._write)
                    {
                        bg = "#B8E6C1";
                        marker = "RW";
                    }
                    else if (cell._write)
                    {
                        bg = "#D8F5D0";
                        marker = "W";
                    }
                    else if (cell._read)
                    {
                        bg = "#D8E9FF";
                        marker = "R";
                    }
                    if (cell._read || cell._write)
                    {
                        first_active = first_active < 0 ? static_cast<i32>(pass_index) : std::min(first_active, static_cast<i32>(pass_index));
                        last_active = std::max(last_active, static_cast<i32>(pass_index));
                    }
                    use_cells += std::format("<TD BGCOLOR=\"{}\">{}</TD>", bg, marker);
                }
                if (first_active < 0)
                    continue;

                Vector<String> version_names;
                for (const auto &version: node->_versions)
                    version_names.emplace_back(std::format("v{}", version._handle._version));

                buffer.append(std::format(
                    "        {} [label=<"
                    "<TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">"
                    "<TR><TD BGCOLOR=\"#FFE9D6\" COLSPAN=\"{}\"><B>{}</B><BR ALIGN=\"LEFT\"/>{}</TD></TR>"
                    "<TR><TD BGCOLOR=\"#F3F3F3\"><B>Pass</B></TD>{}</TR>"
                    "<TR><TD BGCOLOR=\"#F3F3F3\"><B>Use</B></TD>{}</TR>"
                    "<TR><TD BGCOLOR=\"#F3F3F3\"><B>Cover</B></TD><TD COLSPAN=\"{}\" ALIGN=\"LEFT\">#{} -&gt; #{} | versions: {}</TD></TR>"
                    "</TABLE>>];\n",
                    physical_node_id(res_id),
                    _sorted_passes.size() + 1u,
                    escape_html(node->_name),
                    escape_html(build_resource_desc(*node)),
                    header_cells,
                    use_cells,
                    _sorted_passes.size(),
                    first_active,
                    last_active,
                    escape_html(join_strings(version_names, ", "))));
            }
            buffer.append("    }\n");

            for (u32 pass_index = 0; pass_index < _sorted_passes.size(); ++pass_index)
            {
                const auto *pass = _sorted_passes[pass_index];
                for (const auto &[handle, access]: pass->_input_accesses)
                {
                    if (const auto *node = GetResourceNode(handle); node != nullptr)
                    {
                        buffer.append(std::format(
                            "    {} -> {} [color=\"#4F81BD\", label=\"{}\"];\n",
                            version_node_id(handle._id, handle._version),
                            pass_node_id(pass_index),
                            escape_dot(build_access_label(access, false))));
                    }
                }
                for (const auto &[handle, access]: pass->_output_accesses)
                {
                    if (GetResourceNode(handle) != nullptr)
                    {
                        buffer.append(std::format(
                            "    {} -> {} [color=\"#4CAF50\", label=\"{}\"];\n",
                            pass_node_id(pass_index),
                            version_node_id(handle._id, handle._version),
                            escape_dot(build_access_label(access, true))));
                    }
                }
            }

            buffer.append("}\n");

            FileManager::WriteFile(filename, false, buffer);
            LOG_INFO(L"CRenderGraph::ExportToFile: Output to {},go https://dreampuf.github.io/GraphvizOnline/ to have a view", filename);
        }

        void RenderGraph::ExportTimelineToFile(const WString &filename)
        {
            std::string buffer;

            const auto escape_html = [](StringView text)
            {
                String escaped;
                escaped.reserve(text.size() + 16u);
                for (char c: text)
                {
                    switch (c)
                    {
                    case '&': escaped += "&amp;"; break;
                    case '<': escaped += "&lt;"; break;
                    case '>': escaped += "&gt;"; break;
                    case '"': escaped += "&quot;"; break;
                    case '\n': escaped += "<BR ALIGN=\"LEFT\"/>"; break;
                    case '\r': break;
                    default: escaped.push_back(c); break;
                    }
                }
                return escaped;
            };
            const auto pass_type_color = [](EPassType type)
            {
                switch (type)
                {
                case EPassType::kGraphics: return String("#D8F5D0");
                case EPassType::kCompute: return String("#D8E9FF");
                case EPassType::kAsyncCompute: return String("#FFE4BF");
                case EPassType::kCopy: return String("#FFF3A6");
                default: return String("#E5E5E5");
                }
            };
            const auto short_pass_name = [](const String &name)
            {
                constexpr size_t kMaxLen = 12u;
                if (name.size() <= kMaxLen)
                    return name;
                return name.substr(0u, kMaxLen - 3u) + "...";
            };
            const auto resource_kind = [](const ResourceNode &node)
            {
                return std::format("{} {}",
                    node._is_external ? "Ext" : "Tmp",
                    node._is_tex ? "Tex" : "Buf");
            };

            struct TimelineCell
            {
                bool _read = false;
                bool _write = false;
            };
            struct TimelineRow
            {
                const ResourceNode *_node = nullptr;
                u32 _resource_id = 0u;
                i32 _first_active = -1;
                i32 _last_active = -1;
                Vector<TimelineCell> _cells;
            };

            HashMap<u32, Vector<TimelineCell>> timelines;
            const auto ensure_timeline = [&](u32 res_id) -> Vector<TimelineCell> &
            {
                auto &timeline = timelines[res_id];
                if (timeline.empty())
                    timeline.resize(_sorted_passes.size());
                return timeline;
            };

            for (u32 pass_index = 0; pass_index < _sorted_passes.size(); ++pass_index)
            {
                const auto *pass = _sorted_passes[pass_index];
                for (const auto &[handle, access]: pass->_input_accesses)
                {
                    auto &cell = ensure_timeline(handle._id)[pass_index];
                    cell._read = cell._read || access.isRead() || !access.isWrite();
                }
                for (const auto &[handle, access]: pass->_output_accesses)
                {
                    auto &cell = ensure_timeline(handle._id)[pass_index];
                    cell._write = cell._write || access.isWrite() || !access.isRead();
                }
            }

            Vector<TimelineRow> rows;
            rows.reserve(_transient_resources.size() + _external_resources.size());
            const auto collect_rows = [&](const auto &resources)
            {
                for (const auto &[res_id, node]: resources)
                {
                    auto it = timelines.find(res_id);
                    if (it == timelines.end())
                        continue;

                    TimelineRow row;
                    row._node = &node;
                    row._resource_id = res_id;
                    row._cells = it->second;
                    for (u32 pass_index = 0; pass_index < row._cells.size(); ++pass_index)
                    {
                        const auto &cell = row._cells[pass_index];
                        if (!cell._read && !cell._write)
                            continue;
                        row._first_active = row._first_active < 0 ? static_cast<i32>(pass_index) : std::min(row._first_active, static_cast<i32>(pass_index));
                        row._last_active = std::max(row._last_active, static_cast<i32>(pass_index));
                    }
                    if (row._first_active >= 0)
                        rows.emplace_back(std::move(row));
                }
            };
            collect_rows(_transient_resources);
            collect_rows(_external_resources);

            std::sort(rows.begin(), rows.end(), [](const TimelineRow &lhs, const TimelineRow &rhs)
            {
                if (lhs._first_active != rhs._first_active)
                    return lhs._first_active < rhs._first_active;
                if (lhs._last_active != rhs._last_active)
                    return lhs._last_active < rhs._last_active;
                if (lhs._node->_name != rhs._node->_name)
                    return lhs._node->_name < rhs._node->_name;
                return lhs._resource_id < rhs._resource_id;
            });

            buffer.append("digraph RenderGraphTimeline {\n");
            buffer.append("    rankdir=TB;\n");
            buffer.append("    graph [fontname=\"Consolas\", pad=0.15];\n");
            buffer.append("    node [fontname=\"Consolas\", shape=plain];\n");

            buffer.append("    timeline [label=<\n");
            buffer.append("    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\">\n");
            buffer.append(std::format("        <TR><TD BGCOLOR=\"#2D2D2D\" COLSPAN=\"{}\"><FONT COLOR=\"white\"><B>RenderGraph Timeline</B></FONT></TD></TR>\n", _sorted_passes.size() + 3u));
            buffer.append(std::format("        <TR><TD ALIGN=\"LEFT\" COLSPAN=\"{}\">R = read, W = write, RW = read/write, span = first..last active pass</TD></TR>\n", _sorted_passes.size() + 3u));
            buffer.append("        <TR><TD BGCOLOR=\"#F3F3F3\"><B>Resource</B></TD><TD BGCOLOR=\"#F3F3F3\"><B>Kind</B></TD><TD BGCOLOR=\"#F3F3F3\"><B>Span</B></TD>");
            for (u32 pass_index = 0; pass_index < _sorted_passes.size(); ++pass_index)
            {
                buffer.append(std::format("<TD BGCOLOR=\"{}\"><B>#{}<BR/>{}</B></TD>",
                    pass_type_color(_sorted_passes[pass_index]->_type),
                    pass_index,
                    escape_html(short_pass_name(_sorted_passes[pass_index]->_name))));
            }
            buffer.append("</TR>\n");

            for (const auto &row: rows)
            {
                const auto &node = *row._node;
                buffer.append(std::format(
                    "        <TR><TD ALIGN=\"LEFT\">{}<BR ALIGN=\"LEFT\"/><FONT POINT-SIZE=\"10\">v:{} | id:{}</FONT></TD><TD>{}</TD><TD>#{}..#{} </TD>",
                    escape_html(node._name),
                    node._versions.size() > 0u ? node._versions.size() - 1u : 0u,
                    row._resource_id,
                    escape_html(resource_kind(node)),
                    row._first_active,
                    row._last_active));

                for (const auto &cell: row._cells)
                {
                    String bg = "#EFEFEF";
                    String marker = "-";
                    if (cell._read && cell._write)
                    {
                        bg = "#B8E6C1";
                        marker = "RW";
                    }
                    else if (cell._write)
                    {
                        bg = "#D8F5D0";
                        marker = "W";
                    }
                    else if (cell._read)
                    {
                        bg = "#D8E9FF";
                        marker = "R";
                    }
                    buffer.append(std::format("<TD BGCOLOR=\"{}\">{}</TD>", bg, marker));
                }
                buffer.append("</TR>\n");
            }

            buffer.append("    </TABLE>>];\n");
            buffer.append("}\n");

            FileManager::WriteFile(filename, false, buffer);
            LOG_INFO(L"RenderGraph::ExportTimelineToFile: Output to {},go https://dreampuf.github.io/GraphvizOnline/ to have a view", filename);
        }

    }// namespace ::Render::RDG
}
