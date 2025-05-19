#include "Render/Features/GpuTerrain.h"
#include "Render/CommandBuffer.h"
#include "Framework/Common/Profiler.h"
#include "Render/Renderer.h"
#include "Framework/Common/ResourceMgr.h"
#include "pch.h"

namespace Ailu::Render
{
#pragma region GpuTerrain
    GpuTerrain::GpuTerrain() : RenderFeature("GpuTerrain")
    {
        _terrain_pass = AL_NEW(TerrainPass);
        _terrain_gen = g_pResourceMgr->Load<ComputeShader>(L"Shaders/gpu_terrain.alasset");
        _plane = g_pResourceMgr->Load<Mesh>(L"Meshs/terrain_plane.alasset");
        _terrain_mat = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/terrain.alasset"),"DefaultTerrain");
        _terrain_pass->Setup(_terrain_gen.get(),_plane.get(),_terrain_mat.get());
    }
    GpuTerrain::~GpuTerrain()
    {
        AL_DELETE(_terrain_pass);
    }
    void GpuTerrain::AddRenderPasses(Renderer &renderer, RenderingData &rendering_data)
    {
        renderer.EnqueuePass(_terrain_pass);
    }
#pragma endregion

#pragma region TerrainPass
    TerrainPass::TerrainPass() : RenderPass("TerrainPass")
    {
        _event = ERenderPassEvent::kBeforeSkybox;
        constexpr u16 kMaxNodeCount = 5*5+10*10+20*20+40*40+80*80+160*160;
        GPUBufferDesc desc;
        desc._is_random_write = true;
        desc._is_readable = false;
        desc._format = EALGFormat::kALGFormatR32G32_UINT;
        desc._element_num = kMaxNodeCount >> 1;
        desc._element_size = sizeof(Vector2UInt);
        desc._size = desc._element_size * desc._element_num;
        desc._target = EGPUBufferTarget::kAppend;
        _src_node_buf = GPUBuffer::Create(desc);
        _src_node_buf->Name("SrcNodes");
        for(u16 i = 0; i < 5; i++)
        {
            for(u16 j = 0; j < 5; j++)
                _lod5_nodes.emplace_back(i,j);
        }
        _temp_node_buf = GPUBuffer::Create(desc);
        _temp_node_buf->Name("TempNodes");
        desc._format = EALGFormat::kALGFormatR32G32B32_UINT;
        desc._element_size = sizeof(Vector3UInt);
        desc._size = desc._element_size * desc._element_num;
        _final_node_buf = GPUBuffer::Create(desc);
        _final_node_buf->Name("FinalNodes");
        _disp_args_buf = GPUBuffer::Create(EGPUBufferTarget::kIndirectArguments,sizeof(DispatchArguments),1u,"GpuTerrainGenArgsBuffer");
        DispatchArguments dispatch_arg = {1,1,1};
        _disp_args_buf->SetData((const u8*)&dispatch_arg,sizeof(DispatchArguments));
        _patches_buf = GPUBuffer::Create((EGPUBufferTarget)(EGPUBufferTarget::kAppend | EGPUBufferTarget::kStructured),12u,5000,"GpuTerrainPatchesBuffer");
        _draw_arg_buf = GPUBuffer::Create(EGPUBufferTarget::kIndirectArguments,sizeof(DrawIndexedArguments),1u,"GpuTerrainPatchArgsBuffer");
    }

    TerrainPass::~TerrainPass()
    {
        DESTORY_PTR(_src_node_buf);
        DESTORY_PTR(_temp_node_buf);
        DESTORY_PTR(_final_node_buf);
        DESTORY_PTR(_disp_args_buf);
        DESTORY_PTR(_patches_buf);
        DESTORY_PTR(_draw_arg_buf);
    }

    void TerrainPass::Setup(ComputeShader* terrain_gen,Mesh* plane,Material* terrain_mat)
    {
        _terrain_gen =  terrain_gen;
        _plane = plane;
        _terrain_mat = terrain_mat;
        DrawIndexedArguments draw_arg{plane->GetIndicesCount(),1u,0,0u,0u};
        _draw_arg_buf->SetData((const u8*)&draw_arg,sizeof(DrawIndexedArguments));
    }

    void TerrainPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get("Terrain");
        {
            PROFILE_BLOCK_GPU(cmd.get(), GpuTerrain);
            auto kernel = _terrain_gen->FindKernel("QuadTreeProcessor");
            GPUBuffer* src_buf = _src_node_buf;
            GPUBuffer* dst_buf = _temp_node_buf;
            //四叉树分割
            _terrain_gen->SetBuffer(kernel,"final_nodes",_final_node_buf);
            _terrain_gen->SetFloat("control_factor",1.0f);
            _terrain_gen->SetVector("camera_pos",rendering_data._camera->Position());
            for (i32 i = kMaxLOD; i >=0 ;i--)
            {
                _terrain_gen->SetBuffer(kernel,"src_nodes",src_buf);
                _terrain_gen->SetBuffer(kernel,"temp_nodes",dst_buf);
                _terrain_gen->SetInt("cur_lod",i);
                cmd->CopyCounterValue(src_buf,_disp_args_buf,0u);
                cmd->Dispatch(_terrain_gen,kernel,_disp_args_buf,0u);
                std::swap(src_buf,dst_buf);
            }
            //为每个节点生成patch
            //DispatchArguments dispatch_arg = {1,1,1};
            //_disp_args_buf->SetData((const u8*)&dispatch_arg, sizeof(DispatchArguments));
            kernel = _terrain_gen->FindKernel("GenPatches");
            cmd->CopyCounterValue(_final_node_buf,_disp_args_buf,0u);
            _terrain_gen->SetBuffer(kernel,"_input_final_nodes",_final_node_buf);
            _terrain_gen->SetBuffer(kernel,"_patch_list",_patches_buf);
            cmd->Dispatch(_terrain_gen,kernel,_disp_args_buf,0u);
            //绘制
            cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
            _terrain_mat->SetBuffer("PatchList",_patches_buf);
            cmd->CopyCounterValue(_patches_buf,_draw_arg_buf,DrawIndexedArguments::OffsetOfInstanceCount());
            cmd->DrawMeshIndirect(_plane,0u,_terrain_mat,0u,_draw_arg_buf);
            // _terrain_gen->SetBuffer("src_nodes",_temp_node_buf);
            // _terrain_gen->SetBuffer("temp_nodes",_src_node_buf);
            // cmd->Dispatch(_terrain_gen.get(),kernel,2u,1u,1u);
        }
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    void TerrainPass::BeginPass(GraphicsContext *context)
    {
        _src_node_buf->SetData(_lod5_nodes);
        _temp_node_buf->SetCounter(0u);
        _final_node_buf->SetCounter(0u);
        _patches_buf->SetCounter(0u);
    }
    void TerrainPass::EndPass(GraphicsContext *context)
    {

    }
#pragma endregion
}// namespace Ailu
