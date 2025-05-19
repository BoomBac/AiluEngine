#include "./RenderFeature.h"
#include "generated/GpuTerrain.gen.h"
namespace Ailu::Render
{
    class TerrainPass;
    ACLASS()
    class GpuTerrain : public RenderFeature
    {
        GENERATED_BODY()
    public:
        GpuTerrain();
        ~GpuTerrain();
        void AddRenderPasses(Renderer &renderer, RenderingData &rendering_data) final;
    private:
        TerrainPass* _terrain_pass;
        Ref<Mesh> _plane;
        Ref<Material> _terrain_mat;
        Ref<ComputeShader> _terrain_gen;
    };

    class TerrainPass : public RenderPass
    {
    public:
        TerrainPass();
        ~TerrainPass() final;
        void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
        void BeginPass(GraphicsContext *context) final;
        void EndPass(GraphicsContext *context) final;
        void Setup(ComputeShader* terrain_gen,Mesh* plane,Material* terrain_mat);
    private:
        static const u16 kMaxLOD =5;
        ComputeShader* _terrain_gen;
        Mesh* _plane;
        Material* _terrain_mat;

        GPUBuffer* _src_node_buf;
        GPUBuffer* _temp_node_buf;
        GPUBuffer* _final_node_buf;
        GPUBuffer* _disp_args_buf;
        GPUBuffer* _patches_buf;
        GPUBuffer* _draw_arg_buf;
        Vector<Vector2UInt> _lod5_nodes;
    };
}