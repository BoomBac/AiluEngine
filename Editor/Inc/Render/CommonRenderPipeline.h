#pragma once
#ifndef __COMMON_RENDER_PIPELINE_H__
#define __COMMON_RENDER_PIPELINE_H__
#include "Render/RenderPipeline.h"

namespace Ailu
{
	namespace Editor
	{
		class CommonRenderPipeline : public Ailu::RenderPipeline
		{
			DISALLOW_COPY_AND_ASSIGN(CommonRenderPipeline)
		public:
			CommonRenderPipeline();
		private:
			void BeforeReslove() final;
		};
	}
}

#endif // !COMMON_RENDER_PIPELINE_H__

