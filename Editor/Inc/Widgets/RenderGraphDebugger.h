#ifndef __RENDERGRAPH_DEBUGGER_H__
#define __RENDERGRAPH_DEBUGGER_H__
#include "ImGuiWidget.h"
namespace Ailu
{
    namespace Editor
    {
        class RenderGraphDebugger : public ImGuiWidget
        {
        public:
            RenderGraphDebugger();
            ~RenderGraphDebugger();
            void Open(const i32 &handle) final;
            void Close(i32 handle) final;
            void OnEvent(Event &e) final;

        private:
            void ShowImpl() final;

        private:

        };
    }
}// namespace Ailu
#endif// !__RENDERGRAPH_DEBUGGER_H__

