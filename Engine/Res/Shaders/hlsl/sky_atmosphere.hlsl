//info bein
//pass begin::
//name: SkyAtmosphere
//vert: VSMain
//pixel: PSMain
//Cull: Front
//Queue: Opaque
//Stencil: {Ref:127,Comp:NotEqual,Pass:Keep}
//pass end::
//Properties
//{
//	color("Color",Color) = (1,1,1,1)
//}
//info end

#include "common.hlsli"
#include "constants.hlsli"
#include "atmosphere_common.hlsli"


struct VSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float3 wnormal : NORMAL;
};


float3 jodieReinhardTonemap(float3 c)
{
    // From: https://www.shadertoy.com/view/tdSXzD
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    float3 tc = c / (c + 1.0);
    return lerp(c / (l + 1.0), tc, tc);
}

float3 sunWithBloom(float3 rayDir, float3 sunDir) 
{
    const float sunSolidAngle = 0.53*PI/180.0;
    const float minSunCosTheta = cos(sunSolidAngle);

    float cosTheta = dot(rayDir, sunDir);
    if (cosTheta >= minSunCosTheta) return 1.0.xxx;
    
    float offset = minSunCosTheta - cosTheta;
    float gaussianBloom = exp(-offset*50000.0)*0.5;
    float invBloom = 1.0/(0.02 + offset*300.0)*0.01;
    return (gaussianBloom+invBloom).xxx * 4;
}

const static int numScatteringSteps = 16;
float3 raymarchScattering(float3 pos,float3 rayDir,float3 sunDir,float tMax,float numSteps) 
{
    float cosTheta = dot(rayDir, sunDir);
    
    float miePhaseValue = getMiePhase(cosTheta);
    float rayleighPhaseValue = getRayleighPhase(-cosTheta);
    
    float3 lum = 0.0.xxx;
    float3 transmittance = 1.0.xxx;
    float t = 0.0;
    for (float i = 0.0; i < numSteps; i += 1.0) 
    {
        float newT = ((i + 0.3)/numSteps)*tMax;
        float dt = newT - t;
        t = newT;
        
        float3 newPos = pos + t*rayDir;
        
        float3 rayleighScattering, extinction;
        float mieScattering;
        getScatteringValues(newPos, rayleighScattering, mieScattering, extinction);
        
        float3 sampleTransmittance = exp(-dt*extinction);

        float3 sunTransmittance = getValFromTLUT(tLUTRes, newPos, sunDir);
        float3 psiMS = getValFromMultiScattLUT(msLUTRes, newPos, sunDir);
        
        float3 rayleighInScattering = rayleighScattering*(rayleighPhaseValue*sunTransmittance + psiMS);
        float3 mieInScattering = mieScattering*(miePhaseValue*sunTransmittance + psiMS);
        float3 inScattering = (rayleighInScattering + mieInScattering);

        // Integrated scattering within path segment.
        float3 scatteringIntegral = (inScattering - inScattering * sampleTransmittance) / extinction;

        lum += scatteringIntegral*transmittance;
        
        transmittance *= sampleTransmittance;
    }
    return lum;
}

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformToClipSpace(v.position);
	result.position.z = result.position.w;
	//result.wnormal = normalize(mul(_MatrixWorld, float4(v.normal, 0.0f)).xyz);
	result.wnormal = v.position;
	return result;
}
PerMaterialCBufferBegin
	float4 _CameraDir;
PerMaterialCBufferEnd


#define GROUND_COLOR float3(1,1,1)
#define SKY_COLOR float3(0.05,0.14,0.42)

float4 PSMain(PSInput input) : SV_TARGET
{
    //return lerp(GROUND_COLOR,SKY_COLOR,lerp(0.9,1.0,input.wnormal.y)).xyzz * 2.0;
    float3 sunDir = -_MainlightWorldPosition;
    
    float3 rayDir =  normalize(input.wnormal);//normalize(camDir + camRight*xy.x*camWidthScale + camUp*xy.y*camHeightScale);
    
    float3 lum = getValFromSkyLUT(rayDir, sunDir);

    // Bloom should be added at the end, but this is subtle and works well.
    float3 sunLum = sunWithBloom(rayDir, sunDir);
    // Use smoothstep to limit the effect, so it drops off to actual zero.
    sunLum = smoothstep(0.02, 1.0, sunLum);
    if (length(sunLum) > 0.0) {
        if (rayIntersectSphere(viewPos, rayDir, groundRadiusMM) >= 0.0) {
            sunLum *= 0.0;
        } else {
            // If the sun value is applied to this pixel, we need to calculate the transmittance to obscure it.
            sunLum *= getValFromTLUT(tLUTRes, viewPos, sunDir);
        }
    }
    lum += sunLum;
    
    // Tonemapping and gamma. Super ad-hoc, probably a better way to do this.
    lum *= 20.0;
    lum = pow(lum, 1.3.xxx);
    lum /= (smoothstep(0.0, 0.2, clamp(sunDir.y, 0.0, 1.0))*2.0 + 0.15);
    
    lum = jodieReinhardTonemap(lum);
    
    //lum = pow(lum, 0.45.xxx);
    
    // Defining the color variable and returning it.
    half4 customColor;
    customColor = half4(0.5, 0, 0, 1);
    return float4(lum,1.0);
}
