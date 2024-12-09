#pragma once
#ifndef __D3DSHADER_H__
#define __D3DSHADER_H__
#include <../Ext/d3dx12.h>
#include <d3d12shader.h>
#include <map>
#include <unordered_map>

#include "Render/RenderConstants.h"
#include "Render/Shader.h"


using Microsoft::WRL::ComPtr;

namespace Ailu
{
    namespace D3DConvertUtils
    {
        static EShaderDateType GetShaderDataType(const char *semantic, u8 mask)
        {
            String set_str(semantic);
            if (su::Equal(set_str, RenderConstants::kSemanticPosition) || su::Equal(set_str, RenderConstants::kSemanticNormal) || su::Equal(set_str, RenderConstants::kSemanticTangent) || su::Equal(set_str, RenderConstants::kSemanticColor) || su::Equal(set_str, RenderConstants::kSemanticTexcoord) || su::Equal(set_str, RenderConstants::kSemanticBoneWeight))
            {
                if (mask == 15)//1111b
                    return EShaderDateType::kFloat4;
                else if (mask == 7)//111b
                    return EShaderDateType::kFloat3;
                else if (mask == 3)
                    return EShaderDateType::kFloat2;
                else if (mask == 1)
                    return EShaderDateType::kFloat;
                else
                    return EShaderDateType::kNone;
            }
            else if (su::Equal(set_str, RenderConstants::kSemanticBoneIndex))
            {
                if (mask == 15)
                    return EShaderDateType::kuInt4;
                else if (mask == 7)
                    return EShaderDateType::kuInt3;
                else if (mask == 3)
                    return EShaderDateType::kuInt2;
                else if (mask == 1)
                    return EShaderDateType::kuInt;
                else
                    return EShaderDateType::kNone;
            }
            else if (su::Equal(set_str, RenderConstants::kSemanticVertexIndex) || su::Equal(set_str, RenderConstants::kSemanticInstanceID))
                return EShaderDateType::kNone;
            return EShaderDateType::kNone;
        }
        static DXGI_FORMAT GetGXGIFormatByShaderDataType(EShaderDateType data_type)
        {
            switch (data_type)
            {
                case EShaderDateType::kFloat4:
                    return DXGI_FORMAT_R32G32B32A32_FLOAT;
                case EShaderDateType::kFloat3:
                    return DXGI_FORMAT_R32G32B32_FLOAT;
                case EShaderDateType::kFloat2:
                    return DXGI_FORMAT_R32G32_FLOAT;
                case EShaderDateType::kuInt4:
                    return DXGI_FORMAT_R32G32B32A32_UINT;
                case EShaderDateType::kuInt3:
                    return DXGI_FORMAT_R32G32B32_UINT;
                case EShaderDateType::kuInt2:
                    return DXGI_FORMAT_R32G32_UINT;
                case EShaderDateType::kuInt:
                    return DXGI_FORMAT_R32_UINT;
                default:
                    return DXGI_FORMAT_UNKNOWN;
            }
        }
        static std::tuple<D3D12_INPUT_ELEMENT_DESC *, u8> ConvertToD3D12InputLayout(const VertexInputLayout &layout)
        {
            static D3D12_INPUT_ELEMENT_DESC desc_arr[RenderConstants::kMaxVertexAttrNum];
            u8 layout_index = 0;
            for (auto &it: layout)
            {

                desc_arr[layout_index++] = D3D12_INPUT_ELEMENT_DESC{it.Name.c_str(), 0,
                                                                    GetGXGIFormatByShaderDataType(it.Type), it.Stream, it.Offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
            }
            return std::make_tuple(desc_arr, layout_index + 1);
        }

        static D3D12_RASTERIZER_DESC ConvertToD3D12RasterizerDesc(const RasterizerState &state)
        {
            auto raster_state = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            if (state._cull_mode == ECullMode::kFront) raster_state.CullMode = D3D12_CULL_MODE_FRONT;
            else if (state._cull_mode == ECullMode::kNone)
                raster_state.CullMode = D3D12_CULL_MODE_NONE;
            else
                raster_state.CullMode = D3D12_CULL_MODE_BACK;
            if (state._fill_mode == EFillMode::kWireframe) raster_state.FillMode = D3D12_FILL_MODE_WIREFRAME;
            else
                raster_state.FillMode = D3D12_FILL_MODE_SOLID;
            raster_state.ConservativeRaster = state._is_conservative ? D3D12_CONSERVATIVE_RASTERIZATION_MODE::D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON : D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
            return raster_state;
        }

        static D3D12_BLEND ConvertToD3D12BlendFactor(const EBlendFactor &factor)
        {
            switch (factor)
            {
                case EBlendFactor::kZero:
                    return D3D12_BLEND_ZERO;
                case EBlendFactor::kOne:
                    return D3D12_BLEND_ONE;
                case EBlendFactor::kSrcColor:
                    return D3D12_BLEND_SRC_COLOR;
                case EBlendFactor::kOneMinusSrcColor:
                    return D3D12_BLEND_INV_SRC_COLOR;
                case EBlendFactor::kDstColor:
                    return D3D12_BLEND_DEST_COLOR;
                case EBlendFactor::kOneMinusDstColor:
                    return D3D12_BLEND_INV_DEST_COLOR;
                case EBlendFactor::kSrcAlpha:
                    return D3D12_BLEND_SRC_ALPHA;
                case EBlendFactor::kOneMinusSrcAlpha:
                    return D3D12_BLEND_INV_SRC_ALPHA;
                case EBlendFactor::kDstAlpha:
                    return D3D12_BLEND_DEST_ALPHA;
                case EBlendFactor::kOneMinusDstAlpha:
                    return D3D12_BLEND_INV_DEST_ALPHA;
                    //case EBlendFactor::kConstantColor:          return
                    //case EBlendFactor::kOneMinusConstantColor:  return
                    //case EBlendFactor::kConstantAlpha:          return
                    //case EBlendFactor::kOneMinusConstantAlpha:  return
                    //case EBlendFactor::kSrcAlphaSaturate:       return
                case EBlendFactor::kSrc1Color:
                    return D3D12_BLEND_SRC1_COLOR;
                case EBlendFactor::kOneMinusSrc1Color:
                    return D3D12_BLEND_INV_SRC1_COLOR;
                case EBlendFactor::kSrc1Alpha:
                    return D3D12_BLEND_SRC1_ALPHA;
                case EBlendFactor::kOneMinusSrc1Alpha:
                    return D3D12_BLEND_INV_SRC1_ALPHA;
            }
            return D3D12_BLEND_ZERO;
        }

        static D3D12_BLEND_OP ConvertToD3D12BlendOp(const EBlendOp &op)
        {
            switch (op)
            {
                case EBlendOp::kAdd:
                    return D3D12_BLEND_OP_ADD;
                case EBlendOp::kMax:
                    return D3D12_BLEND_OP_MAX;
                case EBlendOp::kMin:
                    return D3D12_BLEND_OP_MIN;
                case EBlendOp::kReverseSubtract:
                    return D3D12_BLEND_OP_REV_SUBTRACT;
                case EBlendOp::kSubtract:
                    return D3D12_BLEND_OP_SUBTRACT;
            }
            return D3D12_BLEND_OP_ADD;
        }

        static u8 ConvertToD3DBlendColorMask(const EColorMask &mask)
        {
            switch (mask)
            {
                case Ailu::EColorMask::kRED:
                    return D3D12_COLOR_WRITE_ENABLE_RED;
                case Ailu::EColorMask::kGREEN:
                    return D3D12_COLOR_WRITE_ENABLE_GREEN;
                case Ailu::EColorMask::kBLUE:
                    return D3D12_COLOR_WRITE_ENABLE_BLUE;
                case Ailu::EColorMask::kALPHA:
                    return D3D12_COLOR_WRITE_ENABLE_ALPHA;
                case Ailu::EColorMask::k_NONE:
                    return 0u;
                case Ailu::EColorMask::k_RGB:
                    return (D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE);
                case Ailu::EColorMask::k_RGBA:
                    return D3D12_COLOR_WRITE_ENABLE_ALL;
                case Ailu::EColorMask::k_RG:
                    return (D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN);
                case Ailu::EColorMask::k_BA:
                    return (D3D12_COLOR_WRITE_ENABLE_BLUE | D3D12_COLOR_WRITE_ENABLE_ALPHA);
            }
            return D3D12_COLOR_WRITE_ENABLE_ALL;
        }

        static D3D12_BLEND_DESC ConvertToD3D12BlendDesc(const BlendState &state, const u8 &rt_num)
        {
            auto blend_state = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            for (size_t i = 0; i < rt_num; i++)
            {
                blend_state.RenderTarget[i].BlendEnable = state._b_enable;
                blend_state.RenderTarget[i].BlendOp = ConvertToD3D12BlendOp(state._color_op);
                blend_state.RenderTarget[i].BlendOpAlpha = ConvertToD3D12BlendOp(state._alpha_op);
                blend_state.RenderTarget[i].SrcBlend = ConvertToD3D12BlendFactor(state._src_color);
                blend_state.RenderTarget[i].SrcBlendAlpha = ConvertToD3D12BlendFactor(state._src_alpha);
                blend_state.RenderTarget[i].DestBlend = ConvertToD3D12BlendFactor(state._dst_color);
                blend_state.RenderTarget[i].DestBlendAlpha = ConvertToD3D12BlendFactor(state._dst_alpha);
                blend_state.RenderTarget[i].RenderTargetWriteMask = ConvertToD3DBlendColorMask(state._color_mask);
            }
            return blend_state;
        }

        static D3D12_BLEND_DESC ConvertToD3D12BlendDesc(const BlendState &state)
        {
            return ConvertToD3D12BlendDesc(state, 1);
        }

        static D3D12_COMPARISON_FUNC ConvertToD3D12CompareFunc(const ECompareFunc &func)
        {
            switch (func)
            {
                case ECompareFunc::kLess:
                    return D3D12_COMPARISON_FUNC_LESS;
                case ECompareFunc::kLessEqual:
                    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
                case ECompareFunc::kGreater:
                    return D3D12_COMPARISON_FUNC_GREATER;
                case ECompareFunc::kGreaterEqual:
                    return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
                case ECompareFunc::kAlways:
                    return D3D12_COMPARISON_FUNC_ALWAYS;
                case ECompareFunc::kNever:
                    return D3D12_COMPARISON_FUNC_NEVER;
                case ECompareFunc::kEqual:
                    return D3D12_COMPARISON_FUNC_EQUAL;
                case ECompareFunc::kNotEqual:
                    return D3D12_COMPARISON_FUNC_NOT_EQUAL;
            }
            return D3D12_COMPARISON_FUNC_LESS;
        }

        static D3D12_STENCIL_OP ConvertToD3D12StencilOp(const EStencilOp &op)
        {
            switch (op)
            {
                case EStencilOp::kKeep:
                    return D3D12_STENCIL_OP_KEEP;
                case EStencilOp::kZero:
                    return D3D12_STENCIL_OP_ZERO;
                case EStencilOp::kReplace:
                    return D3D12_STENCIL_OP_REPLACE;
                case EStencilOp::kIncrementAndClamp:
                    return D3D12_STENCIL_OP_INCR_SAT;
                case EStencilOp::kDecrementAndClamp:
                    return D3D12_STENCIL_OP_DECR_SAT;
                case EStencilOp::kInvert:
                    return D3D12_STENCIL_OP_INVERT;
                case EStencilOp::kIncrement:
                    return D3D12_STENCIL_OP_INCR;
                case EStencilOp::kDecrement:
                    return D3D12_STENCIL_OP_DECR;
            }
            return D3D12_STENCIL_OP_KEEP;
        }

        static D3D12_DEPTH_STENCIL_DESC ConvertToD3D12DepthStencilDesc(const DepthStencilState &state)
        {
            auto ds_state = CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT{});
            ds_state.DepthEnable = state._depth_test_func != ECompareFunc::kAlways && state._depth_test_func != ECompareFunc::kNever;
            ds_state.DepthFunc = ConvertToD3D12CompareFunc(state._depth_test_func);
            ds_state.StencilEnable = state._b_front_stencil;
            if (state._b_front_stencil)
            {
                ds_state.FrontFace.StencilFunc = ConvertToD3D12CompareFunc(state._stencil_test_func);
                ds_state.FrontFace.StencilPassOp = ConvertToD3D12StencilOp(state._stencil_test_pass_op);
                ds_state.BackFace.StencilFunc = ds_state.FrontFace.StencilFunc;
                ds_state.BackFace.StencilPassOp = ds_state.FrontFace.StencilPassOp;
            }
            ds_state.DepthWriteMask = state._b_depth_write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            return ds_state;
        }

