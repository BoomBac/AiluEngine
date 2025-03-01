//
// Created by 22292 on 2024/11/13.
//

#include "Inc/Render/Pass/VoxelGI.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Inc/Render/CommandBuffer.h"
#include "Inc/Render/Renderer.h"

namespace Ailu
{
    //---------------------------------------------------------------------------VoxelizePass-----------------------------------------------------------------------------
    VoxelizePass::VoxelizePass() : RenderPass("VoxelizePass")
    {
        _standard_lit_forward = Shader::s_p_defered_standart_lit.lock();
        _voxel_pass_index = _standard_lit_forward->FindPass("VoxelLit");
        _cam_cbuf = IConstantBuffer::Create(sizeof(CBufferPerCameraData), false, "VoxelCbuf");
        _voxelize_cs = g_pResourceMgr->GetRef<ComputeShader>(L"Shaders/voxelize.alasset");
    }
    VoxelizePass::~VoxelizePass()
    {
        DESTORY_PTR(_voxel_buf);
        DESTORY_PTR(_cam_cbuf);
    }
    void VoxelizePass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get("Voxelize");
        {
            auto *cbuf_cam = IConstantBuffer::As<CBufferPerCameraData>(_cam_cbuf);
            Vector3f cam_pos = rendering_data._vxgi_data._center;
            cam_pos.z -= rendering_data._vxgi_data._size.z * 0.5f;
            BuildOrthographicMatrix(cbuf_cam->_MatrixP, -rendering_data._vxgi_data._size.x * 0.5f, rendering_data._vxgi_data._size.x * 0.5f,
                                    rendering_data._vxgi_data._size.y * 0.5f, -rendering_data._vxgi_data._size.y, 0.01f, rendering_data._vxgi_data._size.z);
            BuildViewMatrixLookToLH(cbuf_cam->_MatrixV, cam_pos, Vector3f::kForward, Vector3f::kUp);
            cbuf_cam->_MatrixVP = cbuf_cam->_MatrixV * cbuf_cam->_MatrixP;
            auto color = cmd->GetTempRT(_data._grid_num.x, _data._grid_num.y, "VoxelColorRT", ERenderTargetFormat::kDefault, false, false, false);
            {
                PROFILE_BLOCK_GPU(cmd.get(), Voxelize)
                cmd->SetRenderTarget(color);
                //voxelize
                for (const auto &it: *rendering_data._cull_results)
                {
                    auto &[queue, obj_list] = it;
                    if (queue < Shader::kRenderQueueTransparent)
                    {
                        for (auto &obj: obj_list)
                        {
                            cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, _cam_cbuf);
                            cmd->SetGlobalBuffer("g_voxel_data_block", _voxel_buf);
                            cmd->DrawRenderer(obj._mesh, obj._material, (*rendering_data._p_per_object_cbuf)[obj._scene_id], obj._submesh_index,
                                              _voxel_pass_index, 1u);
                        }
                    }
                }
            }

