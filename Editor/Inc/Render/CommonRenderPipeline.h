#pragma once
#ifndef __COMMON_RENDER_PIPELINE_H__
#define __COMMON_RENDER_PIPELINE_H__
#include <Render/RenderPipeline.h>

#include "PickPass.h"
namespace Ailu
{
	namespace Editor
	{
		class CommonRenderPipeline : public Ailu::RenderPipeline
		{
			DISALLOW_COPY_AND_ASSIGN(CommonRenderPipeline)
		public:
			CommonRenderPipeline();
            void Setup() final;

        public:
            PickFeature _pick;
		private:
			void BeforeReslove() final;

        private:
		};
	}
}

#endif // !COMMON_RENDER_PIPELINE_H__

