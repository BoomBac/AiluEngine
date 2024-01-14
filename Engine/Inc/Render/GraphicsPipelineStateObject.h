#pragma once
#ifndef __GFX_PIPELINE_STATE_H__
#define __GFX_PIPELINE_STATE_H__

#include "PipelineState.h"
#include "Framework/Math/ALMath.hpp"
#include "GlobalMarco.h"
#include "Shader.h"
#include "AlgFormat.h"
#include "Texture.h"



//ref
//https://zhuanlan.zhihu.com/p/582020846
//https://alpqr.github.io/qtrhi/qrhigraphicspipeline.html#CompareOp-enum
//https://learn.microsoft.com/en-us/windows/win32/direct3d12/managing-graphics-pipeline-state-in-direct3d-12

namespace Ailu
{
	struct GraphicsPipelineStateInitializer
	{
		VertexInputLayout _input_layout; //0~3 4
		Shader* _p_vertex_shader; //4~13 10
		Shader* _p_pixel_shader; // 14~35 22
		ETopology _topology; //36~37 2
		BlendState _blend_state; // 38~40 3
		RasterizerState _raster_state;// 41 ~ 43 3
		DepthStencilState _depth_stencil_state;// 44~46 3
		RenderTargetState _rt_state;// 47~49 3

		static GraphicsPipelineStateInitializer GetNormalOpaquePSODesc()
		{
			GraphicsPipelineStateInitializer pso_desc{};
			pso_desc._blend_state = BlendState{};
			pso_desc._depth_stencil_state = TStaticDepthStencilState<true, ECompareFunc::kLess>::GetRHI();
			pso_desc._rt_state = RenderTargetState{};
			pso_desc._topology = ETopology::kTriangle;
			pso_desc._raster_state = TStaticRasterizerState<ECullMode::kBack, EFillMode::kSolid>::GetRHI();
			return pso_desc;
		}

		static GraphicsPipelineStateInitializer GetNormalTransparentPSODesc()
		{
			GraphicsPipelineStateInitializer pso_desc{};
			pso_desc._blend_state = TStaticBlendState<true, EBlendFactor::kSrcAlpha, EBlendFactor::kOneMinusSrcAlpha>::GetRHI();
			pso_desc._rt_state = RenderTargetState{};
			pso_desc._depth_stencil_state = TStaticDepthStencilState<false, ECompareFunc::kAlways>::GetRHI();
			pso_desc._topology = ETopology::kTriangle;
			pso_desc._raster_state = TStaticRasterizerState<ECullMode::kBack, EFillMode::kSolid>::GetRHI();
			return pso_desc;
		}
	};

	class GraphicsPipelineStateObject
	{
	public:
		inline static GraphicsPipelineStateObject* sCurrent = 0;
		static Scope<GraphicsPipelineStateObject> Create(const GraphicsPipelineStateInitializer& initializer);
		static ALHash::Hash<64> ConstructPSOHash(u8 input_layout,u32 shader,u8 topology,u8 blend_state,u8 raster_state,u8 ds_state,u8 rt_state);
		static void ConstructPSOHash(ALHash::Hash<64>& hash,u8 input_layout,u32 shader,u8 topology,u8 blend_state,u8 raster_state,u8 ds_state,u8 rt_state);
		static ALHash::Hash<64> ConstructPSOHash(const GraphicsPipelineStateInitializer& initializer);
		static void ExtractPSOHash(const ALHash::Hash<64>& pso_hash, u8& input_layout, u32& shader, u8& topology, u8& blend_state, u8& raster_state, u8& ds_state, u8& rt_state);

		virtual ~GraphicsPipelineStateObject() = default;
		virtual void Build() = 0;
		virtual void Bind() = 0;
		virtual void SetPipelineResource(void* res, const EBindResDescType& res_type, u8 slot = 255) = 0;
		virtual void SetPipelineResource(void* res, const EBindResDescType& res_type, const String& name) = 0;
		virtual const ALHash::Hash<64>& Hash() = 0;
		virtual const String& Name() const = 0;
	protected:
		virtual void BindResource(void* res, const EBindResDescType& res_type, u8 slot = 255) = 0;
	};

	enum EGraphicsPSO : uint32_t
	{
		kStandShadering = 0u,
		kWireFrame = 1u,
		kShadedWireFrame = 3u,
		kGizmo = 4u,
	};

	class GraphicsPipelineStateMgr
	{
		struct PipelineResourceInfo
		{
			void* _p_resource = nullptr;
			EBindResDescType _res_type = EBindResDescType::kUnknown;
			u8 _slot = 0;
		};
	public:
		static void BuildPSOCache();
		static void AddPSO(Scope<GraphicsPipelineStateObject> p_gpso);
		static GraphicsPipelineStateObject* GetPso(const uint32_t& id);
		inline static GraphicsPipelineStateObject* s_standard_shadering_pso = nullptr;
		inline static GraphicsPipelineStateObject* s_gizmo_pso = nullptr;
		inline static GraphicsPipelineStateObject* s_wireframe_pso = nullptr;
		static void EndConfigurePSO();
		static void OnShaderRecompiled(Shader* shader);
		static void ConfigureShader(const u32& shader_hash);
		static void ConfigureVertexInputLayout(const u8& hash); //0~3 4
		static void ConfigureTopology(const u8& hash); //36~37 2
		static void ConfigureBlendState(const u8& hash); // 38~40 3
		static void ConfigureRasterizerState(const u8& hash);// 41 ~ 43 3
		static void ConfigureDepthStencilState(const u8& hash);// 44~46 3
		static void ConfigureRenderTarget(const u8& hash);// 44~46 3
		static void SetRenderTargetState(EALGFormat color_format,EALGFormat depth_format, u8 color_rt_id = 0);
		static void SetRenderTargetState(EALGFormat color_format,u8 color_rt_id = 0);

		static bool IsReadyForCurrentDrawCall() { return s_is_ready; }

		static void SubmitBindResource(void* res, const EBindResDescType& res_type, u8 slot = 255);
		static void SubmitBindResource(Texture* texture, u8 slot = -1);
	private:
		inline static Queue<Scope<GraphicsPipelineStateObject>> s_update_pso{};
		inline static std::map<ALHash::Hash<64>, Scope<GraphicsPipelineStateObject>> s_pso_library{};
		inline static std::unordered_map<uint32_t, Scope<GraphicsPipelineStateObject>> s_pso_pool{};
		inline static uint32_t s_reserved_pso_id = 32u;
		inline static List<PipelineResourceInfo> s_bind_resource_list{};
		inline static bool s_is_ready = false;
		static RenderTargetState _s_render_target_state;

		inline static ALHash::Hash<64> s_cur_pos_hash{};
		inline static u32 s_hash_shader; // 4~35 32
		inline static u8 s_hash_input_layout; //0~3 4
		inline static u8 s_hash_topology; //36~37 2
		inline static u8 s_hash_blend_state; // 38~40 3
		inline static u8 s_hash_raster_state;// 41 ~ 43 3
		inline static u8 s_hash_depth_stencil_state;// 44~46 3
		inline static u8 s_hash_rt_state;// 44~46 3
	};
}


#endif // !GFX_PIPELINE_STATE_H__