            Vector4f grid_num = Vector4f((f32) _data._grid_num.x, (f32) _data._grid_num.y, (f32) _data._grid_num.z, 1.f);
            _voxelize_cs->SetVector("_grid_num", grid_num);
            //write to texture3d
            _voxelize_cs->SetTexture("_VoxelTex", _voxel_tex.get());
            _voxelize_cs->SetBuffer("_VoxelBuffer", _voxel_buf);
            auto kernel = _voxelize_cs->FindKernel("FillTexture3D");
            u16 x, y, z;
            _voxelize_cs->GetThreadNum(kernel, x, y, z);
            {
                PROFILE_BLOCK_GPU(cmd.get(), FillTexture3D)
                cmd->Dispatch(_voxelize_cs.get(), kernel, std::ceilf(_data._grid_num.x / (f32) x), std::ceilf(_data._grid_num.y / (f32) y), std::ceilf(_data._grid_num.z / (f32) z));
            }
            //release res
            cmd->ReleaseTempRT(color);
        }
        context->ExecuteCommandBuffer(cmd);
        //filter
        _voxel_tex->GenerateMipmap();
        Shader::SetGlobalTexture("_VoxelLightTex", _voxel_tex.get());
        CommandBufferPool::Release(cmd);
    }
    void VoxelizePass::BeginPass(GraphicsContext *context)
    {
        if (_pre_grid_num != _data._grid_num)
        {
            DESTORY_PTR(_voxel_buf);
            GPUBufferDesc desc;
            desc._is_random_write = true;
            desc._is_readable = false;
            desc._format = EALGFormat::kALGFormatR32_UINT;
            desc._element_num = _data._grid_num.x * _data._grid_num.y * _data._grid_num.z;
            desc._size = sizeof(Vector4f) * desc._element_num;
            _voxel_buf = IGPUBuffer::Create(desc, "VoexlBuffer");
            if (_voxel_tex)
                _voxel_tex.reset();
            Texture3DInitializer initializer;
            initializer._width = _data._grid_num.x;
            initializer._height = _data._grid_num.y;
            initializer._depth = _data._grid_num.z;
            initializer._format = ETextureFormat::kRGBAFloat;
            initializer._is_random_access = true;
            _voxel_tex = Texture3D::Create(initializer);
            _voxel_tex->Name("VoxelTex");
            _voxel_tex->Apply();
            for (u16 i = 0; i < _voxel_tex->MipmapLevel(); i++)
            {
                _voxel_tex->CreateView(Texture::ETextureViewType::kSRV, i, -1);
                _voxel_tex->CreateView(Texture::ETextureViewType::kUAV, i, -1);
            }
        }
    }
    void VoxelizePass::EndPass(GraphicsContext *context)
    {
        _pre_grid_num = _data._grid_num;
    }
    void VoxelizePass::Setup(const VoxelGIData &data)
    {
        _data = data;
    }

    //---------------------------------------------------------------------------VoxelizePass-------------------------------------------------------------------------------
    //---------------------------------------------------------------------------VoxelDebugPass-----------------------------------------------------------------------------
    VoxelDebugPass::VoxelDebugPass() : RenderPass("VoxelDebugPass")
    {
        _voxel_debug = g_pResourceMgr->GetRef<Shader>(L"Shaders/voxel_drawer.alasset");
        _voxel_debug_mat = MakeRef<Material>(_voxel_debug.get(), "Runtime/VoxelDrawer");
        _data._grid_num = {32, 32, 1};
        _data._grid_size = Vector3f::kOne;
        _voxel_debug_mat->SetVector("_GridNum", Vector4Int(_data._grid_num.x, _data._grid_num.y, _data._grid_num.z, 1));
        _voxel_debug_mat->SetVector("_GridSize", Vector4f::kOne * 2.1f);

        //_event = (ERenderPassEvent::ERenderPassEvent)(ERenderPassEvent::kAfterTransparent - 1);//before copy color
        _event = ERenderPassEvent::kAfterPostprocess;
    }
    VoxelDebugPass::~VoxelDebugPass()
    {
    }
    void VoxelDebugPass::Setup(const VoxelGIData &data, Texture3D *voxel_tex)
    {
        _data = data;
        _voxel_tex = voxel_tex;
    }
    void VoxelDebugPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get("VoxelDebug");
        {
            PROFILE_BLOCK_GPU(cmd.get(), VoxelDebug)
            cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
            u16 mip = rendering_data._vxgi_debug_mipmap;
            i32 mip_grid_num_x = _data._grid_num.x >> mip;
            i32 mip_grid_num_y = _data._grid_num.y >> mip;
            i32 mip_grid_num_z = _data._grid_num.z >> mip;
            _voxel_debug_mat->SetVector("_GridCenter", _data._center);
            _voxel_debug_mat->SetVector("_GridNum", Vector4Int(mip_grid_num_x, mip_grid_num_y, mip_grid_num_z, 1));
            _voxel_debug_mat->SetVector("_GridSize", _data._grid_size * powf(2.f, (f32) mip));
            _voxel_debug_mat->SetTexture("_VoxelSrc", _voxel_tex);
            _voxel_debug_mat->SetUint("_DebugMip", rendering_data._vxgi_debug_mipmap);
            auto world_matrix = BuildIdentityMatrix();//MatrixScale(_data._grid_size / 2.0f);
            u32 inst_count = mip_grid_num_x * mip_grid_num_y * mip_grid_num_z;
            cmd->DrawRenderer(Mesh::s_p_cube.lock().get(), _voxel_debug_mat.get(), world_matrix, 0u, 0u, inst_count);
        }
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    void VoxelDebugPass::BeginPass(GraphicsContext *context)
    {
        RenderPass::BeginPass(context);
    }
    void VoxelDebugPass::EndPass(GraphicsContext *context)
    {
        RenderPass::EndPass(context);
    }
    //---------------------------------------------------------------------------VoxelDebugPass-----------------------------------------------------------------------------

    //---------------------------------------------------------------------------VoxelGI------------------------------------------------------------------------------------
    VoxelGI::VoxelGI() : RenderFeature("VoxelGI")
    {
        _voxelize_data._grid_num = {32, 32, 32};
    }
    VoxelGI::~VoxelGI()
    {
    }
    void VoxelGI::AddRenderPasses(Renderer &renderer, RenderingData &rendering_data)
    {
        _voxelize_pass.Setup(rendering_data._vxgi_data);
        renderer.EnqueuePass(&_voxelize_pass);
        if (_is_draw_debug_voxel)
        {
            _voxel_debug_pass.Setup(rendering_data._vxgi_data, _voxelize_pass._voxel_tex.get());
            renderer.EnqueuePass(&_voxel_debug_pass);
        }
    }

    //---------------------------------------------------------------------------VoxelGI-----------------------------------------------------------------------------
}// namespace Ailu