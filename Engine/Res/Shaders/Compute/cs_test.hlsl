#include "cs_common.hlsli"

Texture2D gInputA;
Texture2D gInputB;
RWTexture2D<float3> gOut;

//uint PackRGBA(uint red, uint green, uint blue, uint alpha)
//{
//	return (red << 24) | (green << 16) | (blue << 8) | alpha;
//}


[numthreads(16,16,1)]
void cs_main(uint3 thread_id : SV_DispatchThreadID,uint3 group_thread_id : SV_GroupThreadID,
uint3 group_id : SV_GroupID)
{
	float2 uv = thread_id.xy / 256.0f;
	float3 color = gInputA.SampleLevel(g_LinearClampSampler,uv,0.0f).rgb;
	// //float3 color = gInputA[thread_id.xy * 4].rgb;
	// uint packedR = uint(color.r * 255);
	// uint packedG = uint(color.g * 255) << 8; // shift bits over 8 places
	// uint packedB = uint(color.b * 255) << 16; // shift bits over 16 places
	// uint packedA = uint(255) << 24; // shift bits over 24 places
	// uint packedColor = packedR + packedG + packedB + packedA;
	// //packedColor = gInputA[thread_id.xy];
	// gOut[thread_id.xy] = packedColor; // * gInputA[thread_id.xy] + gInputB[thread_id.xy];
	gOut[thread_id.xy] = color;
}
