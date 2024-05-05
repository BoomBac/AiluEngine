#include "pch.h"
#include "Framework/ImGui/Widgets/OutputLog.h"


namespace Ailu
{
	OutputLog::OutputLog(IAppender* imgui_logger) : ImGuiWidget("OutputLog")
	{
		_p_logger = static_cast<ImGuiLogAppender*>(imgui_logger);
	}
	OutputLog::~OutputLog()
	{
	}
	void OutputLog::Open(const i32& handle)
	{
		ImGuiWidget::Open(handle);
	}
	void OutputLog::Close(i32 handle)
	{
		ImGuiWidget::Close(handle);
	}


	void OutputLog::ShowImpl()
	{
		_p_logger->Draw("OutputLog");
	}
}
