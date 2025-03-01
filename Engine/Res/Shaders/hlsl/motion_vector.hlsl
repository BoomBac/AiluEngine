//info bein
//pass begin::
//name: Internal/CameraMotionVector
//vert: FullscreenVSMain
//pixel: CameraMotionVectorPSMain
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Always
//ZWrite: Off
//pass end::
//info end
#include "common.hlsli"
#include "fullscreen_quad.hlsli"

float2 CalculateNdcMotionFormClip(float4 clip_pos_cur, float4 clip_pos_pre)
{
    float2 cur_ndc = clip_pos_cur.xy * rcp(clip_pos_cur.w);
    float2 pre_ndc = clip_pos_pre.xy * rcp(clip_pos_pre.w);
    float2 velocity = cur_ndc - pre_ndc;
    velocity.y = -velocity.y;
    velocity *= 0.5;
    return velocity;
}

//---------------------------------------------object motion vector---------------------------------------------
struct VertInput
{
    float3 position : POSITION;
    float3 position_prev : TEXCOORD0;
#if defined(ALPHA_TEST)
    float2 uv : TEXCOORD1;
#endif
};

struct VertOutput
{
    float4 position : SV_POSITION;
    float4 clip_pos_cur: TEXCOORD1;
	float4 clip_pos_pre: TEXCOORD2;
#if defined(ALPHA_TEST)
    float2 uv : TEXCOORD3;
#endif
};

VertOutput MotionVectorVSMain(VertInput v)
{
    VertOutput result;
    result.position = TransformToClipSpace(v.position);//jitter
	float3 pre_object_pos = v.position;
#if defined(CPU_DEFORM)
	pre_object_pos = v.position_prev;
#endif
	float3 pre_world_pos = TransformPreviousObjectToWorld(pre_object_pos);
    float3 world_pos = TransformObjectToWorld(v.position);
	result.clip_pos_cur = mul(_MatrixVP_NoJitter, float4(world_pos,1.0f));
    result.clip_pos_pre = mul(_MatrixVP_Pre, float4(pre_world_pos,1.0f));
#if defined(ALPHA_TEST)
    result.uv = v.uv;
#endif
    return result;
}

float2 MotionVectorPSMain(VertOutput input) : SV_TARGET
{
    if (_MotionVectorParam.y)
        return float2(0,0);
#if defined(ALPHA_TEST)
	float4 base_color = _SamplerMask & 1? _AlbedoTex.Sample(g_LinearWrapSampler, input.uv) : _AlbedoValue;
	clip(base_color.a - _AlphaCulloff);
#endif
	return CalculateNdcMotionFormClip(input.clip_pos_cur, input.clip_pos_pre);
}
//---------------------------------------------object motion vector---------------------------------------------

FullScreenPSInput FullscreenVSMain(FullScreenVSInput v);
TEXTURE2D(_CameraDepthTexture)

float2 CameraMotionVectorPSMain(FullScreenPSInput input) : SV_TARGET
{
    float2 uv = input.uv;
    float depth = LOAD_TEXTURE2D(_CameraDepthTexture,input.uv * _ScreenParams.zw);
    float3 world_pos = ComputeWorldSpacePosition(uv,depth,_MatrixIVP);
    float4 cur_clip_pos = mul(_MatrixVP_NoJitter, float4(world_pos,1.0f));
    float4 pre_clip_pos = mul(_MatrixVP_Pre, float4(world_pos,1.0f));
	return CalculateNdcMotionFormClip(cur_clip_pos, pre_clip_pos);
}