#ifndef __DOCK_MGR_H__
#define __DOCK_MGR_H__
#include "DockWindow.h"

namespace Ailu
{
    namespace UI
    {
        class UILayer;
    }
    namespace Editor
    {
        class DockManager
        {
        public:
            void AddDock(DockWindow *dock);
            void RemoveDock(DockWindow *dock);
            void Update(f32 dt);
        private:
            Vector<DockWindow *> _docks;
            UI::UILayer *_uiLayer = nullptr;
        };
    }
}// namespace Ailu

#endif// !__DOCK_MGR_H__
