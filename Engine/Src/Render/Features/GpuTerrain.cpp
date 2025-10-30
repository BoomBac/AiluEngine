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
        _terrain_mat->SetTexture("_BaseColor", g_pResourceMgr->Get<Texture2D>(L"Textures/TerrainDiffuse"));
        _terrain_mat->SetTexture("_HeightMap", g_pResourceMgr->Get<Texture2D>(L"Textures/TerrainHeight"));
        _terrain_pass->Setup(_terrain_gen.get(),_plane.get(),_terrain_mat.get(),&_max_height);
        _terrain_mat->EnableKeyword("DEBUG_LOD");
    }
    GpuTerrain::~GpuTerrain()
    {
        AL_DELETE(_terrain_pass);
    }

    void Ailu::Render::GpuTerrain::AddRenderPasses(Renderer &renderer, const RenderingData & rendering_data)
    {
        _terrain_pass->_cam = Camera::sSelected ? Camera::sSelected: Camera::sCurrent;
        _terrain_pass->_is_debug = _debug;
        _terrain_mat->SetVector("_params", Vector4f(_max_height, 0.0f, _world_size, _world_size));
        if (_debug)
            _terrain_mat->EnableKeyword("DEBUG_LOD");
        else
            _terrain_mat->DisableKeyword("DEBUG_LOD");
        if (_enable_cull)
            _terrain_gen->EnableKeyword("_ENABLE_CULL");
        else
            _terrain_gen->DisableKeyword("_ENABLE_CULL");
        renderer.EnqueuePass(_terrain_pass);
    }
#pragma endregion

