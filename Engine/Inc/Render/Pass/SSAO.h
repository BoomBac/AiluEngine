#pragma once
#ifndef __SSAO_H__
#define __SSAO_H__
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
	private:
	};
}

#endif // !SSAO_H__

