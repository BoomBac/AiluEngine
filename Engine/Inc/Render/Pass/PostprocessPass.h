#pragma once
#ifndef __POSTPROCESS_PASS_H__
#define __POSTPROCESS_PASS_H__
#include "RenderPass.h"
#include "Render/Texture.h"

namespace Ailu
{
	class PostProcessPass : public RenderPass
	{
	public:
		PostProcessPass();
		~PostProcessPass();
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
		f32 _upsample_radius = 0.005f;
		f32 _bloom_intensity = 0.35f;
	private:
		//Scope<RenderTexture> _p_tex_bloom_threshold;
		Ref<Material> _p_bloom_thread_mat;
		Material* _p_blit_mat;
		Mesh* _p_quad_mesh;
		IConstantBuffer* _p_obj_cb;
		Rect _bloom_thread_rect;
		u16 _bloom_iterator_count = 6;
        Texture *_nose_tex;
		Vector<Ref<Material>> _bloom_mats;
        Vector4f _noise_texel_size;
	};
}


#endif // !POSTPROCESS_PASS_H__

