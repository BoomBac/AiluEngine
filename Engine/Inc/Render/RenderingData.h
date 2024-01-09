#pragma once
#ifndef __RENDERING_DATA_H__
#define __RENDERING_DATA_H__

#include <cstdint>
#include "Framework/Math/ALMath.hpp"

namespace Ailu
{
    enum class EShaderingMode : uint8_t
    {
        kShader,kWireFrame,kShaderedWireFrame
    };
    struct RenderingStates
    {      
        inline static uint32_t s_vertex_num = 0u;
        inline static uint32_t s_triangle_num = 0u;
        inline static uint32_t s_draw_call = 0u;
        static void Reset()
        {
            s_vertex_num = 0u;
            s_triangle_num = 0u;
            s_draw_call = 0u;
        }
        inline static EShaderingMode s_shadering_mode = EShaderingMode::kShader;
    };
	struct ShaderDirectionalAndPointLightData
	{
		union
		{
			Vector3f _LightDir;
			Vector3f _LightPos;
		};
		float _LightParam0;
		Vector3f _LightColor;
		float _LightParam1;
		ShaderDirectionalAndPointLightData() {}
	};

	struct ShaderSpotlLightData
	{
		Vector3f  _LightDir;
		float	  _Rdius;
		Vector3f  _LightPos;
		float	  _InnerAngle;
		Vector3f  _LightColor;
		float     _OuterAngle;
	};
	constexpr static uint16_t kMaxDirectionalLightNum = 2u;
	constexpr static uint16_t kMaxPointLightNum = 4u;
	constexpr static uint16_t kMaxSpotLightNum = 4u;

	struct ScenePerFrameData
	{
		Matrix4x4f _MatrixV;
		Matrix4x4f _MatrixP;
		Matrix4x4f _MatrixVP;
		Vector4f   _CameraPos;
		ShaderDirectionalAndPointLightData _DirectionalLights[kMaxDirectionalLightNum];
		ShaderDirectionalAndPointLightData _PointLights[kMaxPointLightNum];
		ShaderSpotlLightData _SpotLights[kMaxSpotLightNum];
		float padding[44];
	};

	static_assert((sizeof(ScenePerFrameData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

	struct ScenePerMaterialData
	{
		Vector4f _BaseColor;
		float	 _Roughness;
		Vector3f _Emssive;
		float	 _Metallic;
		float    _Specular;
		// low_bit: metallic|roughness|emssive|normal|albedo
		uint32_t _SamplerMask;
		float padding[53]; // Padding so the constant buffer is 256-byte aligned.
	};

	static_assert((sizeof(ScenePerMaterialData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

	struct ScenePerObjectData
	{
		Matrix4x4f _MatrixM;
		float padding[48]; // Padding so the constant buffer is 256-byte aligned.
	};
	static_assert((sizeof(ScenePerObjectData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

    struct RenderingShadowData
    {
        Matrix4x4f _shadow_view;
        Matrix4x4f _shadow_proj;
        float _shadow_bias = 0.001f;
    };
    struct RenderingData
    {
    public:
        RenderingShadowData _shadow_data[8];
        u8 _actived_shadow_count = 0;
        void Reset()
        {
            _actived_shadow_count = 0;
        }
    };
}


#endif // !RENDERING_DATA_H__

