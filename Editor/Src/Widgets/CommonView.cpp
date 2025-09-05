#include "Widgets/CommonView.h"
#include "UI/Container.h"
#include "UI/Basic.h"

#include "Render/RenderPipeline.h"
#include "Render/Renderer.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Input.h"

namespace Ailu
{
    static Render::Renderer *s_cur_renderer = nullptr;//=
	namespace Editor
	{
        static Vector<UI::Text *> g_statics_texts;
		CommonView::CommonView() :DockWindow("CommonView")
		{
            _root = _content_root->AddChild<UI::ScrollView>();
            _root->SlotSizePolicy(UI::ESizePolicy::kFill);
            _vb = _root->AddChild<UI::VerticalBox>();
            _vb->SlotSizePolicy(UI::ESizePolicy::kAuto);
            _vb->AddChild<UI::Text>("Features");
            s_cur_renderer = Render::RenderPipeline::Get().GetRenderer();
            for (auto feature: s_cur_renderer->GetFeatures())
            {
                auto text = _vb->AddChild<UI::Text>(feature->Name());
                text->SlotMargin({5.0f, 0.0f, 0.0f, 0.0f});
            }
            g_statics_texts.push_back(_vb->AddChild<UI::Text>());
            g_statics_texts.push_back(_vb->AddChild<UI::Text>());
            _vb->AddChild<UI::Text>("Rendering Stats");
            g_statics_texts.push_back(_vb->AddChild<UI::Text>());
            g_statics_texts.push_back(_vb->AddChild<UI::Text>());
            g_statics_texts.push_back(_vb->AddChild<UI::Text>());
            g_statics_texts.push_back(_vb->AddChild<UI::Text>());
            g_statics_texts.push_back(_vb->AddChild<UI::Text>());
            g_statics_texts.push_back(_vb->AddChild<UI::Text>());
            g_statics_texts.push_back(_vb->AddChild<UI::Text>());
            for (auto text: g_statics_texts)
            {
                text->SlotAlignmentH(UI::EAlignment::kLeft).SlotMargin({1.0f, 1.0f, 1.0f, 1.0f});
            }
        }
        void CommonView::Update(f32 dt)
        {
            DockWindow::Update(dt);
            auto [wx, wy] = Application::Get().GetWindow().GetClientPosition();
            Vector2f window_pos = {(f32) wx, (f32) wy};
            g_statics_texts[0]->SetText(std::format("MousePos: {}", Input::GetMousePos(Application::FocusedWindow()).ToString()));
            g_statics_texts[1]->SetText(std::format("WinPos: {}", window_pos.ToString()));
            g_statics_texts[2]->SetText(std::format("FrameRate: {:.2f}", Render::RenderingStates::s_frame_rate));
            g_statics_texts[3]->SetText(std::format("FrameTime: {:.2f} ms", Render::RenderingStates::s_frame_time));
            g_statics_texts[4]->SetText(std::format("GpuLatency: {:.2f} ms", Render::RenderingStates::s_gpu_latency));
            g_statics_texts[5]->SetText(std::format("Draw Call: {} ", Render::RenderingStates::s_draw_call));
            g_statics_texts[6]->SetText(std::format("Dispatch Call: {} ", Render::RenderingStates::s_dispatch_call));
            g_statics_texts[7]->SetText(std::format("VertCount: {} ", Render::RenderingStates::s_vertex_num));
            g_statics_texts[8]->SetText(std::format("TriCount: {} ", Render::RenderingStates::s_triangle_num));
        }
	}
}