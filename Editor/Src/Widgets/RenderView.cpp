#include "Widgets/RenderView.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "Render/Camera.h"
#include "Render/RenderPipeline.h"

namespace Ailu
{
    Render::Renderer *s_renderer = nullptr;
    namespace Editor
    {
        RenderView::RenderView() : DockWindow("RenderView")
        {
            _vb = _content_root->AddChild<UI::VerticalBox>();
            _vb->SlotSizePolicy(UI::ESizePolicy::kFixed);
            _vb->SlotPadding(_content_root->Thickness());
            _source = _vb->AddChild<UI::Image>();
            _source->SlotSizePolicy(UI::ESizePolicy::kFill);
            _source->SlotAlignmentH(UI::EAlignment::kFill);
            _on_size_change += [this](Vector2f new_size)
            {
                auto t = _content_root->Thickness();
                _vb->SlotSize(new_size.x, new_size.y - kTitleBarHeight);
                if (Render::Camera::sCurrent)
                    Render::Camera::sCurrent->OutputSize((u16) (new_size.x - t.x - t.z), (u16) (new_size.y - kTitleBarHeight - t.y - t.w));
            };
            _on_size_change_delegate.Invoke(_size);
            s_renderer = Render::RenderPipeline::Get().GetRenderer();
        }
        void RenderView::Update(f32 dt)
        {
            DockWindow::Update(dt);
            _source->SetTexture(s_renderer->TargetTexture());
        }
        void RenderView::SetSource(Render::Texture *tex)
        {
            if (tex)
            {
                _source->SetTexture(tex);
            }
        }
    }
}