        static D3D12_PRIMITIVE_TOPOLOGY_TYPE ConvertToDXTopologyType(const ETopology &Topology)
        {
            switch (Topology)
            {
                case ETopology::kPoint:
                    return D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
                case ETopology::kLine:
                    return D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                case ETopology::kTriangle:
                    return D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                case ETopology::kTriangleStrip:
                    return D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                case ETopology::kPatch:
                    return D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
            }
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        }

        static D3D_PRIMITIVE_TOPOLOGY ConvertToDXTopology(const ETopology &Topology)
        {
            switch (Topology)
            {
                case ETopology::kPoint:
                    return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
                case ETopology::kLine:
                    return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_LINELIST;
                case ETopology::kTriangle:
                    return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                case ETopology::kTriangleStrip:
                    return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
                case ETopology::kPatch:
                    return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
            }
            return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        }
    }// namespace D3DConvertUtils

    class D3DShader : public Shader
    {
        struct D3DShaderElement
        {
            struct D3DVariantElement
            {
                Vector<D3D_SHADER_MACRO> _keyword_defines;
                ComPtr<ID3D12RootSignature> _p_sig;
                ComPtr<ID3DBlob> _p_vblob = nullptr;
                ComPtr<ID3DBlob> _p_pblob = nullptr;
                ComPtr<ID3DBlob> _p_gblob = nullptr;
                ComPtr<ID3D12ShaderReflection> _p_v_reflection;
                ComPtr<ID3D12ShaderReflection> _p_p_reflection;
                ComPtr<ID3D12ShaderReflection> _p_g_reflection;
                D3D12_INPUT_ELEMENT_DESC _vertex_input_layout[RenderConstants::kMaxVertexAttrNum];
            };
            Map<ShaderVariantHash, D3DVariantElement> _variants;
            D3D12_PRIMITIVE_TOPOLOGY _topology = D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        };

