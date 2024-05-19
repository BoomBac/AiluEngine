//info bein
//name: bloom
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//info end

Texture2D _SourceTex : register(t0);
SamplerState g_LinearSampler : register(s0);

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
	result.position = float4(v.position.xy, 0.0, 1.0);
	result.uv = v.uv;
	return result;
}

float3 ACESFilm(float3 x)
{
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 color = _SourceTex.Sample(g_LinearSampler, input.uv).rgb;
	return color.xyzz;
	// if (color.r + color.g + color.b > 1.5f)
	// 	return float4(color,1.f); 
	// else
	// 	return float4(0.f, 0.f, 0.f, 1.f);
}
