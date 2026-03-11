#ifndef __DXR_SAMPLE_H__
#define __DXR_SAMPLE_H__

#include <d3dx12.h>
#include <dxgi1_6.h>
#include <d3d12shader.h>

#include "Render/Buffer.h"
#include "Render/RayTracing/RayTracingScene.h"
#include "Render/RayTracing/RayTracingGeometry.h"

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
            Ref<Render::GPUBuffer> BuildBottomLevelAccelerationStructure(Render::Mesh *mesh);
            void RebuildTopLevelAccelerationStructure(Render::RHICommandBuffer *cmd);
            void UpdateTopLevelAccelerationStructure(Render::RHICommandBuffer *cmd);
            void CreateRaytracingPipelineStateObject();
            void CreateLocalRootSignatureSubobjects(CD3DX12_STATE_OBJECT_DESC *raytracingPipeline);
            void BuildShaderTables();
            void CreateRootSignatures();
            void SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC &desc, ComPtr<ID3D12RootSignature> *rootSig);
            void MakesureOutput(u16 w, u16 h);
            void LoadLibraryReflection(ID3D12LibraryReflection *reflection);
            static void FillInstanceTransform(const Matrix4x4f &matrix, D3D12_RAYTRACING_INSTANCE_DESC &instance_desc);

        private:
            ID3D12Device5 *m_dxrDevice;
            ID3D12CommandQueue *m_commandQueue;
            ComPtr<ID3D12StateObject> m_dxrStateObject;
            // Root signatures
            ComPtr<ID3D12RootSignature> m_raytracingGlobalRootSignature;
            ComPtr<ID3D12RootSignature> m_raytracingLocalRootSignature;

            Ref<Render::VertexBuffer> m_vertexBuffer;
            Ref<Render::IndexBuffer> m_indexBuffer;
            Ref<Render::GPUBuffer> m_topLevelAS;
            Ref<Render::GPUBuffer> m_bottomLevelAS;
            Ref<Render::GPUBuffer> m_tlasScratchBuffer;
            Ref<Render::GPUBuffer> m_instanceDescsBuffer;
            Ref<Render::GPUBuffer> _vertex_data;
            Ref<Render::GPUBuffer> _normal_data;
            Ref<Render::GPUBuffer> _indices_data;
            Ref<Render::GPUBuffer> _instance_geometry_data;
            Ref<Render::GPUBuffer> _plane_blas;
            Vector<D3D12_RAYTRACING_INSTANCE_DESC> m_instanceDescs;
            u32 m_instanceCount = 0u;
            bool m_tlasDirty = false;
            ComPtr<ID3DBlob> m_byte_code;

            Ref<Render::RayTracingScene> m_scene;
            Ref<Render::RayTracingGeometry> m_cube;
            Ref<Render::RayTracingGeometry> m_plane;

            // Raytracing scene
            RayGenConstantBuffer m_rayGenCB;

            // Shader tables
            // static const wchar_t* c_hitGroupName;
            // static const wchar_t* c_raygenShaderName;
            // static const wchar_t* c_closestHitShaderName;
            // static const wchar_t* c_missShaderName;
            u32 m_hitGroupShaderRecordSize = 0u;
            ComPtr<ID3D12Resource> m_missShaderTable;
            ComPtr<ID3D12Resource> m_hitGroupShaderTable;
            ComPtr<ID3D12Resource> m_rayGenShaderTable;
            Ref<Render::Texture2D> m_uav_output;
        };
    }// namespace RHI::DX12
}// namespace Ailu

#endif// __DXR_SAMPLE_H__