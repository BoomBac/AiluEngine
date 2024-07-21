#include "pch.h"
#include "Render/RenderPipeline.h"
#include "Render/CommandBuffer.h"
#include "Framework/Common/Profiler.h"

namespace Ailu
{
	RenderPipeline::RenderPipeline()
	{
		Init();
	}
	void RenderPipeline::Init()
	{
		_renderers.push_back(MakeScope<Renderer>());
		_cameras.emplace_back(Camera::sCurrent);
	}
	void RenderPipeline::Destory()
	{
	}
	void RenderPipeline::Setup()
	{

	}
	void RenderPipeline::Render()
	{
		Setup();
		for (auto cam : _cameras)
		{
			RenderSingleCamera(*cam, *_renderers[0].get());
		}
		FrameCleanUp();
	}
	void RenderPipeline::RenderSingleCamera(Camera& cam, Renderer& renderer)
	{
		renderer.Render(cam,*g_pSceneMgr->_p_current);
		cam.SetRenderer(&renderer);
	}
	void RenderPipeline::FrameCleanUp()
	{
		RenderTexture::ResetRenderTarget();
	}
}
