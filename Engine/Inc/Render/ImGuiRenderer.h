#include <set>

namespace Ailu
{
    namespace Render
    {
        class RHICommandBuffer;
        class RenderTexture;
        class ImGuiRenderer
        {
        public:
            static void Init();
            static void Shutdown();
            static ImGuiRenderer &Get();
            ImGuiRenderer();
            ~ImGuiRenderer();
            void Render(RHICommandBuffer* cmd);
            void RecordImguiUsedTexture(RenderTexture *tex);
        private:
            // Add any necessary private members here, such as context or state
            std::set<RenderTexture *> _imgui_used_rt;
        };
    }
}