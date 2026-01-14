#include "Render/Features/RayTraceGI.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Renderer.h"
#include "Render/CommandBuffer.h"
#include "Render/RenderGraph/RenderGraph.h"

namespace Ailu
{
    namespace Render
    {
#pragma region RayTraceGI
        RayTraceGI::RayTraceGI() : RenderFeature("RayTraceGI")
        {
            _gi_compute_shader = g_pResourceMgr->Load<ComputeShader>(L"Shaders/raytrace_gi.alasset");
            _gi_pass = MakeScope<GIPass>(_gi_compute_shader.get());
            _gi_compute_shader->SetInts("_PickPixel",{200,200});
        }
        void RayTraceGI::AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data)
        {
            _gi_pass->_debug_pos = _debug_pos;
            _gi_compute_shader->SetInts("_PickPixel", {(i32)_debug_pos.x, (i32)_debug_pos.y});
            _gi_compute_shader->SetInt("_debug_hit_box_idx", _debug_hit_box);
            _debug_pos = {-1.0f, -1.0f};
            _gi_pass->_debug_line_mat->SetInt("_debug_hit_box_idx", _debug_hit_box);
            renderer.EnqueuePass(_gi_pass.get());
        }
#pragma endregion

#pragma region GIPass
        GIPass::GIPass(ComputeShader *cs) : _gi_compute_shader(cs), RenderPass("GIPass")
        {
            _debug_line_mat = MakeRef<Material>(g_pResourceMgr->Load<Shader>(L"Shaders/raytrace_debug_draw.alasset").get(), "RayDebugLineMat");
            _kernel_ray_gen = cs->FindKernel("RayGen");
            _event = (ERenderPassEvent::ERenderPassEvent)(ERenderPassEvent::kAfterTransparent - 5);//before copy color
            BufferDesc desc;
            desc._is_random_write = true;
            desc._is_readable = false;
            desc._format = EALGFormat::kALGFormatUNKOWN;
            desc._element_num = 5000;
            desc._element_size = 32u;//rt_common.hlsli  sizeof(DebugRay)
            desc._size = desc._element_size * desc._element_num;
            desc._target = EGPUBufferTarget::kAppend;
            _debug_buffer = GPUBuffer::Create(desc);
            desc._format = EALGFormat::kALGFormatR32_UINT;
            desc._element_size = sizeof(u32);
            _debug_index_buffer = GPUBuffer::Create(desc);
            _arg_buffer = GPUBuffer::Create(EGPUBufferTarget::kIndirectArguments, sizeof(DrawArguments), 1u, "RayDebugArgsBuffer");
            DrawArguments draw_arg{0u, 1u, 0, 0u};
            _arg_buffer->SetData((const u8 *) &draw_arg, sizeof(DrawArguments));
            _gi_compute_shader->SetBuffer("_debug_rays", _debug_buffer.get());
            _gi_compute_shader->SetBuffer("_debug_ray_indices", &*_debug_index_buffer);
        }
        void GIPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
        {
            static RDG::RGHandle gi_texturte;
            graph.AddPass("RayTrace GI", RDG::PassDesc(RDG::EPassType::kCompute), [&, this](RDG::RenderGraphBuilder &builder)
                          {
                    auto desc = rendering_data._camera_data._camera_color_target_desc;
                    desc._is_random_access = true;
                    gi_texturte = builder.AllocTexture(desc, "_RayTraceGITexture");
                    gi_texturte = builder.Write(gi_texturte,EResourceUsage::kWriteUAV); }, 
                [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
                {
                Vector4f params = {1.0f, 1.0f, (f32) (data._width), (f32) (data._height)};
                params.x /= params.z;
                params.y /= params.w;
                u16 w = (u16) params.z, h = (u16) params.w;
                {
                    if (_debug_pos.x >= 0)
                        _debug_buffer->SetCounter(0u);
                    auto [x, y, z] = _gi_compute_shader->CalculateDispatchNum(_kernel_ray_gen, w, h, 1);
                    //_gi_compute_shader->SetBuffer("_debug_rays", _debug_buffer.get());
                    //_gi_compute_shader->SetBuffer("_debug_ray_indices", &*_debug_index_buffer);
                    _gi_compute_shader->SetTexture("_GI_Texture", graph.Resolve<Texture>(gi_texturte));
                    cmd->Dispatch(_gi_compute_shader, _kernel_ray_gen, x, y, 1);
                } });
            graph.AddPass("Debug GI", RDG::PassDesc(), [&, this](RDG::RenderGraphBuilder &builder)
                          {
                    builder.Read(gi_texturte);
                    builder.Read(rendering_data._rg_handles._color_target);
                    rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target); 
                    }, [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
                { 
                    cmd->Blit(gi_texturte,data._rg_handles._color_target);
                });
            graph.AddPass("Debug Ray", RDG::PassDesc(), [&, this](RDG::RenderGraphBuilder &builder)
                          {
                    builder.Read(rendering_data._rg_handles._color_target);
                    builder.Read(rendering_data._rg_handles._depth_target);
                    rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target);
                    rendering_data._rg_handles._depth_target = builder.Write(rendering_data._rg_handles._depth_target);
                }, 
                [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
                          { 
                    _debug_line_mat->SetBuffer("_ray_list", _debug_buffer.get());
                    cmd->SetRenderTarget(data._rg_handles._color_target,data._rg_handles._depth_target);
                    cmd->CopyCounterValue(&*_debug_buffer, &*_arg_buffer, offsetof(DrawArguments, DrawArguments::_vertex_count_per_instance));
                    cmd->DrawProceduralIndirect(&*_debug_line_mat,0,&*_arg_buffer);
                });
        }
#pragma endregion
    }// namespace Render
} // namespace Ailu