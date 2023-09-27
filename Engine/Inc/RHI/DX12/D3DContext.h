#pragma once
#ifndef __D3D_CONTEXT_H__
#define __D3D_CONTEXT_H__

#include <d3dx12.h>
#include <dxgi1_6.h>

#include "D3DConstants.h"
#include "Render/Camera.h"
#include "Render/GraphicsContext.h"
#include "Platform/WinWindow.h"
#include "Framework/Math/ALMath.hpp"
#include "Render/Buffer.h"
#include "Render/Shader.h"
#include "Render/Material.h"
#include "Framework/Assets/Mesh.h"
#include "D3DCommandBuffer.h"

#include "Objects/SceneActor.h"

using Microsoft::WRL::ComPtr;
namespace Ailu
{
    struct SimpleVertex
    {
        Vector3f position;
        Vector4f color;
    };

    class D3DContext : public GraphicsContext
    {
        friend class D3DRendererAPI;
        friend class D3DComandBuffer;
    public:
        D3DContext(WinWindow* window);
        ~D3DContext();
        void Init() override;
        void Present() override;
        void Clear(Vector4f color, float depth, bool clear_color, bool clear_depth);
        inline static D3DContext* GetInstance() { return s_p_d3dcontext; };

        ID3D12Device* GetDevice();
        ID3D12GraphicsCommandList* GetCmdList();
        ComPtr<ID3D12DescriptorHeap> GetDescriptorHeap();
        D3D12_CPU_DESCRIPTOR_HANDLE& GetSRVCPUDescriptorHandle(uint32_t index);
        D3D12_GPU_DESCRIPTOR_HANDLE& GetSRVGPUDescriptorHandle(uint32_t index);
        const D3D12_CONSTANT_BUFFER_VIEW_DESC& GetCBufferViewDesc(uint32_t index) const;
        uint8_t* GetCBufferPtr();
        void ExecuteCommandBuffer(Ref<D3DComandBuffer> cmd);

        void DrawIndexedInstanced(uint32_t index_count, uint32_t instance_count, const Matrix4x4f& transform);
        void DrawInstanced(uint32_t vertex_count, uint32_t instance_count, const Matrix4x4f& transform);
        void DrawInstanced(uint32_t vertex_count, uint32_t instance_count);

    private:
        void Destroy();
        void LoadPipeline();
        void LoadAssets();
        void PopulateCommandList();
        void WaitForGpu();
        void MoveToNextFrame();
        void InitCBVSRVUAVDescHeap();
        void CreateDepthStencilTarget();
        void CreateDescriptorHeap();
        D3D12_GPU_DESCRIPTOR_HANDLE GetCBVGPUDescHandle(uint32_t index) const;


    private:
        inline static D3DContext* s_p_d3dcontext = nullptr;
        WinWindow* _window;

        // Pipeline objects.
        ComPtr<IDXGISwapChain3> m_swapChain;
        ComPtr<ID3D12Device> m_device;
        ComPtr<ID3D12Resource> _color_buffer[D3DConstants::kFrameCount];
        ComPtr<ID3D12Resource> _depth_buffer[D3DConstants::kFrameCount];
        ComPtr<ID3D12CommandAllocator> m_commandAllocators[D3DConstants::kFrameCount];
        ComPtr<ID3D12CommandQueue> m_commandQueue;
        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_pipelineState;
        ComPtr<ID3D12GraphicsCommandList> m_commandList;
        ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
        ComPtr<ID3D12DescriptorHeap> m_cbvHeap;
        ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
        uint8_t m_rtvDescriptorSize;
        uint8_t _dsv_desc_size;
        uint8_t _cbv_desc_size;
        uint8_t* _p_cbuffer = nullptr;

        // App resources.
        ComPtr<ID3D12Resource> m_vertexBuffer;
        D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

        Ref<Mesh> _tree;
        Ref<Material> _mat_standard;
        Ref<Material> _mat_wireframe;

        Ref<SceneActor> _p_actor;
        Ref<SceneActor> _p_light;


        uint32_t _render_object_index = 0u;

        ComPtr<ID3D12Resource> m_constantBuffer;
        ScenePerFrameData _perframe_scene_data;
        std::vector<D3D12_CONSTANT_BUFFER_VIEW_DESC> _cbuf_views;

        // Synchronization objects.
        uint8_t m_frameIndex;
        HANDLE m_fenceEvent;
        ComPtr<ID3D12Fence> m_fence;
        uint64_t m_fenceValues[D3DConstants::kFrameCount];


        uint32_t _width;
        uint32_t _height;
        float m_aspectRatio;

        std::unique_ptr<Camera> _p_scene_camera;
    };

    class Gizmo
    {
    public:
        static void Init()
        {
            p_buf = DynamicVertexBuffer::Create();
            s_color.a = 0.75f;
        }

        static void DrawLine(const Vector3f& from, const Vector3f& to, Color color = Gizmo::s_color)
        {
            DrawLine(from,to, color, color);
        }

        static void DrawLine(const Vector3f& from, const Vector3f& to, const Color& color_from, const Color& color_to)
        {
            static float vbuf[6];
            static float cbuf[8];
            vbuf[0] = from.x;
            vbuf[1] = from.y;
            vbuf[2] = from.z;
            vbuf[3] = to.x;
            vbuf[4] = to.y;
            vbuf[5] = to.z;
            memcpy(cbuf, color_from.data, 16);
            memcpy(cbuf + 4, color_to.data, 16);
            cbuf[3] *= s_color.a;
            cbuf[7] *= s_color.a;
            p_buf->AppendData(vbuf, 6, cbuf, 8);
            _vertex_num += 2;
        }

