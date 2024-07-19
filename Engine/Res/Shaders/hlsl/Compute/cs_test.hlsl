#include "cs_common.hlsli"

Texture2D gInputA;
Texture2D gInputB;
RWTexture2D<float3> gOut;


[numthreads(16,16,1)]
void cs_main(uint3 thread_id : SV_DispatchThreadID,uint3 group_thread_id : SV_GroupThreadID,
uint3 group_id : SV_GroupID)
{
	float2 uv = thread_id.xy / 256.0f;
	float3 color = gInputA.SampleLevel(g_LinearClampSampler,uv,0.0f).rgb;
	gOut[thread_id.xy] = color;
}
