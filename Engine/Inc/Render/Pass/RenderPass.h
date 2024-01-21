#pragma once
#ifndef __RENDER_PASS__
#define __RENDER_PASS__
#include "Render/GraphicsContext.h"
#include "Render/RenderingData.h"
#include "Render/Material.h"

namespace Ailu
{
	class RenderPass
	{
	public:
		~RenderPass() = default;
		virtual void Execute(GraphicsContext* context) = 0;
		virtual void Execute(GraphicsContext* context, CommandBuffer* cmd) = 0;
		virtual void BeginPass(GraphicsContext* context) = 0;
		virtual void EndPass(GraphicsContext* context) = 0;
		virtual const String& GetName() const = 0;
	};

	class OpaquePass : public RenderPass
	{
	public:
		OpaquePass();
		void Execute(GraphicsContext* context) final;
		void Execute(GraphicsContext* context,CommandBuffer* cmd) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
		const String& GetName() const final { return _name; };
	private:
		String _name;
	};

	class ResolvePass : public RenderPass
	{
	public:
		ResolvePass(Ref<RenderTexture>& source);
		void Execute(GraphicsContext* context) final;
		void Execute(GraphicsContext* context, CommandBuffer* cmd) final;
		void BeginPass(GraphicsContext* context) final;
		void BeginPass(GraphicsContext* context, Ref<RenderTexture>& source);
		void EndPass(GraphicsContext* context) final;
		const String& GetName() const final { return _name; };
	private:
		String _name;
		Ref<RenderTexture> _p_src_color;
	};

	class ShadowCastPass : public RenderPass
	{
		inline static u16 kShadowMapSize = 1024;
	public:
		ShadowCastPass();
		void Execute(GraphicsContext* context) final;
		void Execute(GraphicsContext* context, CommandBuffer* cmd) final;
		void Execute(GraphicsContext* context, CommandBuffer* cmd,const RenderingData& rendering_data);
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
		const String& GetName() const final { return _name; };
	private:
		Rect _rect;
		Ref<RenderTexture> _p_shadow_map;
		Ref<Material> _p_shadowcast_material;
		String _name;
	};

	class CubeMapGenPass : public RenderPass
	{
	public:
		CubeMapGenPass(u16 size,String texture_name,String src_texture_name);
		void Execute(GraphicsContext* context) final;
		void Execute(GraphicsContext* context, CommandBuffer* cmd) final;
		void Execute(GraphicsContext* context, CommandBuffer* cmd, const RenderingData& rendering_data);
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
		const String& GetName() const final { return _name; };
		Ref<RenderTexture> _p_cube_map;
		Ref<RenderTexture> _p_env_map;
	private:
		ScenePerPassData _pass_data[6];
		Matrix4x4f _world_mat;
		Rect _rect;
		Ref<Material> _p_gen_material;
		Ref<Material> _p_filter_material;
		Ref<Mesh> _p_cube_mesh;
		String _name;
	};
}

#endif // !RENDER_PASS__

