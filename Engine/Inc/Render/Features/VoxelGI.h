//
// Created by 22292 on 2024/11/13.
//

#ifndef AILU_VOXELGI_H
#define AILU_VOXELGI_H

#include "../Buffer.h"
#include "RenderFeature.h"

namespace Ailu::Render
{
    class VoxelizePass : public RenderPass
    {
    public:
        VoxelizePass();
        ~VoxelizePass() final;
        void Setup(const VoxelGIData &data);
        void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
        void BeginPass(GraphicsContext *context) final;
        void EndPass(GraphicsContext *context) final;

    public:
        Ref<Texture3D> _voxel_tex = nullptr;

    private:
        u16 _voxel_pass_index = 0u;
        Ref<Shader> _standard_lit_forward;
        VoxelGIData _data;
        Vector3Int _pre_grid_num;
        ConstantBuffer *_cam_cbuf;
        Ref<ComputeShader> _voxelize_cs;
        GPUBuffer *_voxel_buf = nullptr;
    };

    class VoxelDebugPass : public RenderPass
    {
    public:
        VoxelDebugPass();
        ~VoxelDebugPass() final;
        void Setup(const VoxelGIData &data, Texture3D *voxel_tex);
        void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
        void BeginPass(GraphicsContext *context) final;
        void EndPass(GraphicsContext *context) final;

    private:
        Ref<Shader> _voxel_debug;
        Ref<Material> _voxel_debug_mat;
        VoxelGIData _data;
        Texture3D *_voxel_tex = nullptr;
    };

    class VoxelGI : public RenderFeature
    {
    public:
        VoxelGI();
        ~VoxelGI() final;
        void AddRenderPasses(Renderer &renderer, RenderingData &rendering_data) final;

    public:
        bool _is_draw_debug_voxel = false;
        VoxelGIData _voxelize_data;

    private:
        VoxelizePass _voxelize_pass;
        VoxelDebugPass _voxel_debug_pass;
    };
}// namespace Ailu


#endif//AILU_VOXELGI_H