    public:
        D3DShader(const WString &sys_path);
        ~D3DShader();
        void Bind(u16 pass_index, ShaderVariantHash variant_hash) final;
        void *GetByteCode(EShaderType type, u16 pass_index, ShaderVariantHash variant_hash) final;
        ID3D12RootSignature *GetSignature(u16 pass_index, ShaderVariantHash variant_hash);
        std::pair<D3D12_INPUT_ELEMENT_DESC *, u8> GetVertexInputLayout(u16 pass_index, ShaderVariantHash variant_hash);

    private:
        bool RHICompileImpl(u16 pass_index, ShaderVariantHash variant_hash) final;
        void Reset();
        void LoadShaderReflection(u16 pass_index, ShaderVariantHash variant_hash, ID3D12ShaderReflection *ref_vs, ID3D12ShaderReflection *ref_ps);
        void LoadAdditionalShaderReflection(const WString &sys_path, u16 pass_index, ShaderVariantHash variant_hash);
        void GenerateInternalPSO(u16 pass_index, ShaderVariantHash variant_hash);
        Vector<D3D_SHADER_MACRO> ConstructVariantMarcos(u16 pass_index, ShaderVariantHash variant_hash);

    private:
        Vector<D3DShaderElement> _pass_elements;
        bool _is_first_compile = true;
    };

