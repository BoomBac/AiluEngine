//info bein
//name: skybox_pp
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: LEqual
//ZWrite: Off
//info end
#include "common.hlsli"

struct VSInput
{
	float3 position : POSITION;
	float2 uv : TEXCOORD;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = float4(v.position.xy,1.0, 1.0);
	result.uv = v.uv;
	return result;
}

/*

	Non physical based atmospheric scattering made by robobo1221
	Site: http://www.robobo1221.net/shaders
	Shadertoy: http://www.shadertoy.com/user/robobo1221

*/

static const float pi = 3.14159265359f;
static const float invPi = 1.0f / pi;
static const float zenithOffset = 0.1f;
static const float multiScatterPhase = 0.1f;
static const float density = 0.7f;

static const float anisotropicIntensity = 0.0; //Higher numbers result in more anisotropic scattering
static const float3 skyColor = float3(0.39, 0.57, 1.0) * (1.0 + anisotropicIntensity); //Make sure one of the conponents is never 0.0
static const float2 iResolution = float2(1600,900);

#define smooth(x) x*x*(3.0-2.0*x)
#define zenithDensity(x) density / pow(max(x - zenithOffset, 0.35e-2), 0.75)

float3 getSkyAbsorption(float3 x, float y){
	
	float3 absorption = x * -y;
	     absorption = exp2(absorption) * 2.0;
	
	return absorption;
}

float getSunPoint(float2 p, float2 lp){
	return smoothstep(0.03, 0.026, distance(p, lp)) * 50.0;
}

float getRayleigMultiplier(float2 p, float2 lp){
	return 1.0 + pow(1.0 - clamp(distance(p, lp), 0.0, 1.0), 2.0) * pi * 0.5;
}

float getMie(float2 p, float2 lp){
	float disk = clamp(1.0 - pow(distance(p, lp), 0.1), 0.0, 1.0);
	
	return disk*disk*(3.0 - 2.0 * disk) * 2.0 * pi;
}


float3 getAtmosphericScattering(float2 p, float2 lp)
{
	float2 correctedLp = lp / max(iResolution.x, iResolution.y) * iResolution.xy;
		
	float zenith = zenithDensity(p.y);
	float sunPointDistMult =  clamp(length(max(correctedLp.y + multiScatterPhase - zenithOffset, 0.0)), 0.0, 1.0);
	
	float rayleighMult = getRayleigMultiplier(p, correctedLp);
	
	float3 absorption = getSkyAbsorption(skyColor, zenith);
    float3 sunAbsorption = getSkyAbsorption(skyColor, zenithDensity(correctedLp.y + multiScatterPhase));
	float3 sky = skyColor * zenith * rayleighMult;
	float3 sun = getSunPoint(p, correctedLp) * absorption;
	float3 mie = getMie(p, correctedLp) * sunAbsorption;
	
	float3 totalSky = lerp(sky * absorption, sky / (sky + 0.5), sunPointDistMult);
         totalSky += sun + mie;
	     totalSky *= sunAbsorption * 0.5 + 0.5 * length(sunAbsorption);
	
	return totalSky;
}

float3 jodieReinhardTonemap(float3 c){
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    float3 tc = c / (c + 1.0);

    return lerp(c / (l + 1.0), tc, tc);
}


// float3 ACESFilm(float3 x)
// {
// 	float a = 2.51f;
// 	float b = 0.03f;
// 	float c = 2.43f;
// 	float d = 0.59f;
// 	float e = 0.14f;
// 	return saturate((x*(a*x+b))/(x*(c*x+d)+e));
// }

float3 CalculateSkyColor(float3 cameraForward)
{
    float3 skyDirection = float3(0, 1, 0);
    float angle = dot(cameraForward, skyDirection);
    //float t = saturate(angle * 0.5f + 0.5f);
    float t = angle;
    //float3 skyColor = lerp(float3(0.2, 0.30, 0.96), float3(1.0, 1.0, 1.0), t);
    float3 skyColor = lerp(float3(0.0,0.0,0.0), float3(1.0, 1.0, 1.0), t);
    return skyColor;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	//float2 position = -_MatrixV[2].xy;
	//float2 lightPosition = -_DirectionalLights[0]._LightPosOrDir.xy;
	//float3 color = getAtmosphericScattering(position, lightPosition) * pi;
	//color = jodieReinhardTonemap(color);
	float3 color = lerp(float3(1.0,1.0,1.0),float3(0.2,0.30,0.86),1.0 - input.uv.y);
	return float4(color,1.0);
}
