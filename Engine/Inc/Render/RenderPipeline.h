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
		void Setup();
		void Render();
		RenderTexture* CameraTarget(u16 index = 0) { return index < _cameras.size() ? _cameras[index]->TargetTexture() : nullptr; }
		Renderer* GetRenderer(u16 index = 0) { return index < _renderers.size()? _renderers[index].get() : nullptr; };
	private:
		void RenderSingleCamera(Camera& cam, Renderer& renderer);
		void FrameCleanUp();
	protected:
		virtual void BeforeReslove() {};
		Vector<Camera*> _cameras;
		Vector<Scope<Renderer>> _renderers;
	};
}

#endif // !RENDER_PIPELINE__
