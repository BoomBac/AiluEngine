#include "Framework/Common/Application.h"
#include "Dock/DockManager.h"
#include "UI/UIRenderer.h"
#include "UI/UILayer.h"

namespace Ailu
{
    namespace Editor
    {
        void DockManager::AddDock(DockWindow *dock)
        {
            if (!_uiLayer)
            {
                for (auto layer: Application::Get().GetLayerStack())
                {
                    if (_uiLayer = dynamic_cast<UI::UILayer *>(layer); _uiLayer != nullptr)
                        break;
                }
            }
            if (auto it = std::find_if(_docks.begin(), _docks.end(), [&](DockWindow* d)->bool {return d == dock;}); it != _docks.end())
                return;
            _docks.push_back(dock);
            UI::UIRenderer::Get()->AddWidget(dock->_title_bar);
            UI::UIRenderer::Get()->AddWidget(dock->_content);
            _uiLayer->RegisterWidget(dock->_title_bar.get());
            _uiLayer->RegisterWidget(dock->_content.get());
        }
        void DockManager::RemoveDock(DockWindow *dock)
        {
            if (auto it = std::find_if(_docks.begin(), _docks.end(), [&](DockWindow* d)->bool {return d == dock;}); it != _docks.end())
            {
                UI::UIRenderer::Get()->RemoveWidget(dock->_title_bar.get());
                UI::UIRenderer::Get()->RemoveWidget(dock->_content.get());
                _uiLayer->UnRegisterWidget(dock->_title_bar.get());
                _uiLayer->UnRegisterWidget(dock->_content.get());
                _docks.erase(it);
            }
        }
        void DockManager::Update(f32 dt)
        {
            if (auto it = std::find_if(_docks.begin(), _docks.end(), [](DockWindow *d)
                                       { return d->_is_focused; });
                it != _docks.end())
            {
                std::swap(*it, _docks.back());
            }

            for (auto dock : _docks)
                dock->Update(dt);
        }
    }
}// namespace Ailu