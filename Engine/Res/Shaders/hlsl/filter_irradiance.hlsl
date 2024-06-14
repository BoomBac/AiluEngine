//info bein
//pass begin::
//name: filter_irradiance
//vert: VSMain
//pixel: PSMain
//Cull: Front
//Queue: Opaque
//pass end::
//pass begin::
//name: filter_reflection
//vert: VSMain
//pixel: PSMainReflection
//Cull: Front
//Queue: Opaque
//pass end::
//info end

#include "common.hlsli"
#include "brdf.hlsli"
#include "brdf_helper.hlsli"
#include "schlick.brdf"

TextureCube EnvMap : register(t0);

#define CUBEMAP_MIPMAP 7
struct CubeFilterVSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
};

struct CubeFilterPSInput
{
	float4 position : SV_POSITION;
	float3 wnormal : NORMAL;
};

float3 ImportanceSample(float3 N)
{
    float3 V = N;
    float4 result = float4(0,0,0,0);
    uint cubeWidth = 128, cubeHeight = 128;
	static const uint SAMPLE_COUNT = 128u;
    for(uint i = 0; i < SAMPLE_COUNT; i++ )
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = importanceSampleDiffuse( Xi, N);
        float3 L = normalize(2 * dot( V, H ) * H - V);
        float NoL = saturate(dot( N, L ));
        if (NoL > 0.0)
        {
            // Compute Lod using inverse solid angle and pdf.
            // From Chapter 20.4 Mipmap filtered samples in GPU Gems 3.
            // http://http.developer.nvidia.com/GPUGems3/gpugems3_ch20.html
            float pdf = max(0.0, dot(N, L) * INV_PI);
            
            float solidAngleTexel = 4 * PI / (6 * cubeWidth * cubeWidth);
            float solidAngleSample = 1.0 / (SAMPLE_COUNT * pdf);
            float lod = 0.5 * log2((float)(solidAngleSample / solidAngleTexel));

            float3 diffuseSample = EnvMap.SampleLevel(g_AnisotropicClampSampler, L,lod).rgb;
            result = sumDiffuse(diffuseSample, NoL, result);
        }
   }
   if (result.w == 0)
        return result.xyz;
   else   
       return (result.xyz / result.w);
}

float3 FilterCubeMap(float3 normal)
{
	//return ImportanceSample(normalize(normal));
	float3 irradiance = { 0.f, 0.f, 0.f };
	float3 up = { 0.f, 1.f, 0.f };
	float3 right = normalize(cross(up, normal));
	up = normalize(cross(normal, right));
	float nrSamples = 0.0;
	//float sampleDelta = 0.025f;
	static float delta_phi =  DoublePI / 360.0;
	static float delta_thta = HalfPI / 90.0;
	for (float phi = 0.0; phi < DoublePI; phi += delta_phi)
	{
		for (float theta = 0.0; theta < HalfPI; theta += delta_thta)
		{
            // spherical to cartesian (in tangent space)
			float3 tangentSample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            // tangent space to world
			float3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal;
			//不指定lod的话边界会有接缝，原因未知
			irradiance += EnvMap.SampleLevel(g_LinearClampSampler, sampleVec,0).rgb * cos(theta) * sin(theta);
			nrSamples++;
		}
	}
	irradiance = PI * irradiance * (1.0 / float(nrSamples));
	return irradiance;
}


CubeFilterPSInput VSMain(CubeFilterVSInput v)
{
	CubeFilterPSInput result;
	float4x4 mvp = mul(_PassMatrixVP, _MatrixWorld);
	result.position = mul(mvp, float4(v.position, 1.0f));
	result.wnormal = normalize(v.position);
	return result;
}

float4 PSMain(CubeFilterPSInput input) : SV_TARGET
{
	return float4(FilterCubeMap(normalize(input.wnormal)), 1.0);
}

PerMaterialCBufferBegin
	float _roughness;
	float _width;
PerMaterialCBufferEnd

// Based on Karis 2014   UnrealEngine PBR
float3 FilterCubeMapWithRoughness(float3 normal,float roughness,float image_size)
{
	float3 N = normalize(normal);
    float3 R = N;
    float3 V = R;

    static const uint SAMPLE_COUNT = 1024u;
    float totalWeight = 0.0;   
    float3 prefilteredColor = 0.0.xxx;     
    for(uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H  = ImportanceSampleGGX(Xi, N, roughness);
		float vh = dot(V,H);
		float nh = vh;
        float3 L  = normalize(2.0 * vh * H - V);
		float nl = saturate(dot(N,L));
		nh = saturate(nh);
		if(nl > 0.0)
		{
			// Based off https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch20.html
            // Typically you'd have the following:
            // float pdf = D_GGX(NoH, roughness) * NoH / (4.0 * VoH);
            // but since V = N => VoH == NoH
            float pdf = D_GGX(nh, roughness) / 4.0 + 0.001;
            // Solid angle of current sample -- bigger for less likely samples
            float omegaS = 1.0 / (float(SAMPLE_COUNT) * pdf);
            // Solid angle of texel
            float omegaP = 4.0 * PI / (6.0 * image_size * image_size);
            // Mip level is determined by the ratio of our sample's solid angle to a texel's solid angle
            float mipLevel = max(0.5 * log2(omegaS / omegaP), 0.0);
            prefilteredColor += EnvMap.SampleLevel(g_AnisotropicClampSampler, L,mipLevel).rgb * nl;
            totalWeight += nl;
		}
        // float NdotL = max(dot(N, L), 0.0);
        // if(NdotL > 0.0)
        // {
        //     prefilteredColor += EnvMap.Sample(g_LinearWrapSampler, L).rgb * NdotL;
        //     //prefilteredColor += SrcTex.Sample(g_LinearWrapSampler, SampleSphericalMap(L)).rgb * NdotL;
        //     totalWeight      += NdotL;
        // }
    }
    prefilteredColor = prefilteredColor / totalWeight;
    return prefilteredColor;
}

float4 PSMainReflection(CubeFilterPSInput input) : SV_TARGET
{
	if(_roughness == 0)
		return EnvMap.SampleLevel(g_AnisotropicClampSampler,normalize(-input.wnormal),0);
	return float4(FilterCubeMapWithRoughness(normalize(-input.wnormal),_roughness,_width), 1.0);
}
