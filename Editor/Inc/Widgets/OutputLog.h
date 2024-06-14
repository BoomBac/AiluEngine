#pragma once
#ifndef __OUTPUT_LOG_H__
#define __OUTPUT_LOG_H__
#include "ImGuiWidget.h"
#include "Ext/imgui/imgui.h"
#include "Framework/Common/LogMgr.h"

namespace Ailu
{
	namespace Editor
	{
		class ImGuiLogAppender : public IAppender
		{
		public:
			ImGuiLogAppender()
			{
				AutoScroll = true;
				Clear();
			}

			void Print(std::string str) final
			{
				AddLog("%s\r\n", str.c_str());
			}
			void Print(std::wstring str) final
			{
				AddLog("%s\r\n", ToChar(str).c_str());
			}

			void Draw(const char* title)
			{
				// Options menu
				if (ImGui::BeginPopup("Options"))
				{
					ImGui::Checkbox("Auto-scroll", &AutoScroll);
					ImGui::EndPopup();
				}

				// Main window
				if (ImGui::Button("Options"))
					ImGui::OpenPopup("Options");
				ImGui::SameLine();
				bool clear = ImGui::Button("Clear");
				ImGui::SameLine();
				bool copy = ImGui::Button("Copy");
				ImGui::SameLine();
				Filter.Draw("Filter", -100.0f);

				ImGui::Separator();

				if (ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
				{
					if (clear)
						Clear();
					if (copy)
						ImGui::LogToClipboard();

					ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
					const char* buf = Buf.begin();
					const char* buf_end = Buf.end();
					if (Filter.IsActive())
					{
						// In this example we don't use the clipper when Filter is enabled.
						// This is because we don't have random access to the result of our filter.
						// A real application processing logs with ten of thousands of entries may want to store the result of
						// search/filter.. especially if the filtering function is not trivial (e.g. reg-exp).
						for (int line_no = 0; line_no < LineOffsets.Size; line_no++)
						{
							const char* line_start = buf + LineOffsets[line_no];
							const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
							if (Filter.PassFilter(line_start, line_end))
							{
								if (*line_start == *"E")
									ImGui::PushStyleColor(ImGuiCol_Text, _red_color);
								else if (*line_start == *"W")
									ImGui::PushStyleColor(ImGuiCol_Text, _yellow_color);
								else
									ImGui::PushStyleColor(ImGuiCol_Text, _white_color);
								ImGui::TextUnformatted(line_start, line_end);
								ImGui::PopStyleColor();
							}
						}
					}
					else
					{
						// The simplest and easy way to display the entire buffer:
						//   ImGui::TextUnformatted(buf_begin, buf_end);
						// And it'll just work. TextUnformatted() has specialization for large blob of text and will fast-forward
						// to skip non-visible lines. Here we instead demonstrate using the clipper to only process lines that are
						// within the visible area.
						// If you have tens of thousands of items and their processing cost is non-negligible, coarse clipping them
						// on your side is recommended. Using ImGuiListClipper requires
						// - A) random access into your data
						// - B) items all being the  same height,
						// both of which we can handle since we have an array pointing to the beginning of each line of text.
						// When using the filter (in the block of code above) we don't have random access into the data to display
						// anymore, which is why we don't use the clipper. Storing or skimming through the search result would make
						// it possible (and would be recommended if you want to search through tens of thousands of entries).
						ImGuiListClipper clipper;
						clipper.Begin(LineOffsets.Size);
						while (clipper.Step())
						{
							for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
							{
								const char* line_start = buf + LineOffsets[line_no];
								const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
								if (line_start != nullptr)
								{
									if (*line_start == *"E")
										ImGui::PushStyleColor(ImGuiCol_Text, _red_color);
									else if (*line_start == *"W")
										ImGui::PushStyleColor(ImGuiCol_Text, _yellow_color);
									else
										ImGui::PushStyleColor(ImGuiCol_Text, _white_color);
									ImGui::TextUnformatted(line_start, line_end);
									ImGui::PopStyleColor();
								}
							}
						}
						clipper.End();
					}
					ImGui::PopStyleVar();

					// Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
					// Using a scrollbar or mouse-wheel will take away from the bottom edge.
					if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
						ImGui::SetScrollHereY(1.0f);
				}
				ImGui::EndChild();
			}
		private:
			void    Clear()
			{
				Buf.clear();
				LineOffsets.clear();
				LineOffsets.push_back(0);
			}

			void    AddLog(const char* fmt, ...) IM_FMTARGS(2)
			{
				int old_size = Buf.size();
				va_list args;
				va_start(args, fmt);
				Buf.appendfv(fmt, args);
				va_end(args);
				for (int new_size = Buf.size(); old_size < new_size; old_size++)
					if (Buf[old_size] == '\n')
						LineOffsets.push_back(old_size + 1);
			}
		private:
			ImGuiTextBuffer     Buf;
			ImGuiTextFilter     Filter;
			ImVector<int>       LineOffsets; // Index to lines offset. We maintain this with AddLog() calls.
			bool                AutoScroll;  // Keep scrolling if already at the bottom.

			ImVec4 _red_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
			ImVec4 _yellow_color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
			ImVec4 _white_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		};

		//这个类是用来输出日志的，它是一个简单的文本输出窗口，它可以过滤文本，并且可以自动滚动。
		//它会被ImguiLyaer和Logmgr两个地方delete示例，会有问题，暂时没想好解决方案
		class OutputLog : public ImGuiWidget
		{
			// Usage:
			//  static ExampleAppLog my_log;
			//  my_log.AddLog("Hello %d world\n", 123);
			//  my_log.Draw("title");

		public:
			OutputLog(IAppender* imgui_logger);
			~OutputLog();
			void Open(const i32& handle) final;
			void Close(i32 handle) final;
		private:
			void ShowImpl() final;
		private:
			ImGuiLogAppender* _p_logger;
		};
	}
}
#endif // !__OUTPUT_LOG_H__
