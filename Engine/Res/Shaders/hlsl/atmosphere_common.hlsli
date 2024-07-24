#ifndef __ATMOSPHERE_COMMON_H__
#define __ATMOSPHERE_COMMON_H__

#include "common.hlsli"
//const static float PI = 3.14159265358;

#define PI 3.14159265358f

// Units are in megameters.
const static float groundRadiusMM = 6.360;
const static float atmosphereRadiusMM = 6.460;

// 200M above the ground.
const static float3 viewPos = float3(0.0, groundRadiusMM + 0.0002, 0.0);

const static float2 tLUTRes = float2(256.0, 64.0);
const static float2 msLUTRes = float2(32.0, 32.0);
// Doubled the vertical skyLUT res from the paper, looks way
// better for sunrise.
const static float2 skyLUTRes = float2(192.0, 192.0);

const static float3 groundAlbedo = float3(0.3,0.3,0.3);

// These are per megameter.
const static float3 rayleighScatteringBase = float3(5.802, 13.558, 33.1);
const static float rayleighAbsorptionBase = 0.0;

const static float mieScatteringBase = 3.996;
const static float mieAbsorptionBase = 4.4;

const static float3 ozoneAbsorptionBase = float3(0.650, 1.881, .085);

/*
 * Animates the sun movement.
 */
// float getSunAltitude(float time)
// {
//     const static float periodSec = 120.0;
//     const static float halfPeriod = periodSec / 2.0;
//     const static float sunriseShift = 0.1;
//     float cyclePoint = (1.0 - abs((mod(time,periodSec)-halfPeriod)/halfPeriod));
//     cyclePoint = (cyclePoint*(1.0+sunriseShift))-sunriseShift;
//     return (0.5*PI)*cyclePoint;
// }

float getMiePhase(float cosTheta) {
    const static float g = 0.8;
    const static float scale = 3.0/(8.0*PI);
    
    float num = (1.0-g*g)*(1.0+cosTheta*cosTheta);
    float denom = (2.0+g*g)*pow(abs(1.0 + g*g - 2.0*g*cosTheta), 1.5);
    
    return scale*num/denom;
}

float getRayleighPhase(float cosTheta) {
    const static float k = 3.0/(16.0*PI);
    return k*(1.0+cosTheta*cosTheta);
}

void getScatteringValues(float3 pos, 
                         out float3 rayleighScattering, 
                         out float mieScattering,
                         out float3 extinction) {
    float altitudeKM = (length(pos)-groundRadiusMM)*1000.0;
    // Note: Paper gets these switched up.
    float rayleighDensity = exp(-altitudeKM/8.0);
    float mieDensity = exp(-altitudeKM/1.2);
    
    rayleighScattering = rayleighScatteringBase*rayleighDensity;
    float rayleighAbsorption = rayleighAbsorptionBase*rayleighDensity;
    
    mieScattering = mieScatteringBase*mieDensity;
    float mieAbsorption = mieAbsorptionBase*mieDensity;
    
    float3 ozoneAbsorption = ozoneAbsorptionBase*max(0.0, 1.0 - abs(altitudeKM-25.0)/15.0);
    
    extinction = rayleighScattering + rayleighAbsorption + mieScattering + mieAbsorption + ozoneAbsorption;
}

float safeacos(const float x) {
    return acos(clamp(x, -1.0, 1.0));
}

// From https://gamedev.stackexchange.com/questions/96459/fast-ray-sphere-collision-code.
float rayIntersectSphere(float3 ro, float3 rd, float rad) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - rad*rad;
    if (c > 0.0f && b > 0.0) return -1.0;
    float discr = b*b - c;
    if (discr < 0.0) return -1.0;
    // Special case: inside sphere, use far discriminant
    if (discr > b*b) return (-b + sqrt(discr));
    return -b - sqrt(discr);
}

TEXTURE2D(_TexTransmittanceLUT,0)
TEXTURE2D(_TexMultScatterLUT,1)

// Texture2D<float4> _TexTransmittanceLUT;
// SamplerState sampler_TexTransmittanceLUT;

// Texture2D<float4> _TexMultScatterLUT;
// SamplerState sampler_TexMultScatterLUT;

/*
 * Same parameterization here.
 */
float3 getValFromTLUT(float2 bufferRes, float3 pos, float3 sunDir) 
{
    float height = length(pos);
    float3 up = pos / height;
	float sunCosZenithAngle = dot(sunDir, up);
    float2 uv = float2(tLUTRes.x*clamp(0.5 + 0.5*sunCosZenithAngle, 0.0, 1.0),
                   tLUTRes.y*max(0.0, min(1.0, (height - groundRadiusMM)/(atmosphereRadiusMM - groundRadiusMM))));
    uv /= bufferRes;
    return SAMPLE_TEXTURE2D_LOD(_TexTransmittanceLUT,g_LinearClampSampler,uv,0).rgb;
}
float3 getValFromMultiScattLUT(float2 bufferRes, float3 pos, float3 sunDir) 
{
    float height = length(pos);
    float3 up = pos / height;
	float sunCosZenithAngle = dot(sunDir, up);
    float2 uv = float2(msLUTRes.x*clamp(0.5 + 0.5*sunCosZenithAngle, 0.0, 1.0),
                   msLUTRes.y*max(0.0, min(1.0, (height - groundRadiusMM)/(atmosphereRadiusMM - groundRadiusMM))));
    uv /= bufferRes;
    return SAMPLE_TEXTURE2D_LOD(_TexMultScatterLUT,g_LinearClampSampler,uv,0).rgb;
}

TEXTURE2D(_TexSkyViewLUT,1)
float3 getValFromSkyLUT(float3 rayDir, float3 sunDir) 
{
    float height = length(viewPos);
    float3 up = viewPos / height;
    
    float horizonAngle = safeacos(sqrt(height * height - groundRadiusMM * groundRadiusMM) / height);
    float altitudeAngle = horizonAngle - acos(dot(rayDir, up)); // Between -PI/2 and PI/2
    float azimuthAngle; // Between 0 and 2*PI
    if (abs(altitudeAngle) > (0.5*PI - .0001)) {
        // Looking nearly straight up or down.
        azimuthAngle = 0.0;
    } else {
        float3 right = cross(sunDir, up);
        float3 forward = cross(up, right);
        
        float3 projectedDir = normalize(rayDir - up*(dot(rayDir, up)));
        float sinTheta = dot(projectedDir, right);
        float cosTheta = dot(projectedDir, forward);
        azimuthAngle = atan2(sinTheta, cosTheta) + PI;
    }
    
    // Non-linear mapping of altitude angle. See Section 5.3 of the paper.
    float v = 0.5 + 0.5*sign(altitudeAngle)*sqrt(abs(altitudeAngle)*2.0/PI);
    float2 uv = float2(azimuthAngle / (2.0*PI), v);
    uv *= skyLUTRes;
    uv /= skyLUTRes;//iChannelResolution[1].xy;
    
    return SAMPLE_TEXTURE2D_LOD(_TexSkyViewLUT,g_LinearClampSampler,uv,0).rgb;
}

#endif//__ATMOSPHERE_COMMON_H__