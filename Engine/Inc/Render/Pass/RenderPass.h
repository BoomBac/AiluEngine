#pragma once
#ifndef __RENDER_PASS__
#define __RENDER_PASS__
#include "Render/GraphicsContext.h"
#include "Render/RenderingData.h"
#include "Render/Material.h"
#include "Render/Mesh.h"

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

	class RenderPass : public IRenderPass
	{
	public:
		RenderPass(const String& name) : _name(name), _is_active(true) {};
		virtual void Execute(GraphicsContext* context, RenderingData& rendering_data) override {};
		virtual void BeginPass(GraphicsContext* context) override {};
		virtual void EndPass(GraphicsContext* context) override {};
		virtual const String& GetName() const final { return _name; };
		virtual const bool IsActive() const final {return _is_active; };
		virtual const void SetActive(bool is_active) final { _is_active = is_active; };
	protected:
		String _name;
		bool _is_active;
	};

	class OpaquePass : public RenderPass
	{
	public:
		OpaquePass();
		~OpaquePass();
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
	};

	class ResolvePass : public RenderPass
	{
	public:
		ResolvePass(Ref<RenderTexture>& source);
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void BeginPass(GraphicsContext* context, Ref<RenderTexture>& source);
		void EndPass(GraphicsContext* context) final;
		bool _is_offscreen = false;
	private:
		Rect _backbuf_rect;
		Ref<RenderTexture> _p_src_color;
	};

	class ShadowCastPass : public RenderPass
	{
		inline static u16 kShadowMapSize = 2048;
	public:
		ShadowCastPass();
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
	private:
		Rect _rect;
		Ref<RenderTexture> _p_shadow_map;
		Ref<Material> _p_shadowcast_material;
	};

	class CubeMapGenPass : public RenderPass
	{
	public:
		CubeMapGenPass(u16 size,String texture_name,String src_texture_name);
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
		Ref<RenderTexture> _p_cube_map;
		Ref<RenderTexture> _p_env_map;
	private:
		ScenePerPassData _pass_data[6];
		Scope<ConstantBuffer> _per_pass_cb[6];
		Scope<ConstantBuffer> _per_obj_cb;
		Matrix4x4f _world_mat;
		Rect _rect;
		Ref<Material> _p_gen_material;
		Ref<Material> _p_filter_material;
		Ref<Mesh> _p_cube_mesh;
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
		Vector<Ref<RenderTexture>> _gbuffers;
		Ref<Material> _p_lighting_material;
		Ref<Mesh> _p_quad_mesh;
		Array<Rect, 3> _rects;
	};

	class SkyboxPass : public RenderPass
	{
	public:
		SkyboxPass();
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
	private:
		Ref<Mesh> _p_sky_mesh;
		Ref<Material> _p_skybox_material;
		Scope<ConstantBuffer> _p_cbuffer;
	};
}

#endif // !RENDER_PASS__

