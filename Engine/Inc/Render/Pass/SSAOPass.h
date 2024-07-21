#pragma once
#ifndef __SSAO_PASS_H__
#define __SSAO_PASS_H__
#include "RenderPass.h"
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
		Vector4f _ao_params = {1.25f,500.f,0.f,0.f};
		bool _is_debug_mode = false;
	private:
		Ref<ComputeShader> _ssao_computer;
		Ref<Material> _ssao_gen;
		Ref<Texture2D> _ssao_result;
	};
}

#endif // !__SSAO_PASS_H__