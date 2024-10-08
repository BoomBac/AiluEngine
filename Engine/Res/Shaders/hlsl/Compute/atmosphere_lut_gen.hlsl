#include "cs_common.hlsli"

#include "../atmosphere_common.hlsli"

#pragma enable_d3d11_debug_symbols
// Each #kernel tells which function to compile; you can have many kernels
#pragma kernel TransmittanceGen
#pragma kernel MultiScattGen
#pragma kernel SkyLightGen
//#pragma kernel SkyLighting

RWTexture2D<float4> _TransmittanceLUT;

#define sunTransmittanceSteps 40.0f

float3 getSunTransmittance(float3 pos, float3 sunDir) 
{
    if (rayIntersectSphere(pos, sunDir, groundRadiusMM) > 0.0) 
    {
        return float3(0.0,0.0,0.0);
    }
    float atmoDist = rayIntersectSphere(pos, sunDir, atmosphereRadiusMM);
    float t = 0.0;
    float3 transmittance = 1.0.xxx;
    for (float i = 0.0; i < sunTransmittanceSteps; i += 1.0) 
    {
        float newT = ((i + 0.3)/sunTransmittanceSteps)*atmoDist;
        float dt = newT - t;
        t = newT;
        
        float3 newPos = pos + t*sunDir;
        
        float3 rayleighScattering, extinction;
        float mieScattering;
        getScatteringValues(newPos, rayleighScattering, mieScattering, extinction);
        
        transmittance *= exp(-dt*extinction);
    }
    return transmittance;
}


//256*64
[numthreads(16,16,1)]
void TransmittanceGen (CSInput input)
{
    float2 uv = (float2)input.DispatchThreadID.xy + 0.5.xx;
    uv /= tLUTRes;
    float sunCosTheta = 2.0*uv.x - 1.0;
    float sunTheta = safeacos(sunCosTheta);
    float height = lerp(groundRadiusMM, atmosphereRadiusMM, uv.y);
    
    float3 pos = float3(0.0, height, 0.0); 
    float3 sunDir = normalize(float3(0.0, sunCosTheta, -sin(sunTheta)));
    _TransmittanceLUT[input.DispatchThreadID.xy] = float4(getSunTransmittance(pos,sunDir),1.0);
}


//----------------------------------------------------------TransmittanceLUT Gen end-----------------------------------------------------------------
const static float mulScattSteps = 20.0f;
//#define sqrtSamples 8
const static int sqrtSamples = 8;

float3 getSphericalDir(float theta, float phi) 
{
     float cosPhi = cos(phi);
     float sinPhi = sin(phi);
     float cosTheta = cos(theta);
     float sinTheta = sin(theta);
     return float3(sinPhi*sinTheta, cosPhi, sinPhi*cosTheta);
}

// Calculates Equation (5) and (7) from the paper.
void getMulScattValues(float3 pos, float3 sunDir, out float3 lumTotal, out float3 fms) 
{
    lumTotal = 0.0.xxx;
    fms = 0.0.xxx;
    
    float invSamples = 1.0/float(sqrtSamples*sqrtSamples);
    for (int i = 0; i < sqrtSamples; i++) {
        for (int j = 0; j < sqrtSamples; j++) 
        {
            // This integral is symmetric about theta = 0 (or theta = PI), so we
            // only need to integrate from zero to PI, not zero to 2*PI.
            float theta = PI * (float(i) + 0.5) / float(sqrtSamples);
            float phi = safeacos(1.0 - 2.0*(float(j) + 0.5) / float(sqrtSamples));
            float3 rayDir = getSphericalDir(theta, phi);
            
            float atmoDist = rayIntersectSphere(pos, rayDir, atmosphereRadiusMM);
            float groundDist = rayIntersectSphere(pos, rayDir, groundRadiusMM);
            float tMax = atmoDist;
            if (groundDist > 0.0) {
                tMax = groundDist;
            }
            
            float cosTheta = dot(rayDir, sunDir);
    
            float miePhaseValue = getMiePhase(cosTheta);
            float rayleighPhaseValue = getRayleighPhase(-cosTheta);
            
            float3 lum = 0.0.xxx, lumFactor = 0.0.xxx, transmittance = 1.0.xxx;
            float t = 0.0;
            for (float stepI = 0.0; stepI < mulScattSteps; stepI += 1.0) {
                float newT = ((stepI + 0.3)/mulScattSteps)*tMax;
                float dt = newT - t;
                t = newT;

                float3 newPos = pos + t*rayDir;

                float3 rayleighScattering, extinction;
                float mieScattering;
                getScatteringValues(newPos, rayleighScattering, mieScattering, extinction);

                float3 sampleTransmittance = exp(-dt*extinction);
                
                // Integrate within each segment.
                float3 scatteringNoPhase = rayleighScattering + mieScattering;
                float3 scatteringF = (scatteringNoPhase - scatteringNoPhase * sampleTransmittance) / extinction;
                lumFactor += transmittance*scatteringF;
                
                // This is slightly different from the paper, but I think the paper has a mistake?
                // In equation (6), I think S(x,w_s) should be S(x-tv,w_s).
                float3 sunTransmittance = getValFromTLUT(tLUTRes, newPos, sunDir);

                float3 rayleighInScattering = rayleighScattering*rayleighPhaseValue;
                float mieInScattering = mieScattering*miePhaseValue;
                float3 inScattering = (rayleighInScattering + mieInScattering)*sunTransmittance;

                // Integrated scattering within path segment.
                float3 scatteringIntegral = (inScattering - inScattering * sampleTransmittance) / extinction;

                lum += scatteringIntegral*transmittance;
                transmittance *= sampleTransmittance;
            }
            
            if (groundDist > 0.0) {
                float3 hitPos = pos + groundDist*rayDir;
                if (dot(pos, sunDir) > 0.0) {
                    hitPos = normalize(hitPos)*groundRadiusMM;
                    lum += transmittance*groundAlbedo*getValFromTLUT(tLUTRes, hitPos, sunDir);
                }
            }
            
            fms += lumFactor*invSamples;
            lumTotal += lum*invSamples;
        }
    }
}

