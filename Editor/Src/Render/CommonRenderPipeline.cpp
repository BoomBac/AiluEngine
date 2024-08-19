#include "Inc/Render/CommonRenderPipeline.h"
#include "Render/Pass/RenderPass.h"
namespace Ailu::Editor
{
	CommonRenderPipeline::CommonRenderPipeline()
	{
		//_renderers[0]->EnqueuePass(ERenderPassEvent::kAfterPostprocess, MakeScope<GizmoPass>());
	}
	void CommonRenderPipeline::BeforeReslove()
	{
		
	}
}
