#ifndef __BRDF_H__
#define __BRDF_H__
#include "constants.hlsli"
//#include "cbuffer.hlsl"
#include "input.hlsli"

#define F0_AIELECTRICS float3(0.04,0.04,0.04)
#define FROSTBITE_BRDF

//https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Shaders/Private/BRDF.ush


//----------Specular D,Normal Distribution Function（法线分布）--------------------------------------------
// Generalized-Trowbridge-Reitz distribution
// alpha 为粗糙度
// berry分布
float D_GTR1(float alpha, float dotNH)
{
	float a2 = alpha * alpha;
	float cos2th = dotNH * dotNH;
	float den = (1.0 + (a2 - 1.0) * cos2th);
	
	return (a2 - 1.0) / (PI * log(a2) * den);
}
//Trowbridge-Reitz 分布
float D_GTR2(float alpha, float dotNH)
{
	float a2 = alpha * alpha;
	float cos2th = dotNH * dotNH;
	float den = (1.0 + (a2 - 1.0) * cos2th);
	
	return a2 / (PI * den * den);
}
//各项异性版本
float D_GTR2_aniso(float dotHX, float dotHY, float dotNH, float ax, float ay)
{
	float deno = dotHX * dotHX / (ax * ax) + dotHY * dotHY / (ay * ay) + dotNH * dotNH;
	return 1.0 / (PI * ax * ay * deno * deno);
}
//-----------------------------------------------------------------------------------------------------

//-----------Specular F,Fresnel(菲涅尔)-----------------------------------------------------------------
half Pow2(half v)
{
    return v * v;
}
half Pow5(half v)
{
    return v * v * v * v * v;
}
//近似，完整的fresnel方程很复杂的
//计算寒霜优化版diffuse
float3 F_Schlick(in float3 f0 , in float f90 , in float hov)
{
	return f0 + ( f90 - f0 ) * Pow5(1.f - hov);
}
//FRESNEL近似版本
float3 F_Schlick(float3 f0,float hov)
{
	return f0 + ( 1 - f0 ) * Pow5(1.f - hov);
}
//for ibl diffuse
float3 F_SchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max((1.0 - roughness).xxx, F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// float3 F_Schlick( float3 SpecularColor, float VoH )
// {
// 	float Fc = Pow5( 1 - VoH );					// 1 sub, 3 mul
// 	//return Fc + (1 - Fc) * SpecularColor;		// 1 add, 3 mad
// 	// Anything less than 2% is physically impossible and is instead considered to be shadowing
// 	return saturate( 50.0 * SpecularColor.g ) * Fc + (1 - Fc) * SpecularColor;	
// }
// float3 F_Fresnel( float3 SpecularColor, float VoH )
// {
// 	float3 SpecularColorSqrt = sqrt( clamp( float3(0, 0, 0), float3(0.99, 0.99, 0.99), SpecularColor ) );
// 	float3 n = ( 1 + SpecularColorSqrt ) / ( 1 - SpecularColorSqrt );
// 	float3 g = sqrt( n*n + VoH*VoH - 1 );
// 	return 0.5 * Square( (g - VoH) / (g + VoH) ) * ( 1 + Square( ((g+VoH)*VoH - 1) / ((g-VoH)*VoH + 1) ) );
// }

//------------------------------------------------------------------------------------------------------

//-----------Specular G,Geometry(几何遮蔽，自遮挡)--------------------------------------------------------
// Smith GGX G项，各项同性版本
//alpha_g = (0.5 + roughness/2)^2
float smithG_GGX(float NdotV, float alphaG)
{
        float a = alphaG * alphaG;
        float b = NdotV * NdotV;
        return 1 / (NdotV + sqrt(a + b - a * b));
}

// Smith GGX G项，各项异性版本
// Derived G function for GGX
float smithG_GGX_aniso(float dotVN, float dotVX, float dotVY, float ax, float ay)
{
        return 1.0 / (dotVN + sqrt(pow(dotVX * ax, 2.0) + pow(dotVY * ay, 2.0) + pow(dotVN, 2.0)));
}

// GGX清漆几何项
// G GGX function for clearcoat
float G_GGX(float dotVN, float alphag)
{
        float a = alphag * alphag;
        float b = dotVN * dotVN;
        return 1.0 / (dotVN + sqrt(a + b - a * b));
}
float G_SmithGGXCorrealated(float nol,float nov,float alpha_g)
{
	float alpha_g2 = alpha_g * alpha_g;
	float lambda_ggxv = nol * sqrt((-nov * alpha_g2 + nov) * nov + alpha_g2);
	float lambda_ggxl = nov * sqrt((-nol * alpha_g2 + nol) * nol + alpha_g2);
	return 0.5f / (lambda_ggxv + lambda_ggxl);
}
// Tuned to match behavior of Vis_Smith
// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
// 结合G项和brdf的配平因子，省去除法
float Vis_Schlick( float a2, float NoV, float NoL )
{
	float k = sqrt(a2) * 0.5;
	float Vis_SchlickV = NoV * (1 - k) + k;
	float Vis_SchlickL = NoL * (1 - k) + k;
	return 0.25 / ( Vis_SchlickV * Vis_SchlickL );
}
//-------------------------------------------------------------------------------------------------------

//-------------------------------------------------Diffsue------------------------------------------------------
float3 Diffuse_Lambert(float3 diffuse_color)
{
	return diffuse_color * (1 / PI);
}
//相较于lambert，其在边缘较暗
float3 Diffuse_Burley( float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH )
{
	float FD90 = 0.5 + 2 * VoH * VoH * Roughness;
	float FdV = 1 + (FD90 - 1) * Pow5( 1 - NoV );
	float FdL = 1 + (FD90 - 1) * Pow5( 1 - NoL );
	return DiffuseColor * ( (1 / PI) * FdV * FdL );
}
//是对迪士尼漫射的改进，使其能量均衡
float Diffuse_Frostbite(float nov,float nol,float loh,float linear_roughness)
{
	float energy_bias = lerp(0.0,0.5,linear_roughness);
	float energy_factor = lerp(1.0,1.0 / 1.51,linear_roughness);
	float fd90 = energy_bias + 2.0 * loh * loh * linear_roughness;
	float3 f0 = 1.0.xxx;
	float light_scatter = F_Schlick(f0,fd90,nol).r;
	float view_scatter = F_Schlick(f0,fd90,nov).r;
	return light_scatter * view_scatter * energy_factor;
}//-------------------------------------------------Diffsue------------------------------------------------------

float3 CookTorranceBRDF(SurfaceData surface,ShadingData shading_data)
{
	float3 diffuse = Diffuse_Lambert(surface.albedo.rgb);
	//float3 diffuse = Diffuse_Burley(surface.albedo.rgb,surface.roughness,shading_data.nv,shading_data.nl,shading_data.vh);
	float D = D_GTR2(lerp(0.0002,1.0,surface.roughness),shading_data.nh);
	float G = Vis_Schlick(Pow2(0.5 + surface.roughness/2),shading_data.nv,shading_data.nl);
	float3 F = F_Schlick(lerp(F0_AIELECTRICS,surface.albedo.rgb,surface.metallic),shading_data.vh);
	float3 specular = D * G * F;
	float3 kd = (F3_WHITE - F) * (1 - surface.metallic);
	return diffuse * kd + specular;
}


#endif