RWTexture2D<float4> _MultScatterLUT;
//32*32
[numthreads(16,16,1)]
void MultiScattGen (CSInput input)
{
    float2 uv = (float2)input.DispatchThreadID.xy + 0.5.xx;
    uv /= (msLUTRes);
    float sunCosTheta = 2.0*uv.x - 1.0;
    float sunTheta = safeacos(sunCosTheta);
    float height = lerp(groundRadiusMM, atmosphereRadiusMM, uv.y);
    
    float3 pos = float3(0.0, height, 0.0); 
    float3 sunDir = normalize(float3(0.0, sunCosTheta, -sin(sunTheta)));
    float3 lum, f_ms;
    getMulScattValues(pos, sunDir, lum, f_ms);
    // Equation 10 from the paper.
    float3 psi = saturate(lum  / (1.0 - f_ms)); 
    _MultScatterLUT[input.DispatchThreadID.xy] = float4(psi,1.0);
}
//----------------------------------------------------------MultScatterLUT Gen end-----------------------------------------------------------------

// Buffer C calculates the actual sky-view! It's a lat-long map (or maybe altitude-azimuth is the better term),
// but the latitude/altitude is non-linear to get more resolution near the horizon.
#define numScatteringSteps 32
float3 raymarchScattering(float3 pos, 
                              float3 rayDir, 
                              float3 sunDir,
                              float tMax,
                              float numSteps) {
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

        lum += saturate(scatteringIntegral*transmittance);
        
        transmittance *= sampleTransmittance;
    }
    return lum;
}

cbuffer AtmosphereCB : register(b0)
{
    float4 _MainLightPosition;
}

float3 getSunDir(float time)
{
    //float altitude = getSunAltitude(time);
    //return normalize(float3(0.0, sin(altitude), -cos(altitude)));
    //return normalize(_SunDir.xyz);
    return -_MainLightPosition.xyz;
}

RWTexture2D<float4> _SkyLightLUT;
[numthreads(16,16,1)]
void SkyLightGen (CSInput input)
{
    float2 uv = (float2)input.DispatchThreadID.xy + 0.5.xx;
    uv /= skyLUTRes;

    float azimuthAngle = (uv.x - 0.5)*2.0*PI;
    // Non-linear mapping of altitude. See Section 5.3 of the paper.
    float adjV;
    if (uv.y < 0.5) {
		float coord = 1.0 - 2.0*uv.y;
		adjV = -coord*coord;
	} else {
		float coord = uv.y*2.0 - 1.0;
		adjV = coord*coord;
	}
    float3 rawSunDir = normalize(getSunDir(0));//normalize(float3(0,0.6523,0.7579));//getSunDir(0);
    //float3 rawSunDir = normalize(float3(0,0.6523,0.7579));//getSunDir(0);
    float height = length(viewPos);
    float3 up = viewPos / height;
    float horizonAngle = safeacos(sqrt(height * height - groundRadiusMM * groundRadiusMM) / height) - 0.5*PI;
    float altitudeAngle = adjV*0.5*PI - horizonAngle;
    
    float cosAltitude = cos(altitudeAngle);
    float3 rayDir = float3(cosAltitude*sin(azimuthAngle), sin(altitudeAngle), -cosAltitude*cos(azimuthAngle));
    float sunAltitude = (0.5*PI) - acos(dot(rawSunDir, up));
    float3 sunDir = float3(0.0, sin(sunAltitude), -cos(sunAltitude));

    float atmoDist = rayIntersectSphere(viewPos, rayDir, atmosphereRadiusMM);
    float groundDist = rayIntersectSphere(viewPos, rayDir, groundRadiusMM);
    float tMax = (groundDist < 0.0) ? atmoDist : groundDist;
    float3 lum = raymarchScattering(viewPos, rayDir, sunDir, tMax, (float)numScatteringSteps);
    _SkyLightLUT[input.DispatchThreadID.xy] = saturate(float4(lum,1.0f));
}