        static void DrawGrid(const int& grid_size, const int& grid_spacing, const Vector3f& center, Color color)
        {
            float halfWidth = static_cast<float>(grid_size * grid_spacing) * 0.5f;
            float halfHeight = static_cast<float>(grid_size * grid_spacing) * 0.5f;
            static Color lineColor = color;
            
            for (int i = -grid_size / 2; i <= grid_size / 2; ++i)
            {
                float xPos = static_cast<float>(i * grid_spacing) + center.x;
                lineColor.a = color.a * s_color.a;
                lineColor.a *= lerpf(1.0f, 0.0f, abs(xPos - center.x) / halfWidth);
                auto color_start = lineColor;
                auto color_end = lineColor;
                if (DotProduct(Camera::sCurrent->GetForward(), { 0,0,1 }) > 0)
                {
                    color_start.a *= 0.8f;
                    color_end.a *= 0.2f;
                }
                else
                {
                    color_start.a *= 0.2f;
                    color_end.a *= 0.8f;
                }
                Gizmo::DrawLine(Vector3f(xPos, center.y, -halfHeight + center.z),
                    Vector3f(xPos, center.y, halfHeight + center.z), color_start, color_end);

                float zPos = static_cast<float>(i * grid_spacing) + center.z;
                lineColor.a = color.a * s_color.a;
                lineColor.a *= lerpf(1.0f, 0.0f, abs(zPos - center.z) / halfWidth);
                color_start = lineColor;
                color_end = lineColor;
                auto right = Camera::sCurrent->GetRight();
                if (DotProduct(Camera::sCurrent->GetForward(), { 1,0,0 }) > 0)
                {
                    color_start.a *= 0.8f;
                    color_end.a *= 0.2f;
                }
                else
                {
                    color_start.a *= 0.2f;
                    color_end.a *= 0.8f;
                }
                Gizmo::DrawLine(Vector3f(-halfWidth + center.x, center.y, zPos),
                    Vector3f(halfWidth + center.x, center.y, zPos), color_start, color_end);             
            }
        }

        static void DrawCube(const Vector3f& center, const Vector3f& size, Vector4f color = Colors::kGray)
        {
            static float vbuf[72];
            static float cbuf[96];
            float halfX = size.x * 0.5f;
            float halfY = size.y * 0.5f;
            float halfZ = size.z * 0.5f;
            float verticesData[72] = {
                center.x - halfX, center.y - halfY, center.z + halfZ,
                center.x + halfX, center.y - halfY, center.z + halfZ,
                center.x + halfX, center.y - halfY, center.z + halfZ,
                center.x + halfX, center.y - halfY, center.z - halfZ,
                center.x + halfX, center.y - halfY, center.z - halfZ,
                center.x - halfX, center.y - halfY, center.z - halfZ,
                center.x - halfX, center.y - halfY, center.z - halfZ,
                center.x - halfX, center.y - halfY, center.z + halfZ,
                center.x - halfX, center.y + halfY, center.z + halfZ,
                center.x + halfX, center.y + halfY, center.z + halfZ,
                center.x + halfX, center.y + halfY, center.z + halfZ,
                center.x + halfX, center.y + halfY, center.z - halfZ,
                center.x + halfX, center.y + halfY, center.z - halfZ,
                center.x - halfX, center.y + halfY, center.z - halfZ,
                center.x - halfX, center.y + halfY, center.z - halfZ,
                center.x - halfX, center.y + halfY, center.z + halfZ,
                center.x - halfX, center.y - halfY, center.z + halfZ,
                center.x - halfX, center.y + halfY, center.z + halfZ,
                center.x + halfX, center.y - halfY, center.z + halfZ,
                center.x + halfX, center.y + halfY, center.z + halfZ,
                center.x + halfX, center.y - halfY, center.z - halfZ,
                center.x + halfX, center.y + halfY, center.z - halfZ,
                center.x - halfX, center.y - halfY, center.z - halfZ,
                center.x - halfX, center.y + halfY, center.z - halfZ,
            };
            float colorData[96];
            for (int i = 0; i < 96; i += 4) 
            {
                colorData[i] = color.x;
                colorData[i + 1] = color.y;
                colorData[i + 2] = color.z;
                colorData[i + 3] = color.w;
            }
            memcpy(vbuf, verticesData, sizeof(verticesData));
            memcpy(cbuf, colorData, sizeof(colorData));
            p_buf->AppendData(vbuf, 72, cbuf, 96);
            _vertex_num += 24;
        }

        static void Submit()
        {
            if (s_color.a > 0.0f)
            {
                p_buf->UploadData();
                p_buf->Bind();
                D3DContext::GetInstance()->DrawInstanced(_vertex_num, 1);               
            }
            _vertex_num = 0;
        }

    public:
        inline static Color s_color = Colors::kGray;
    private:
        inline static uint32_t _vertex_num = 0u;
        inline static Ref<DynamicVertexBuffer> p_buf = nullptr;
    };
}

#endif // !__D3D_CONTEXT_H__

