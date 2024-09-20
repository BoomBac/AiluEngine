#ifndef __BRDF_H__
#define __BRDF_H__
#include "constants.hlsli"
//#include "cbuffer.hlsl"
#include "input.hlsli"

#define DIELECTRIC_SPECULAR 0.04
#define F0 float3(0.04,0.04,0.04)
#define FROSTBITE_BRDF

#ifndef MEDIUMP_FLT_MAX
#define MEDIUMP_FLT_MAX    65504.0
#endif //MEDIUMP_FLT_MAX

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
// Anisotropic GGX
// [Burley 2012, "Physically-Based Shading at Disney"]
float D_GGXaniso( float ax, float ay, float NoH, float XoH, float YoH )
{
// The two formulations are mathematically equivalent
#if 1
	float a2 = ax * ay;
	float3 V = float3(ay * XoH, ax * YoH, a2 * NoH);
	float S = dot(V, V);

	return (1.0f / PI) * a2 * pow(a2 / S,2);
#else
	float d = XoH*XoH / (ax*ax) + YoH*YoH / (ay*ay) + NoH*NoH;
	return 1.0f / ( PI * ax*ay * d*d );
#endif
}

// GGX / Trowbridge-Reitz
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float D_GGX( float a2, float NoH )
{
	float d = ( NoH * a2 - NoH ) * NoH + 1;	// 2 mad
	return a2 / ( PI*d*d );					// 4 mul, 1 rcp
}

// Convert a roughness and an anisotropy factor into GGX alpha values respectively for the major and minor axis of the tangent frame
void GetAnisotropicRoughness(float Alpha, float Anisotropy, out float ax, out float ay)
{
#if 1
	// Anisotropic parameters: ax and ay are the roughness along the tangent and bitangent	
	// Kulla 2017, "Revisiting Physically Based Shading at Imageworks"
	ax = max(Alpha * (1.0 + Anisotropy), 0.001f);
	ay = max(Alpha * (1.0 - Anisotropy), 0.001f);
#else
	float K = sqrt(1.0f - 0.95f * Anisotropy);
	ax = max(Alpha / K, 0.001f);
	ay = max(Alpha * K, 0.001f);
#endif
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
float3 F_SchlickRoughness(float cosTheta, float3 f0, float roughness)
{
    return f0 + (max((1.0 - roughness).xxx, f0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
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
// From the filament docs. Geometric Shadowing function
// https://google.github.io/filament/Filament.html#toc4.4.2
// float V_SmithGGXCorrelated(float NoV, float NoL, float roughness) 
// {
//     // float a2 = pow(roughness, 4.0);
//     // float GGXV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
//     // float GGXL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
//     // return 0.5 / (GGXV + GGXL);
// 	//Original formulation of G_SmithGGX Correlated
// 	float NoV2 = NoV * NoV;
// 	float NoL2 = NoL * NoL;
// 	float a2 = pow(roughness, 4.0);
// 	float lambda_v = ( -1 + sqrt ( a2 * (1 - NoL2 ) / NoL2 + 1) ) * 0.5f;
// 	float lambda_l = ( -1 + sqrt ( a2 * (1 - NoV2 ) / NoV2 + 1) ) * 0.5f;
// 	float G_SmithGGXCorrelated = 1 / (1 + lambda_v + lambda_l ) ;
// 	float V_SmithGGXCorrelated = G_SmithGGXCorrelated / (4.0f * NoL * NoV);
// 	return V_SmithGGXCorrelated;
// }
// Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
float V_SmithGGXCorrelated(float NoV, float NoL, float roughness) 
{
    float a2 = roughness * roughness;
    // TODO: lambdaV can be pre-computed for all the lights, it should be moved out of this function
    float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
    float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
    float v = 0.5 / (lambdaV + lambdaL);
    // a2=0 => v = 1 / 4*NoL*NoV   => min=1/4, max=+inf
    // a2=1 => v = 1 / 2*(NoL+NoV) => min=1/4, max=+inf
    // clamp to the maximum value representable in mediump
    return min(v,MEDIUMP_FLT_MAX);
}
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float G_SmithGGX(float nol,float nov, float roughness)
{
    float ggx2 = GeometrySchlickGGX(nov, roughness);
    float ggx1 = GeometrySchlickGGX(nol, roughness);
    return ggx1 * ggx2;
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


//--------------------------------------------------Env----------------------------------------------------------
float3 EnvBRDF(float metallic, float3 base_color, float2 lut)
{
    float3 f0 = lerp(F0, base_color.rgb, metallic);
    return f0 * lut.x + lut.y;
}
//--------------------------------------------------Env----------------------------------------------------------

float3 CookTorranceBRDF(SurfaceData surface,ShadingData shading_data)
{
	float3 diffuse_color = surface.albedo.rgb * (1 - DIELECTRIC_SPECULAR) * (1.0 - surface.metallic);
	float3 diffuse = Diffuse_Lambert(diffuse_color);
	//float3 diffuse = Diffuse_Burley(surface.albedo.rgb,surface.roughness,shading_data.nv,shading_data.nl,shading_data.vh);
	float D1 = D_GTR2(lerp(0.0002,1.0,surface.roughness),shading_data.nh);
	float ax,ay;
	GetAnisotropicRoughness(surface.roughness,surface.anisotropy,ax,ay);
	float D2 = saturate(D_GGXaniso(ax,ay,shading_data.nh,shading_data.th,shading_data.bh));
	float D = lerp(D1,D2,surface.anisotropy);
	//float G = Vis_Schlick(Pow2(0.5 + surface.roughness/2),shading_data.nv,shading_data.nl);
	float G = V_SmithGGXCorrelated(shading_data.nv,shading_data.nl,surface.roughness);
	float3 F = F_Schlick(lerp(DIELECTRIC_SPECULAR.xxx,surface.albedo.rgb,surface.metallic),shading_data.vh);
	float3 specular = D * G * F;
	float3 kd = (F3_WHITE - F) * (1 - surface.metallic);
	return diffuse * kd + specular;
}


#endif