#pragma once
#ifndef __SSAO_PASS_H__
#define __SSAO_PASS_H__
#include "RenderPass.h"
#include "../Texture.h"
#include "generated./SSAO.gen.h"
namespace Ailu
{
	class SSAOPass : public RenderPass
	{
	public:
		SSAOPass();
		~SSAOPass();
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
		Vector4f _ao_params = {1.25f,125.f,0.f,0.f};
		bool _is_debug_mode = false;
		bool _is_half_res = true;
		bool _is_cbr = false;
	private:
		Ref<ComputeShader> _ssao_computer;
		Ref<Material> _ssao_gen;
	};

	ACLASS()
	class SSAO : public RenderFeature
	{
	GENERATED_BODY()
    public:
		SSAO();
		~SSAO();
		void AddRenderPasses(Renderer &renderer, RenderingData &rendering_data) override;
		APROPERTY()
		bool _is_cbr = false;
		APROPERTY()
		bool _is_half_res = true;
	private:
		SSAOPass _pass;
	};
}

#endif // !__SSAO_PASS_H__