    class D3DComputeShader : public ComputeShader
    {
        struct PSOSystem
        {
            FORCEINLINE std::tuple<ComPtr<ID3D12RootSignature> &, ComPtr<ID3D12PipelineState> &> Front()
            {
                return _b_current_is_a ? std::tie(_p_sig_a, _p_pso_a) : std::tie(_p_sig_b, _p_pso_b);
            }

            inline std::tuple<ComPtr<ID3D12RootSignature> &, ComPtr<ID3D12PipelineState> &> Back()
            {
                auto &sig = !_b_current_is_a ? _p_sig_a : _p_sig_b;
                auto &pso = !_b_current_is_a ? _p_pso_a : _p_pso_b;
                sig.Reset();
                pso.Reset();
                return std::tie(sig, pso);
            }
            void Bind(ID3D12GraphicsCommandList *cmd)
            {
                auto [sig, pso] = Front();
                cmd->SetPipelineState(pso.Get());
                cmd->SetComputeRootSignature(sig.Get());
            }
            inline void Swap()
            {
                _b_current_is_a = !_b_current_is_a;
            }

        private:
            bool _b_current_is_a = true;
            ComPtr<ID3D12RootSignature> _p_sig_a;
            ComPtr<ID3D12PipelineState> _p_pso_a;
            ComPtr<ID3D12RootSignature> _p_sig_b;
            ComPtr<ID3D12PipelineState> _p_pso_b;
        };
        struct D3DKernelElement
        {
            ComPtr<ID3DBlob> _p_blob;
            PSOSystem _pso_sys;
        };

    public:
        D3DComputeShader(const WString &sys_path);
        //void Dispatch(u16 thread_group_x, u16 thread_group_y, u16 thread_group_z) final;
        void Bind(CommandBuffer *cmd, u16 kernel, u16 thread_group_x, u16 thread_group_y, u16 thread_group_z) final;
        u16 NameToSlot(const String& name,u16 kernel) const final;
    private:
        bool RHICompileImpl(u16 kernel_index) final;
        void GenerateInternalPSO(u16 kernel_index);
        void LoadReflectionInfo(ID3D12ShaderReflection *p_reflect, u16 kernel_index);
        void LoadAdditionalShaderReflection(const WString &sys_path, u16 kernel_index, ShaderVariantHash variant_hash = 0);

    private:
        Vector<D3DKernelElement> _elements;
        std::mutex _ele_lock;
    };
}// namespace Ailu


#endif// !__D3DSHADER_H__
