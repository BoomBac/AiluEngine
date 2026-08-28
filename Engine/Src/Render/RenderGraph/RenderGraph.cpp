#include "Render/RenderGraph/RenderGraph.h"
#include "Framework/Common/Hash.hpp"
#include "Render/GraphicsContext.h"
#include "Render/CommandBuffer.h"
#include "Render/FrameResource.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/RenderDebugConfig.h"
#if AILU_ENABLE_FRAME_DEBUGGER
#include "Render/FrameDebugger/FrameCaptureService.h"
#include "Render/FrameDebugger/FrameCaptureTypes.h"
#endif

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

            bool TryUsageToResourceState(EResourceUsage usage, EResourceState &out_state, String &out_error)
            {
                constexpr u32 k_write_mask = static_cast<u32>(EResourceUsage::kWriteUAV) |
                                              static_cast<u32>(EResourceUsage::kWriteRTV) |
                                              static_cast<u32>(EResourceUsage::kDSV) |
                                              static_cast<u32>(EResourceUsage::kCopyDst);
                constexpr u32 k_read_mask = static_cast<u32>(EResourceUsage::kReadSRV) |
                                             static_cast<u32>(EResourceUsage::kCopySrc) |
                                             static_cast<u32>(EResourceUsage::kIndirectArgument);
                constexpr u32 k_known_mask = k_write_mask | k_read_mask |
                                              static_cast<u32>(EResourceUsage::kRaytracingAccel);

                const u32 usage_bits = static_cast<u32>(usage);
                const u32 write_bits = usage_bits & k_write_mask;
                if (usage_bits == 0u)
                {
                    out_error = "access usage is kNone";
                    return false;
                }
                if ((usage_bits & ~k_known_mask) != 0u)
                {
                    out_error = std::format("access usage contains unknown bits 0x{:X}", usage_bits & ~k_known_mask);
                    return false;
                }
                if (write_bits != 0u && (write_bits & (write_bits - 1u)) != 0u)
                {
                    out_error = "multiple write usages cannot be combined";
                    return false;
                }
                if (write_bits != 0u && (usage_bits & ~write_bits) != 0u)
                {
                    out_error = "a write usage cannot be combined with another usage";
                    return false;
                }
                if (HasUsage(usage, EResourceUsage::kRaytracingAccel) && usage_bits !=
                    static_cast<u32>(EResourceUsage::kRaytracingAccel))
                {
                    out_error = "RaytracingAccel cannot be combined with another usage";
                    return false;
                }

                if (HasUsage(usage, EResourceUsage::kWriteUAV))
                    out_state = EResourceState::kUnorderedAccess;
                else if (HasUsage(usage, EResourceUsage::kWriteRTV))
                    out_state = EResourceState::kRenderTarget;
                else if (HasUsage(usage, EResourceUsage::kDSV))
                    out_state = EResourceState::kDepthWrite;
                else if (HasUsage(usage, EResourceUsage::kCopyDst))
                    out_state = EResourceState::kCopyDest;
                else if (HasUsage(usage, EResourceUsage::kRaytracingAccel))
                    out_state = EResourceState::kRaytracingAccelerationStructure;
                else
                {
                    u32 state_bits = 0u;
                    if (HasUsage(usage, EResourceUsage::kReadSRV))
                        state_bits |= static_cast<u32>(EResourceState::kAllShaderResource);
                    if (HasUsage(usage, EResourceUsage::kCopySrc))
                        state_bits |= static_cast<u32>(EResourceState::kCopySource);
                    if (HasUsage(usage, EResourceUsage::kIndirectArgument))
                        state_bits |= static_cast<u32>(EResourceState::kIndirectArgument);
                    out_state = static_cast<EResourceState>(state_bits);
                }
                return true;
            }
        }

        RenderGraph::RenderGraph()
        {
        }

        RenderGraph::~RenderGraph()
        {
        }

        bool RenderGraph::ContainsResource(const GpuResource *resource) const
        {
            if (resource == nullptr)
                return false;
            const auto contains_resource = [resource](const auto &resources)
            {
                for (const auto &[id, node]: resources)
                {
                    if (node.GetResource() == resource)
                        return true;
                }
                return false;
            };
            return contains_resource(_transient_resources) || contains_resource(_external_resources);
        }

        void RenderGraph::EndFrame()
        {
            _transient_tex_handles.clear();
            _transient_buffer_handles.clear();
            for (auto *pass: _passes)
                _pass_pool.Release(pass);
            _passes.clear();
            for (auto& it: _transient_resources)
            {
                auto& [id, node] = it;
                ReleaseTransientResource(node);
            }
            _transient_resources.clear();
            for (auto &[id, node]: _external_resources)
                ResetResourceNodeVersions(node);
            _sorted_passes.clear();
            _compiled_passes.clear();
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
                    node._pool_tex_handle = FrameResourceManager::Get().AllocTexture(it->second._tex_desc);
                    if (node._pool_tex_handle._res == nullptr)
                    {
                        LOG_ERROR("RenderGraph::CreatePhysicsTexture: failed to allocate resource {}", node._name);
                        return nullptr;
                    }
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
                    if (node._pool_buffer_handle._res == nullptr)
                    {
                        LOG_ERROR("RenderGraph::CreatePhysicsBuffer: failed to allocate resource {}", node._name);
                        return nullptr;
                    }
                    node._pool_buffer_handle._res->Name(node._name);
                    node._is_allocated = true;
                }
                return node._pool_buffer_handle._res;
            }
            AL_ASSERT(false);
            return nullptr;
        }

        bool RenderGraph::PrepareResources()
        {
            PROFILE_BLOCK_CPU("RenderGraph::PrepareResources")
            bool is_valid = true;
            const auto prepare_handle = [&](RGHandle handle)
            {
                CreatePhysicalResources(handle);
                auto *node = GetResourceNode(handle);
                if (node == nullptr || node->GetResource() == nullptr)
                {
                    LOG_ERROR("RenderGraph::PrepareResources: unresolved resource {}.{}", handle._id, handle._version);
                    is_valid = false;
                }
            };

            for (const auto &compiled_pass: _compiled_passes)
            {
                if (compiled_pass._pass == nullptr)
                    continue;
                for (const auto &record: compiled_pass._pass->_input_access_records)
                    prepare_handle(record._handle);
                for (const auto &record: compiled_pass._pass->_output_access_records)
                    prepare_handle(record._handle);
            }
            return is_valid;
        }


        RGHandle RenderGraph::Import(GpuResource *external)
        {
            if (external == nullptr)
                return RGHandle(0u);
            EResourceState initial_state = EResourceState::kCommon;
            if (auto *rt = dynamic_cast<RenderTexture *>(external); rt != nullptr && rt->IsSwapChain())
                initial_state = EResourceState::kPresent;
            else if (external->IsReady())
            {
                external->TryCurrentResourceState(initial_state);
            }
            return Import(external, initial_state);
        }

        RGHandle RenderGraph::Import(GpuResource *external, EResourceState initial_state)
        {
            if (external == nullptr)
                return RGHandle(0u);
            auto res_type = external->GetResourceType();
            if (res_type == EGpuResType::kVertexBuffer || res_type == EGpuResType::kIndexBuffer || res_type == EGpuResType::kConstBuffer || res_type == EGpuResType::kGraphicsPSO)
            {
                AL_ASSERT_MSG(false, "RenderGraph::Import: Unsupported resource type!");
            }
            bool is_tex = res_type == EGpuResType::kTexture || res_type == EGpuResType::kRenderTexture;
            auto& external_pool = is_tex ? _external_tex_handles : _external_buffer_handles;
            std::lock_guard lock(_mutex);
            const auto update_external_metadata = [&](ResourceNode &node)
            {
                node._extern_raw_res = external;
                node._is_tex = is_tex;
                node._is_render_output = res_type == EGpuResType::kRenderTexture;
                node._is_external = true;
                node._is_transient = false;
                node._initial_state = initial_state;
                node._mip_count = 1u;
                node._array_slice_count = 1u;
                node._is_depth_resource = false;

                if (auto *texture = dynamic_cast<Texture *>(external); texture != nullptr)
                {
                    node._mip_count = std::max<u32>(1u, texture->MipmapLevel());
                    switch (texture->Dimension())
                    {
                    case ETextureDimension::kCube:
                        node._array_slice_count = 6u;
                        break;
                    case ETextureDimension::kCubeArray:
                        if (auto *render_texture = dynamic_cast<RenderTexture *>(texture); render_texture != nullptr)
                            node._array_slice_count = std::max<u32>(1u, render_texture->ArraySlice()) * 6u;
                        break;
                    case ETextureDimension::kTex2DArray:
                        if (auto *render_texture = dynamic_cast<RenderTexture *>(texture); render_texture != nullptr)
                            node._array_slice_count = std::max<u32>(1u, render_texture->ArraySlice());
                        break;
                    default:
                        break;
                    }

                    switch (texture->PixelFormat())
                    {
                    case EALGFormat::kALGFormatD16_UNORM:
                    case EALGFormat::kALGFormatD24S8_UINT:
                    case EALGFormat::kALGFormatD32_FLOAT:
                    case EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT:
                        node._is_depth_resource = true;
                        break;
                    default:
                        break;
                    }
                }
            };
            if (auto it = external_pool.find(external->Name()); it != external_pool.end())
            {
                auto &existing = _external_resources[it->second._id];
                if (existing._extern_raw_res != external)
                {
                    LOG_WARNING("RenderGraph::Import: External resource name conflict, re-binding [{}]", external->Name());
                }
                update_external_metadata(existing);
                return it->second;
            }

            RGHandle handle(s_next_handle_id++);
            ResourceNode node(external->Name());
            update_external_metadata(node);
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

        bool RenderGraph::ValidateResourceAccesses()
        {
            bool is_valid = true;

            const auto get_texture_subresource_counts = [](const ResourceNode &node, u32 &out_mip_count,
                                                            u32 &out_slice_count)
            {
                out_mip_count = node._is_tex ? node._mip_count : 1u;
                out_slice_count = node._is_tex ? node._array_slice_count : 1u;
            };

            const auto ranges_overlap = [](const ResourceAccess &lhs, const ResourceAccess &rhs)
            {
                if (lhs._all_sub_resources || rhs._all_sub_resources)
                    return true;

                const u64 lhs_mip_end = static_cast<u64>(lhs._mip_level) + lhs._mip_count;
                const u64 rhs_mip_end = static_cast<u64>(rhs._mip_level) + rhs._mip_count;
                const u64 lhs_slice_end = static_cast<u64>(lhs._array_slice) + lhs._array_slice_count;
                const u64 rhs_slice_end = static_cast<u64>(rhs._array_slice) + rhs._array_slice_count;
                return lhs._mip_level < rhs_mip_end && rhs._mip_level < lhs_mip_end &&
                       lhs._array_slice < rhs_slice_end && rhs._array_slice < lhs_slice_end;
            };

            const auto validate_access = [&](const RenderPass *pass, RGHandle handle, const ResourceAccess &access, bool is_input)
            {
                auto *node = GetResourceNode(handle);
                if (node == nullptr)
                {
                    LOG_ERROR("RenderGraph validation failed: pass={}, resource {}.{} does not exist", pass->_name,
                              handle._id, handle._version);
                    is_valid = false;
                    return;
                }
                if (handle._version >= node->_versions.size())
                {
                    LOG_ERROR("RenderGraph validation failed: pass={}, resource {}.{} has an invalid version", pass->_name,
                              node->_name, handle._version);
                    is_valid = false;
                    return;
                }

                EResourceState state = EResourceState::kCommon;
                String state_error;
                if (!TryUsageToResourceState(access._usage, state, state_error))
                {
                    LOG_ERROR("RenderGraph validation failed: pass={}, resource={}, version={}, usage={}, {}",
                              pass->_name, node->_name, handle._version, static_cast<u32>(access._usage), state_error);
                    is_valid = false;
                    return;
                }

                if (node->_is_transient && is_input && handle._version == 0u &&
                    node->_versions[handle._version]._producer == nullptr)
                {
                    LOG_ERROR("RenderGraph validation failed: pass={}, resource={} is read before its first write",
                              pass->_name, node->_name);
                    is_valid = false;
                }

                if (!node->_is_tex)
                {
                    if (!access._all_sub_resources)
                    {
                        LOG_ERROR("RenderGraph validation failed: pass={}, buffer={} cannot use a subresource range",
                                  pass->_name, node->_name);
                        is_valid = false;
                    }
                    if (node->_is_transient)
                    {
                        const auto &desc = node->_buffer_desc;
                        if (HasUsage(access._usage, EResourceUsage::kReadSRV) && !desc._is_create_srv)
                        {
                            LOG_ERROR("RenderGraph validation failed: pass={}, buffer={} has no SRV", pass->_name, node->_name);
                            is_valid = false;
                        }
                        if (HasUsage(access._usage, EResourceUsage::kWriteUAV) &&
                            (!desc._is_create_uav || !desc._is_random_write))
                        {
                            LOG_ERROR("RenderGraph validation failed: pass={}, buffer={} has no UAV capability",
                                      pass->_name, node->_name);
                            is_valid = false;
                        }
                    }
                    return;
                }

                u32 mip_count = 1u;
                u32 slice_count = 1u;
                get_texture_subresource_counts(*node, mip_count, slice_count);
                if (!access._all_sub_resources &&
                    (access._mip_count == 0u || access._array_slice_count == 0u || access._mip_level >= mip_count ||
                     access._array_slice >= slice_count || access._mip_count > mip_count - access._mip_level ||
                     access._array_slice_count > slice_count - access._array_slice))
                {
                    LOG_ERROR("RenderGraph validation failed: pass={}, texture={} has an invalid subresource range "
                              "mip={} count={} slice={} count={}",
                              pass->_name, node->_name, access._mip_level, access._mip_count, access._array_slice,
                              access._array_slice_count);
                    is_valid = false;
                }

                if (node->_is_transient)
                {
                    const auto &desc = node->_tex_desc;
                    if (HasUsage(access._usage, EResourceUsage::kWriteRTV) && !desc._is_color_target)
                    {
                        LOG_ERROR("RenderGraph validation failed: pass={}, texture={} is not a color target",
                                  pass->_name, node->_name);
                        is_valid = false;
                    }
                    if (HasUsage(access._usage, EResourceUsage::kDSV) && !desc._is_depth_target)
                    {
                        LOG_ERROR("RenderGraph validation failed: pass={}, texture={} is not a depth target",
                                  pass->_name, node->_name);
                        is_valid = false;
                    }
                    if (HasUsage(access._usage, EResourceUsage::kWriteUAV) && !desc._is_random_access)
                    {
                        LOG_ERROR("RenderGraph validation failed: pass={}, texture={} has no UAV capability",
                                  pass->_name, node->_name);
                        is_valid = false;
                    }
                }
            };

            for (const auto *pass: _passes)
            {
                Vector<std::pair<RGHandle, ResourceAccess>> combined_accesses;
                const auto collect_access = [&](const auto &record, bool is_input)
                {
                    validate_access(pass, record._handle, record._access, is_input);
                    for (auto &[combined_handle, combined_access]: combined_accesses)
                    {
                        if (combined_handle == record._handle && ranges_overlap(combined_access, record._access))
                        {
                            combined_access._usage = combined_access._usage | record._access._usage;
                            return;
                        }
                    }
                    combined_accesses.emplace_back(record._handle, record._access);
                };
                for (const auto &record: pass->_input_access_records)
                    collect_access(record, true);
                for (const auto &record: pass->_output_access_records)
                    collect_access(record, false);

                for (const auto &[handle, access]: combined_accesses)
                {
                    EResourceState state = EResourceState::kCommon;
                    String state_error;
                    if (!TryUsageToResourceState(access._usage, state, state_error))
                    {
                        auto *node = GetResourceNode(handle);
                        LOG_ERROR("RenderGraph validation failed: pass={}, resource={}, version={}, combined usage={}, {}",
                                  pass->_name, node != nullptr ? node->_name : String("unknown"), handle._version,
                                  static_cast<u32>(access._usage), state_error);
                        is_valid = false;
                    }
                }
            }
            return is_valid;
        }

        EResourceState RenderGraph::InitialResourceState(const ResourceNode &node) const
        {
            return node._initial_state;
        }

        Vector<u32> RenderGraph::ResolveBarrierSubResources(RGHandle handle, const ResourceAccess &access) const
        {
            if (access._all_sub_resources)
                return {kTotalSubRes};

            auto *self = const_cast<RenderGraph *>(this);
            const auto *node = self->GetResourceNode(handle);
            if (node == nullptr || !node->_is_tex)
                return {kTotalSubRes};

            const u32 mip_count = std::max<u32>(1u, access._mip_count);
            const u32 slice_count = std::max<u32>(1u, access._array_slice_count);
            Vector<u32> sub_resources;
            sub_resources.reserve(mip_count * slice_count);
            for (u32 slice = access._array_slice; slice < access._array_slice + slice_count; ++slice)
            {
                for (u32 mip = access._mip_level; mip < access._mip_level + mip_count; ++mip)
                    sub_resources.push_back(slice * node->_mip_count + mip);
            }
            return sub_resources;
        }

        bool RenderGraph::CompileResourceBarriers()
        {
            PROFILE_BLOCK_CPU("RenderGraph::CompileResourceBarriers")
            _compiled_passes.clear();
            _compiled_passes.reserve(_sorted_passes.size());
            bool is_valid = true;

            HashMap<u32, EResourceState> total_states;
            HashMap<u32, HashMap<u32, EResourceState>> sub_resource_states;

            const auto resolve_node = [&](RGHandle handle) -> ResourceNode *
            {
                auto *node = GetResourceNode(handle);
                if (node == nullptr)
                {
                    LOG_ERROR("RenderGraph::CompileResourceBarriers: missing resource {}.{}", handle._id, handle._version);
                    is_valid = false;
                    return nullptr;
                }
                if (!total_states.contains(handle._id))
                    total_states[handle._id] = InitialResourceState(*node);
                return node;
            };

            const auto get_state = [&](u32 resource_id, u32 sub_resource)
            {
                auto total_it = total_states.find(resource_id);
                const EResourceState total_state = total_it != total_states.end() ? total_it->second : EResourceState::kCommon;
                if (sub_resource == kTotalSubRes)
                    return total_state;
                if (auto sub_it = sub_resource_states.find(resource_id); sub_it != sub_resource_states.end())
                {
                    if (auto state_it = sub_it->second.find(sub_resource); state_it != sub_it->second.end())
                        return state_it->second;
                }
                return total_state;
            };

            const auto set_state = [&](u32 resource_id, u32 sub_resource, EResourceState state)
            {
                if (sub_resource == kTotalSubRes)
                {
                    total_states[resource_id] = state;
                    sub_resource_states[resource_id].clear();
                    return;
                }
                sub_resource_states[resource_id][sub_resource] = state;
            };

            const auto compile_access = [&](CompiledRenderPass &compiled_pass, RGHandle handle, const ResourceAccess &access)
            {
                auto *node = resolve_node(handle);
                if (node == nullptr)
                    return;

                EResourceState after = EResourceState::kCommon;
                String state_error;
                if (!TryUsageToResourceState(access._usage, after, state_error))
                {
                    LOG_ERROR("RenderGraph::CompileResourceBarriers: invalid usage in pass={}, resource={}, version={}, {}",
                              compiled_pass._pass ? compiled_pass._pass->_name : String("unknown"), node->_name,
                              handle._version, state_error);
                    is_valid = false;
                    return;
                }
                // D3D12 depth-stencil resources can contain separate depth and stencil planes.  The
                // render-target subresource index only covers mip/slice, so every access to a depth texture
                // must transition every plane together or a later DSV access can observe a stale plane state.
                const bool is_depth_access = node->_is_tex &&
                    (node->_is_depth_resource || HasUsage(access._usage, EResourceUsage::kDSV));
                const Vector<u32> sub_resources = is_depth_access ? Vector<u32>{kTotalSubRes} :
                                                                  ResolveBarrierSubResources(handle, access);
                for (const u32 sub_resource: sub_resources)
                {
                    const EResourceState before = get_state(handle._id, sub_resource);
#if AILU_ENABLE_RESOURCE_STATE_TRACE
                    const bool is_trace_resource = node->_name.find("GBuffer0") != String::npos ||
                                                   node->_name.find("light probe") != String::npos ||
                                                   node->_name.find("_MainLightShadowMap") != String::npos ||
                                                   node->_name.find("_AddLightShadowMaps") != String::npos ||
                                                   node->_name.find("VolumetricFogAccumTexture") != String::npos;
                    if (is_trace_resource)
                    {
                        LOG_WARNING("RenderGraph resource access: pass={}, resource={}, handle={}.{}, sub_res={}, "
                                    "before={}, after={}, barrier={}, all_sub_resources={}, mip={}, "
                                    "mip_count={}, slice={}, slice_count={}",
                                    compiled_pass._pass ? compiled_pass._pass->_name : String("unknown"),
                                    node->_name,
                                    handle._id,
                                    handle._version,
                                    sub_resource,
                                    static_cast<u32>(before),
                                    static_cast<u32>(after),
                                    before != after,
                                    access._all_sub_resources,
                                    access._mip_level,
                                    access._mip_count,
                                    access._array_slice,
                                    access._array_slice_count);
                    }
#endif
                    if (sub_resource == kTotalSubRes)
                    {
                        if (before != after)
                        {
                            compiled_pass._pre_barriers.push_back({handle, before, after, sub_resource});
                        }
                        else if (auto sub_it = sub_resource_states.find(handle._id);
                                 sub_it != sub_resource_states.end())
                        {
                            for (const auto &[tracked_sub_resource, tracked_state]: sub_it->second)
                            {
                                if (tracked_state != after)
                                    compiled_pass._pre_barriers.push_back(
                                        {handle, tracked_state, after, tracked_sub_resource});
                            }
                        }
                        set_state(handle._id, sub_resource, after);
                    }
                    else
                    {
                        if (before != after)
                            compiled_pass._pre_barriers.push_back({handle, before, after, sub_resource});
                        set_state(handle._id, sub_resource, after);
                    }
                }
            };

            for (u32 pass_index = 0u; pass_index < _sorted_passes.size(); ++pass_index)
            {
                auto *pass = _sorted_passes[pass_index];
                auto &compiled_pass = _compiled_passes.emplace_back();
                compiled_pass._pass = pass;
                compiled_pass._submission_index = pass_index;

                for (const auto &record: pass->_input_access_records)
                    compile_access(compiled_pass, record._handle, record._access);
                for (const auto &record: pass->_output_access_records)
                    compile_access(compiled_pass, record._handle, record._access);
            }

            if (!is_valid)
            {
                _compiled_passes.clear();
                return false;
            }

            HashMap<u32, u32> resource_last_use_pass;
            for (u32 pass_index = 0u; pass_index < _sorted_passes.size(); ++pass_index)
            {
                auto *pass = _sorted_passes[pass_index];
                for (const auto &record: pass->_input_access_records)
                {
                    if (GetResourceNode(record._handle) != nullptr)
                        resource_last_use_pass[record._handle._id] = pass_index;
                }
                for (const auto &record: pass->_output_access_records)
                {
                    if (GetResourceNode(record._handle) != nullptr)
                        resource_last_use_pass[record._handle._id] = pass_index;
                }
            }

            for (const auto &[resource_id, last_use_pass]: resource_last_use_pass)
            {
                auto *node = GetResourceNode(RGHandle(resource_id));
                auto state_it = total_states.find(resource_id);
                if (node == nullptr || state_it == total_states.end())
                    continue;

                const EResourceState initial_state = InitialResourceState(*node);
                // Transient resources are leased from FrameResourceManager with COMMON as the pool boundary.
                // Restore every resource to the state expected by the next lease at the end of the graph.
                const EResourceState restore_state = node->_is_transient ? node->_initial_state : initial_state;
                auto sub_state_it = sub_resource_states.find(resource_id);
                if (sub_state_it != sub_resource_states.end() && !sub_state_it->second.empty() &&
                    state_it->second == restore_state)
                {
                    for (const auto &[sub_resource, state]: sub_state_it->second)
                    {
                        if (state != restore_state)
                        {
#if AILU_ENABLE_RESOURCE_STATE_TRACE
                            if (node->_name.find("GBuffer0") != String::npos ||
                                node->_name.find("light probe") != String::npos ||
                                node->_name.find("_MainLightShadowMap") != String::npos ||
                                node->_name.find("_AddLightShadowMaps") != String::npos ||
                                node->_name.find("VolumetricFogAccumTexture") != String::npos)
                            {
                                LOG_WARNING("RenderGraph resource post barrier: pass={}, resource={}, handle={}, "
                                            "sub_res={}, before={}, after={}",
                                            _compiled_passes[last_use_pass]._pass ?
                                                _compiled_passes[last_use_pass]._pass->_name : String("unknown"),
                                            node->_name,
                                            resource_id,
                                            sub_resource,
                                            static_cast<u32>(state),
                                            static_cast<u32>(restore_state));
                            }
#endif
                            _compiled_passes[last_use_pass]._post_barriers.push_back(
                                {RGHandle(resource_id), state, restore_state, sub_resource});
                        }
                    }
                }
                else if (state_it->second != restore_state)
                {
#if AILU_ENABLE_RESOURCE_STATE_TRACE
                    if (node->_name.find("GBuffer0") != String::npos ||
                        node->_name.find("light probe") != String::npos ||
                        node->_name.find("_MainLightShadowMap") != String::npos ||
                        node->_name.find("_AddLightShadowMaps") != String::npos ||
                        node->_name.find("VolumetricFogAccumTexture") != String::npos)
                    {
                        LOG_WARNING("RenderGraph resource post barrier: pass={}, resource={}, handle={}, sub_res=all, "
                                    "before={}, after={}",
                                        _compiled_passes[last_use_pass]._pass ?
                                        _compiled_passes[last_use_pass]._pass->_name : String("unknown"),
                                    node->_name,
                                    resource_id,
                                    static_cast<u32>(state_it->second),
                                    static_cast<u32>(restore_state));
                    }
#endif
                    _compiled_passes[last_use_pass]._post_barriers.push_back(
                        {RGHandle(resource_id), state_it->second, restore_state, kTotalSubRes});
                }
            }

            return true;
        }

        bool RenderGraph::Compile()
        {
            PROFILE_BLOCK_CPU("RenderGraph::Compile")

            _compile_stats = {};
            _compile_stats._pass_count = static_cast<u32>(_passes.size());
            const auto count_resources = [&](const auto &resources)
            {
                _compile_stats._resource_count += static_cast<u32>(resources.size());
                for (const auto &[id, node]: resources)
                    _compile_stats._resource_version_count += static_cast<u32>(node._versions.size());
            };
            count_resources(_transient_resources);
            count_resources(_external_resources);

            _sorted_passes.clear();

            if (!ValidateResourceAccesses())
                return false;

            HashMap<RenderPass*, Vector<RenderPass*>> pass_dependencies;
            HashMap<RenderPass*, std::unordered_set<RenderPass*>> unique_dependencies;
            HashMap<RenderPass*, u32> in_degrees;

            {
                PROFILE_BLOCK_CPU("RenderGraph::BuildDependency")
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
                                    _compile_stats._dependency_edge_count++;
                                }
                            }
                        }
                    }
                };

                build_dependencies(_transient_resources);
                build_dependencies(_external_resources);
            }

            {
                PROFILE_BLOCK_CPU("RenderGraph::TopologicalSort")
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
            }

            if (_sorted_passes.size() != _passes.size())
            {
                LOG_ERROR("RenderGraph::Compile: cycle detected!");
                return false;
            }
            _compile_stats._compiled_pass_count = static_cast<u32>(_sorted_passes.size());

            {
                PROFILE_BLOCK_CPU("RenderGraph::AnalyzeResourceLifetime")
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
            }
            if (!CompileResourceBarriers())
                return false;
            for (const auto &compiled_pass: _compiled_passes)
                _compile_stats._barrier_count += static_cast<u32>(compiled_pass._pre_barriers.size() +
                                                                   compiled_pass._post_barriers.size());
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
            if (!PrepareResources())
                return;
            PROFILE_BLOCK_CPU("RenderGraph::Execute")
            for (const auto &compiled_pass: _compiled_passes)
            {
                auto *pass = compiled_pass._pass;
                if (pass == nullptr)
                    continue;
                {
                    PROFILE_BLOCK_CPU(pass->_name)
                    auto cmd = CommandBufferPool::Get(pass->_name);
                    cmd->SetRenderGraph(this);
#if AILU_ENABLE_FRAME_DEBUGGER
                    if (FrameDebugger::FrameCaptureService::ActiveSession() != nullptr)
                    {
                        FrameDebugger::CapturePassMetadata pass_meta;
                        pass_meta._render_graph = this;
                        pass_meta._compiled_pass = &compiled_pass;
                        cmd->SetCapturePassMetadata(pass_meta);
                    }
#endif
                    for (const auto &record: pass->_input_access_records)
                    {
                        cmd->UseRenderGraphResource(Resolve<GpuResource>(record._handle));
                    }
                    for (const auto &record: pass->_output_access_records)
                    {
                        cmd->UseRenderGraphResource(Resolve<GpuResource>(record._handle));
                    }
                    for (const auto &barrier: compiled_pass._pre_barriers)
                    {
                        auto *resource = Resolve<GpuResource>(barrier._handle);
                        if (resource != nullptr)
                            cmd->ResourceBarrier(resource, barrier._before, barrier._after, barrier._sub_resource);
                    }
                    {
                        PROFILE_BLOCK_GPU(cmd.get(), pass->_name)
                        pass->Execute(*this, cmd.get(), data);
                    }
                    for (const auto &barrier: compiled_pass._post_barriers)
                    {
                        auto *resource = Resolve<GpuResource>(barrier._handle);
                        if (resource != nullptr)
                            cmd->ResourceBarrier(resource, barrier._before, barrier._after, barrier._sub_resource);
                    }
                    for (const auto &record: pass->_output_access_records)
                    {
                        auto *res = Resolve<GpuResource>(record._handle);
                        if (HasUsage(record._access._usage, EResourceUsage::kWriteUAV) && res != nullptr)
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
            const auto state_name = [](EResourceState state)
            {
                switch (state)
                {
                case EResourceState::kCommon: return String("Common/Present");
                case EResourceState::kRenderTarget: return String("RenderTarget");
                case EResourceState::kUnorderedAccess: return String("UnorderedAccess");
                case EResourceState::kDepthWrite: return String("DepthWrite");
                case EResourceState::kDepthRead: return String("DepthRead");
                case EResourceState::kNonPixelShaderResource: return String("NonPixelSRV");
                case EResourceState::kPixelShaderResource: return String("PixelSRV");
                case EResourceState::kAllShaderResource: return String("AllShaderSRV");
                case EResourceState::kIndirectArgument: return String("IndirectArg");
                case EResourceState::kCopyDest: return String("CopyDest");
                case EResourceState::kCopySource: return String("CopySource");
                case EResourceState::kRaytracingAccelerationStructure: return String("RTAS");
                default: return std::format("0x{:X}", static_cast<u32>(state));
                }
            };
            const auto build_access_label = [&](const ResourceAccess &access, bool is_write)
            {
                Vector<String> parts;
                parts.emplace_back(usage_name(access._usage));
                if (is_write || access._load != ELoadStoreAction::kLoad || access._store != ELoadStoreAction::kStore)
                    parts.emplace_back(std::format("{}->{}", load_store_name(access._load), load_store_name(access._store)));
                if (!access._all_sub_resources)
                {
                    parts.emplace_back(access._mip_count == 1u ? std::format("mip {}", access._mip_level) :
                                       std::format("mip {}..{}", access._mip_level, access._mip_level + access._mip_count - 1u));
                    if (access._array_slice != 0u || access._array_slice_count != 1u)
                    {
                        parts.emplace_back(access._array_slice_count == 1u ? std::format("slice {}", access._array_slice) :
                                           std::format("slice {}..{}", access._array_slice,
                                                       access._array_slice + access._array_slice_count - 1u));
                    }
                }
                return join_strings(parts, ", ");
            };
            const auto build_resource_desc = [&](const ResourceNode &node)
            {
                Vector<String> parts;
                parts.emplace_back(node._is_external ? "External" : "Transient");
                parts.emplace_back(node._is_tex ? "Texture" : "Buffer");
                if (node._is_tex)
                {
                    if (node._is_external)
                    {
                        parts.emplace_back(std::format("mip {}", node._mip_count));
                        if (node._array_slice_count > 1u)
                            parts.emplace_back(std::format("array {}", node._array_slice_count));
                        if (node._is_render_output)
                            parts.emplace_back("RT");
                        if (node._is_depth_resource)
                            parts.emplace_back("Depth");
                    }
                    else
                    {
                        if (node._tex_desc._fixed_size)
                            parts.emplace_back(std::format("{}x{}x{}", node._tex_desc._width, node._tex_desc._height,
                                                           node._tex_desc._depth));
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
                }
                else
                {
                    if (node._is_external)
                        parts.emplace_back("external buffer");
                    else
                    {
                        parts.emplace_back(std::format("size {}", node._buffer_desc._size));
                        parts.emplace_back(std::format("elem {}x{}", node._buffer_desc._element_num,
                                                       node._buffer_desc._element_size));
                        if (node._buffer_desc._is_create_srv)
                            parts.emplace_back("SRV");
                        if (node._buffer_desc._is_create_uav)
                            parts.emplace_back("UAV");
                    }
                }
                return join_strings(parts, " | ");
            };
            const auto build_pass_resource_list = [&](const RenderPass *pass, bool input)
            {
                Vector<String> items;
                const auto &accesses = input ? pass->_input_access_records : pass->_output_access_records;
                for (const auto &record: accesses)
                {
                    if (const auto *node = GetResourceNode(record._handle); node != nullptr)
                    {
                        items.emplace_back(std::format("{} v{} [{}]", node->_name, record._handle._version,
                                                       build_access_label(record._access, !input)));
                    }
                }
                std::sort(items.begin(), items.end());
                return join_strings(items, "\n");
            };
            const auto build_barrier_list = [&](const CompiledRenderPass &compiled_pass)
            {
                Vector<String> items;
                for (const auto &barrier: compiled_pass._pre_barriers)
                {
                    const auto *node = GetResourceNode(barrier._handle);
                    const String resource_name = node != nullptr ? node->_name : String("null");
                    const String sub_res = barrier._sub_resource == kTotalSubRes ? String("all") : std::format("sub {}", barrier._sub_resource);
                    items.emplace_back(std::format("{} [{}] {} -> {}", resource_name, sub_res, state_name(barrier._before),
                                                   state_name(barrier._after)));
                }
                for (const auto &barrier: compiled_pass._post_barriers)
                {
                    const auto *node = GetResourceNode(barrier._handle);
                    const String resource_name = node != nullptr ? node->_name : String("null");
                    const String sub_res = barrier._sub_resource == kTotalSubRes ? String("all") : std::format("sub {}", barrier._sub_resource);
                    items.emplace_back(std::format("post {} [{}] {} -> {}", resource_name, sub_res, state_name(barrier._before),
                                                   state_name(barrier._after)));
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
                for (const auto &record: pass->_input_access_records)
                {
                    auto &timeline = ensure_timeline(record._handle._id);
                    timeline[pass_index]._read = timeline[pass_index]._read || record._access.isRead() || !record._access.isWrite();
                }
                for (const auto &record: pass->_output_access_records)
                {
                    auto &timeline = ensure_timeline(record._handle._id);
                    timeline[pass_index]._write = timeline[pass_index]._write || record._access.isWrite() || !record._access.isRead();
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
                const String barriers = i < _compiled_passes.size() ? build_barrier_list(_compiled_passes[i]) : String("none");
                buffer.append(std::format(
                    "        {} [label=<"
                    "<TABLE BORDER=\"1\" CELLBORDER=\"0\" CELLSPACING=\"0\">"
                    "<TR><TD BGCOLOR=\"{}\"><B>#{} {}</B></TD></TR>"
                    "<TR><TD ALIGN=\"LEFT\">Type: {} | in {} | out {}</TD></TR>"
                    "<TR><TD ALIGN=\"LEFT\">Reads:<BR ALIGN=\"LEFT\"/>{}</TD></TR>"
                    "<TR><TD ALIGN=\"LEFT\">Writes:<BR ALIGN=\"LEFT\"/>{}</TD></TR>"
                    "<TR><TD ALIGN=\"LEFT\">Barriers:<BR ALIGN=\"LEFT\"/>{}</TD></TR>"
                    "</TABLE>>];\n",
                    pass_id,
                    pass_type_color(pass->_type),
                    i,
                    escape_html(pass->_name),
                    pass_type_name(pass->_type),
                    pass->_input_access_records.size(),
                    pass->_output_access_records.size(),
                    escape_html(reads),
                    escape_html(writes),
                    escape_html(barriers)));
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
                for (const auto &record: pass->_input_access_records)
                {
                    if (const auto *node = GetResourceNode(record._handle); node != nullptr)
                    {
                        buffer.append(std::format(
                            "    {} -> {} [color=\"#4F81BD\", label=\"{}\"];\n",
                            version_node_id(record._handle._id, record._handle._version),
                            pass_node_id(pass_index),
                            escape_dot(build_access_label(record._access, false))));
                    }
                }
                for (const auto &record: pass->_output_access_records)
                {
                    if (GetResourceNode(record._handle) != nullptr)
                    {
                        buffer.append(std::format(
                            "    {} -> {} [color=\"#4CAF50\", label=\"{}\"];\n",
                            pass_node_id(pass_index),
                            version_node_id(record._handle._id, record._handle._version),
                            escape_dot(build_access_label(record._access, true))));
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
                for (const auto &record: pass->_input_access_records)
                {
                    auto &cell = ensure_timeline(record._handle._id)[pass_index];
                    cell._read = cell._read || record._access.isRead() || !record._access.isWrite();
                }
                for (const auto &record: pass->_output_access_records)
                {
                    auto &cell = ensure_timeline(record._handle._id)[pass_index];
                    cell._write = cell._write || record._access.isWrite() || !record._access.isRead();
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
