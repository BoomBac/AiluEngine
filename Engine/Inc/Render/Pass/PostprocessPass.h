#pragma once
#ifndef __POSTPROCESS_PASS_H__
#define __POSTPROCESS_PASS_H__
#include "RenderPass.h"
#include "Render/Texture.h"
#include "Objects/Type.h"
#include "generated/PostprocessPass.gen.h"

namespace Ailu
{
//#define CURRENT_FILE_ID POSTPROCESSPASS_GEN_H
    ACLASS()
	class AILU_API PostProcessPass : public RenderPass
	{
        GENERATED_BODY();
        //BODY_MACRO_COMBINE(CURRENT_FILE_ID, _, __LINE__, _GENERATED_BODY)
	public:
		PostProcessPass();
		~PostProcessPass();
		void Execute(GraphicsContext* context, RenderingData& rendering_data) final;
		void BeginPass(GraphicsContext* context) final;
		void EndPass(GraphicsContext* context) final;
        APROPERTY()
		f32 _upsample_radius = 0.005f;
        APROPERTY()
		f32 _bloom_intensity = 0.35f;
        APROPERTY()
        bool _is_use_blur = false;
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
        Ref<ComputeShader> _cs_blur;
	};
}


#endif // !POSTPROCESS_PASS_H__

