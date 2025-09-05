#include "Ext/imgui/backends/imgui_impl_dx12.h"
#include "Render/ImGuiRenderer.h"
#include "Render/RenderConstants.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/DescriptorManager.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "Framework/Common/Allocator.hpp"

namespace Ailu
{
    namespace Render
    {
        static RHI::DX12::D3DDescriptorMgr *g_descriptor_mgr = nullptr;
        static ImGuiRenderer *s_instance = nullptr;
        void ImGuiRenderer::Init()
        {
            AL_ASSERT(s_instance == nullptr);
            s_instance = AL_NEW(ImGuiRenderer);
        }
        void ImGuiRenderer::Shutdown()
        {
            AL_DELETE(s_instance);
        }
        ImGuiRenderer &ImGuiRenderer::Get()
        {
            return *s_instance;
        }
        ImGuiRenderer::ImGuiRenderer()
        {
#if defined(DEAR_IMGUI)
            g_descriptor_mgr = &RHI::DX12::D3DDescriptorMgr::Get();
            auto bind_heap = g_descriptor_mgr->GetBindHeap();
            auto [cpu_handle, gpu_handle] = std::make_tuple(bind_heap->GetCPUDescriptorHandleForHeapStart(), bind_heap->GetGPUDescriptorHandleForHeapStart());
            auto &d3d_ctx = static_cast<RHI::DX12::D3DContext &>(GraphicsContext::Get());
            auto backbuffer_format = RenderConstants::kColorRange == EColorRange::kLDR ? ConvertToDXGIFormat(RenderConstants::kLDRFormat) : ConvertToDXGIFormat(RenderConstants::kHDRFormat);
            auto ret = ImGui_ImplDX12_Init(d3d_ctx.GetDevice(), RenderConstants::kFrameCount, backbuffer_format, bind_heap, cpu_handle, gpu_handle);
#endif
        }
        ImGuiRenderer::~ImGuiRenderer()
        {
        }

        void ImGuiRenderer::Render(RHICommandBuffer *cmd)
        {
#if !defined(DEAR_IMGUI)
            return;
#endif
            auto d3dcmd = static_cast<RHI::DX12::D3DCommandBuffer *>(cmd);
            auto dxcmd = d3dcmd->NativeCmdList();
            auto bind_heap = g_descriptor_mgr->GetBindHeap();
            dxcmd->SetDescriptorHeaps(1u, &bind_heap);
            for (auto &it: _imgui_used_rt)
                it->StateTranslation(cmd, EResourceState::kPixelShaderResource, kTotalSubRes);
            _imgui_used_rt.clear();

            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dxcmd);

            ImGuiIO &io = ImGui::GetIO();
            // Update and Render additional Platform Windows
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault(nullptr, (void *) dxcmd);
            }
        }
        void ImGuiRenderer::RecordImguiUsedTexture(RenderTexture *tex)
        {
#if !defined(DEAR_IMGUI)
            return;
#endif
            if (tex && std::find(_imgui_used_rt.begin(), _imgui_used_rt.end(), tex) == _imgui_used_rt.end())
            {
                _imgui_used_rt.insert(tex);
            }
        }
    }// namespace Render
}// namespace Ailu