#pragma region TerrainPass
    TerrainPass::TerrainPass() : RenderPass("TerrainPass")
    {
        _event = ERenderPassEvent::kBeforeSkybox;
        constexpr u16 kMaxNodeCount = 5*5+10*10+20*20+40*40+80*80+160*160;
        BufferDesc desc;
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
        _patches_buf = GPUBuffer::Create((EGPUBufferTarget)(EGPUBufferTarget::kAppend | EGPUBufferTarget::kStructured),32u,8000,"GpuTerrainPatchesBuffer");
        _draw_arg_buf = GPUBuffer::Create(EGPUBufferTarget::kIndirectArguments,sizeof(DrawIndexedArguments),1u,"GpuTerrainPatchArgsBuffer");
        _minmax_height = Texture2D::Create(1280u, 1280u, ETextureFormat::kRGFloat, true, true);
        _minmax_height->Name("MinMaxHeight");
        _minmax_height->Apply();
        _lod_map = Texture2D::Create(160u, 160u, ETextureFormat::kR8UInt, false, true);
        _lod_map->Name("LODMap");
        _lod_map->Apply();
    }

    TerrainPass::~TerrainPass()
    {

    }

    void TerrainPass::Setup(ComputeShader *terrain_gen, Mesh *plane, Material *terrain_mat, f32* max_height)
    {
        _max_height = max_height;
        _terrain_gen =  terrain_gen;
        _plane = plane;
        _terrain_mat = terrain_mat;
        DrawIndexedArguments draw_arg{(u32)plane->GetIndicesCount(),1u,0,0u,0u};
        _draw_arg_buf->SetData((const u8*)&draw_arg,sizeof(DrawIndexedArguments));
        
        auto cmd = CommandBufferPool::Get("TerrainPassInit");
        auto kernel = _terrain_gen->FindKernel("MinMaxHeightGen");
        for (i32 i = 0; i < 9; i++)
        {
            _terrain_gen->SetInt("lod", i);
            if (i == 0)
            {
                _terrain_gen->SetTexture("HeightMap", g_pResourceMgr->Get<Texture2D>(L"Textures/TerrainHeight"));
                _terrain_gen->SetTexture("MinMaxMap", _minmax_height.get(), 0);
            }
            else
            {
                _terrain_gen->SetTexture("HeightMap", _minmax_height.get(), i - 1);
                _terrain_gen->SetTexture("MinMaxMap", _minmax_height.get(), i);
            }
            auto [x, y, z] = _terrain_gen->CalculateDispatchNum(kernel, 1280u >> i, 1280u >> i, 1u);
            cmd->Dispatch(_terrain_gen, kernel, x, y);
        }
        GraphicsContext::Get().ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
        
    }

    void TerrainPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get("Terrain");
        {
            PROFILE_BLOCK_GPU(cmd.get(), GpuTerrain);
            auto kernel = _terrain_gen->FindKernel("QuadTreeProcessor");
            GPUBuffer* src_buf = _src_node_buf.get();
            GPUBuffer *dst_buf = _temp_node_buf.get();
            //四叉树分割
            _terrain_gen->SetBuffer(kernel, "final_nodes", _final_node_buf.get());
            _terrain_gen->SetFloat("control_factor",1.0f);
            //_terrain_gen->SetVector("camera_pos",rendering_data._camera->Position());
            _terrain_gen->SetFloat("max_height",*_max_height);
            _terrain_gen->SetTexture("_MinMaxMap", _minmax_height.get());
            _terrain_gen->SetVector("camera_pos",_cam->Position());
            for (i32 i = kMaxLOD; i >=0 ;i--)
            {
                _terrain_gen->SetBuffer(kernel,"src_nodes",src_buf);
                _terrain_gen->SetBuffer(kernel,"temp_nodes",dst_buf);
                _terrain_gen->SetInt("cur_lod",i);
                cmd->CopyCounterValue(src_buf, _disp_args_buf.get(), 0u);
                cmd->Dispatch(_terrain_gen, kernel, _disp_args_buf.get(), 0u);
                std::swap(src_buf,dst_buf);
            }
            //lod map
            cmd->CopyCounterValue(_final_node_buf.get(), _disp_args_buf.get(), 0u);
            kernel = _terrain_gen->FindKernel("GenLODMap");
            _terrain_gen->SetBuffer(kernel, "_input_final_nodes", _final_node_buf.get());
            _terrain_gen->SetTexture("_LODMap",_lod_map.get());
            cmd->Dispatch(_terrain_gen, kernel, _disp_args_buf.get(), 0u);
            //patch gen
            const auto& vf = _cam->GetViewFrustum()._planes;
            for(u16 i = 0; i < 6; i++)
                _frustum[i] = Vector4f(vf[i]._normal,vf[i]._distance);
            _terrain_gen->SetVectorArray("_frustum",_frustum.data(),6u);
            kernel = _terrain_gen->FindKernel("GenPatches");
            _terrain_gen->SetBuffer(kernel, "_input_final_nodes", _final_node_buf.get());
            _terrain_gen->SetBuffer(kernel, "_patch_list", _patches_buf.get());
            _terrain_gen->SetTexture("_InputLODMap",_lod_map.get());
            cmd->Dispatch(_terrain_gen, kernel, _disp_args_buf.get(), 0u);
            //draw
            cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
            _terrain_mat->SetBuffer("PatchList", _patches_buf.get());
            cmd->CopyCounterValue(_patches_buf.get(), _draw_arg_buf.get(), offsetof(DrawIndexedArguments, DrawIndexedArguments::_instance_count));
            cmd->DrawMeshIndirect(_plane, 0u, _terrain_mat, 0u, _draw_arg_buf.get());
            cmd->ReadbackBuffer(_patches_buf.get(), true, 4u, [](const u8 *data, u32 size)
                                {
                static u32 s_patch_num = 0u;
                u32 patch_num = *(u32*)data;
                if (s_patch_num != patch_num)
                    LOG_INFO("{} patch after cull",patch_num)
                s_patch_num = patch_num;
            });
            if (_is_debug)
            {
                cmd->DrawMeshIndirect(Mesh::s_cube.lock().get(), 0u, _terrain_mat, 1u, _draw_arg_buf.get());
            }
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