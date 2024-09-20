#pragma once
#ifndef __RENDER_PIPELINE__
#define __RENDER_PIPELINE__
#include "Renderer.h"
#include "Camera.h"

namespace Ailu
{
	class AILU_API RenderPipeline
	{
		DISALLOW_COPY_AND_ASSIGN(RenderPipeline)
	public:
		using RenderEvent = std::function<void>();
		RenderPipeline();
		void Init();
		void Destory();
		virtual void Setup();
		void Render();
		Renderer* GetRenderer(u16 index = 0) { return index < _renderers.size()? _renderers[index].get() : nullptr; };
		RenderTexture* GetTarget(u16 index = 0);
	private:
		void RenderSingleCamera(const Camera& cam, Renderer& renderer);
		void FrameCleanUp();
	protected:
		virtual void BeforeReslove() {};
		Vector<Camera*> _cameras;
		Vector<Scope<Renderer>> _renderers;
		Vector<RenderTexture*> _targets;
	};
}

#endif // !RENDER_PIPELINE__
