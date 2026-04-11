#ifndef __DXR_SAMPLE_H__
#define __DXR_SAMPLE_H__

#include <d3dx12.h>
#include <dxgi1_6.h>
#include <d3d12shader.h>

#include "Render/Shader.h"
#include "Render/Buffer.h"
#include "Render/RayTracing/RayTracingScene.h"
#include "Render/RayTracing/RayTracingGeometry.h"
#include "Render/RayTracing/RayTracingShader.h"

using Microsoft::WRL::ComPtr;

namespace Ailu
{
    namespace Render
    {
        class Mesh;
        class Texture2D;
        class RHICommandBuffer;
    }// namespace Render

    namespace RHI::DX12
    {
        class DXRSample
        {
        public:
            struct Viewport
            {
                float left;
                float top;
                float right;
                float bottom;
            };

            struct RayGenConstantBuffer
            {
                Viewport viewport;
                Viewport stencil;
            };

            DXRSample(ID3D12Device5 *device, ID3D12CommandQueue *cmd_queue);
            ~DXRSample();
            void Render(Render::RHICommandBuffer *cmd, u16 w, u16 h);
            void SetInstanceTransform(u32 instance_index, const Matrix4x4f &transform);
            Render::Texture2D* Output() {return m_uav_output.get();}
            
        private:
            void Init(u16 w, u16 h);
            void BuildRaytracingAccelerationStructures();
            void MakesureOutput(u16 w, u16 h);
            static void FillInstanceTransform(const Matrix4x4f &matrix, D3D12_RAYTRACING_INSTANCE_DESC &instance_desc);

        private:
            ID3D12Device5 *m_dxrDevice;
            ID3D12CommandQueue *m_commandQueue;

            Ref<Render::VertexBuffer> m_vertexBuffer;
            Ref<Render::IndexBuffer> m_indexBuffer;
            Ref<Render::GPUBuffer> _vertex_data;
            Ref<Render::GPUBuffer> _normal_data;
            Ref<Render::GPUBuffer> _indices_data;
            Ref<Render::GPUBuffer> _instance_geometry_data;
            Vector<D3D12_RAYTRACING_INSTANCE_DESC> m_instanceDescs;
            u32 m_instanceCount = 0u;
            bool m_tlasDirty = false;

            Ref<Render::RayTracingScene> m_scene;
            Ref<Render::RayTracingGeometry> m_cube;
            Ref<Render::RayTracingGeometry> m_plane;
            Ref<Render::RayTracingShader> m_raytracing_shader;

            // Raytracing scene
            RayGenConstantBuffer m_rayGenCB;
            Ref<Render::Texture2D> m_uav_output;
        };
    }// namespace RHI::DX12
}// namespace Ailu

#endif// __DXR_SAMPLE_H__