#pragma once
#ifndef __SSAO_PASS_H__
#define __SSAO_PASS_H__
#include "Assets/AssetRef.h"
#include "RenderFeature.h"
#include "../Texture.h"
#include "generated/SSAO.gen.h"
namespace Ailu::Render
{
	class SSAOPass : public RenderPass
	{
	public:
		SSAOPass();
		~SSAOPass();
        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
		// x = 强度，y = 遮蔽半径（米，1.0 左右适合大范围 AO，0.2~0.5 适合接触阴影），z/w 未使用。
		Vector4f _ao_params = {1.25f,0.5f,0.f,0.f};
		bool _is_debug_mode = false;
		bool _is_half_res = true;
		bool _is_cbr = false;
	private:
        AssetRef<ComputeShader> _ssao_computer;
		Ref<Material> _ssao_gen;
        ComputeShaderKernelId _ssao_gen_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _ssao_blur_x_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _ssao_blur_y_kernel = kInvalidComputeShaderKernelId;
	};

	ACLASS()
	class SSAO : public RenderFeature
	{
    GENERATED_BODY()
    public:
		SSAO();
		~SSAO();
		void AddRenderPasses(Renderer &renderer, const RenderingData & rendering_data) override;
		APROPERTY()
		bool _is_cbr = false;
		APROPERTY()
		bool _is_half_res = true;
	private:
		SSAOPass _pass;
	};
}

#endif // !__SSAO_PASS_H__
