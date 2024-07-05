#pragma once
#ifndef __RENDER_PASS__
#define __RENDER_PASS__
#include "Render/GraphicsContext.h"
#include "Render/RenderingData.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Objects/Object.h"

namespace Ailu
{
	class IRenderPass
	{
	public:
		~IRenderPass() = default;
		virtual void Execute(GraphicsContext* context,RenderingData& rendering_data) = 0;
		virtual void BeginPass(GraphicsContext* context) = 0;
		virtual void EndPass(GraphicsContext* context) = 0;
		virtual const String& GetName() const = 0;
		virtual const bool IsActive() const = 0;
		virtual const void SetActive(bool is_active) = 0;
	};

	class RenderPass : public IRenderPass,public Object
	{
	public:
		RenderPass(const String& name) : Object(name), _is_active(true) {};
		virtual void Execute(GraphicsContext* context, RenderingData& rendering_data) override {};
		virtual void BeginPass(GraphicsContext* context) override {};
		virtual void EndPass(GraphicsContext* context) override {};
		virtual const String& GetName() const final { return _name; };
		virtual const bool IsActive() const final {return _is_active; };
		virtual const void SetActive(bool is_active) final { _is_active = is_active; };
	protected:
		bool _is_active;
	};

	class ForwardPass : public RenderPass
	{
	public:
		ForwardPass();
		~ForwardPass();
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
	private:
		Ref<Material> shader_state_mat;
		u16 _error_shader_pass_id, _compiling_shader_pass_id;
		Map<u32, Ref<Material>> _transparent_replacement_materials;
		Shader* _forward_lit_shader;
	};

	class ResolvePass : public RenderPass
	{
	public:
		ResolvePass();
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
		bool _is_offscreen = false;
	private:
	};

	class CopyColorPass : public RenderPass
	{
	public:
		CopyColorPass();
		~CopyColorPass();
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
	private:
		Material* _p_blit_mat;
		Mesh* _p_quad_mesh;
		ConstantBuffer* _p_obj_cb;
		Rect _half_sceen_rect;
	};

	class CopyDepthPass : public RenderPass
	{
	public:
		CopyDepthPass();
		~CopyDepthPass();
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
	private:
		Material* _p_blit_mat;
		ConstantBuffer* _p_obj_cb;
	};

	class ShadowCastPass : public RenderPass
	{
		inline static u16 kShadowMapSize = 1024;
	public:
		ShadowCastPass();
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
	private:
		Rect _rect;
		Rect _addlight_rect;
		Scope<RenderTexture> _p_mainlight_shadow_map;
		Scope<RenderTexture> _p_addlight_shadow_maps;
		Scope<RenderTexture> _p_point_light_shadow_maps;
		Vector<Ref<Material>> _shadowcast_materials;
	};

	class CubeMapGenPass : public RenderPass
	{
	public:
		CubeMapGenPass(u16 size,String texture_name,String src_texture_name);
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
		Scope<RenderTexture> _src_cubemap;
		Scope<RenderTexture> _prefilter_cubemap;
		Scope<RenderTexture> _radiance_map;
	private:
		bool is_source_by_texture;
		ScenePerPassData _pass_data[6];
		Scope<ConstantBuffer> _per_pass_cb[6];
		Scope<ConstantBuffer> _per_obj_cb;
		Matrix4x4f _world_mat;
		Rect _cubemap_rect;
		Rect _ibl_rect;
		Vector<Ref<Material>> _reflection_prefilter_mateirals;
		Material* _p_gen_material;
		Material* _p_filter_material;
		Mesh* _p_cube_mesh;
	};
	// 32-bit: standard gbuffer layout
	//ds 24-8
	//g0 lighting-accumulation24 intensity8
	//g1 normalx 16 normaly 16
	//g2 motion vector xy 16 spec-power/intensity 8
	//g3 diffuse-albedo 24 sun-occlusiton 8
	class DeferredGeometryPass : public RenderPass
	{
	public:
		DeferredGeometryPass(u16 width,u16 height);
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
	private:
		Array<Rect, 3> _rects;
	};

	class DeferredLightingPass : public RenderPass
	{
	public:
		DeferredLightingPass();
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
		Ref<Material> _p_lighting_material;
	private:
		Ref<Texture2D> _brdf_lut;
		Ref<ComputeShader> _brdflut_gen;
	};

	class SkyboxPass : public RenderPass
	{
	public:
		SkyboxPass();
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
	private:
		Mesh* _p_sky_mesh;
		Ref<Material> _p_skybox_material;
		Scope<ConstantBuffer> _p_cbuffer;
	};

	class GizmoPass : public RenderPass
	{
	public:
		GizmoPass();
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
	private:
		Vector<Scope<ConstantBuffer>> _p_cbuffers;
	};
}

#endif // !RENDER_PASS__

