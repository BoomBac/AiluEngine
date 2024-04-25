#include "pch.h"
#include "Framework/ImGui/Widgets/OutputLog.h"


namespace Ailu
{
	OutputLog::OutputLog() : ImGuiWidget("OutputLog")
	{
        g_pLogMgr->AddAppender(this);
	}
	OutputLog::~OutputLog()
	{
	}
	void OutputLog::Open(const i32& handle)
	{
		ImGuiWidget::Open(handle);
	}
	void OutputLog::Close()
	{
		ImGuiWidget::Close();
	}
    void OutputLog::Print(std::string str)
    {
        _logger.AddLog("%s\r\n",str.c_str());
    }
    void OutputLog::Print(std::wstring str)
    {
        _logger.AddLog("%s\r\n",ToChar(str).c_str());
    }

	void OutputLog::ShowImpl()
	{
        _logger.Draw("OutputLog");
	}
}
