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
#define AL_SCENE_PRIMITIVE 1
#include "common.hlsli"
#include "primitive.hlsli"
#include "fullscreen_quad.hlsli"

float3 CalculateNdcMotionFormClip(float4 clip_pos_cur, float4 clip_pos_pre)
{
    clip_pos_cur *= rcp(clip_pos_cur.w);
    clip_pos_pre *= rcp(clip_pos_pre.w);
    float2 cur_ndc = clip_pos_cur.xy;
    float2 pre_ndc = clip_pos_pre.xy;
    float2 velocity = cur_ndc - pre_ndc;
    velocity.y = -velocity.y;
    velocity *= 0.5;
    float depth = Linear01Depth(clip_pos_cur.z, _ProjectionParams.y, _ProjectionParams.z);
    depth -= Linear01Depth(clip_pos_pre.z, _ProjectionParams.y, _ProjectionParams.z);
    return float3(velocity, depth);
}

//---------------------------------------------object motion vector---------------------------------------------
struct VertInput
{
	float3 position : POSITION;
	uint instance_id : SV_INSTANCEID;
#if defined(CPU_DEFORM)
    float3 position_prev : TEXCOORD0;
#endif
#if defined(ALPHA_TEST)
    float2 uv : TEXCOORD1;
#endif
};

struct VertOutput
{
    float4 position : SV_POSITION;
    float4 clip_pos_cur: TEXCOORD1;
	float4 clip_pos_pre: TEXCOORD2;
	nointerpolation uint primitive_flags : TEXCOORD4;
#if defined(ALPHA_TEST)
    float2 uv : TEXCOORD3;
#endif
};

VertOutput MotionVectorVSMain(VertInput v)
{
	VertOutput result;
	const PrimitiveData primitive = LoadPrimitive(v.instance_id);
	result.position = TransformPrimitiveToClipSpace(primitive, v.position);//jitter
	float3 pre_object_pos = v.position;
#if defined(CPU_DEFORM)
	pre_object_pos = v.position_prev;
#endif
	float3 pre_world_pos = TransformPrimitivePreviousToWorld(primitive, pre_object_pos);
    float3 world_pos = TransformPrimitiveToWorld(primitive, v.position);
	result.clip_pos_cur = mul(_MatrixVP_NoJitter, float4(world_pos,1.0f));
	result.clip_pos_pre = mul(_MatrixVP_Pre, float4(pre_world_pos,1.0f));
	result.primitive_flags = primitive._flags;
#if defined(ALPHA_TEST)
    result.uv = v.uv;
#endif
    return result;
}

float4 MotionVectorPSMain(VertOutput input) : SV_TARGET
{
    if ((input.primitive_flags & kPrimitiveForceZeroMotion) != 0u)
        return float4(0,0,0,0);
#if defined(ALPHA_TEST)
	float4 base_color = _SamplerMask & 1? _AlbedoTex.Sample(g_LinearWrapSampler, input.uv) : _AlbedoValue;
	clip(base_color.a - _AlphaCulloff);
#endif
	return float4(CalculateNdcMotionFormClip(input.clip_pos_cur, input.clip_pos_pre), 1.0);
}
//---------------------------------------------object motion vector---------------------------------------------

FullScreenPSInput FullscreenVSMain(FullScreenVSInput v);
TEXTURE2D(_CameraDepthTexture)

float4 CameraMotionVectorPSMain(FullScreenPSInput input) : SV_TARGET
{
    float2 uv = input.uv;
    float depth =  SAMPLE_TEXTURE2D(_CameraDepthTexture,g_PointClampSampler,uv).r;
    float3 world_pos = ComputeWorldSpacePosition(uv,depth,_MatrixIVP);
    float4 cur_clip_pos = mul(_MatrixVP_NoJitter, float4(world_pos,1.0f));
    float4 pre_clip_pos = mul(_MatrixVP_Pre, float4(world_pos,1.0f));
	return float4(CalculateNdcMotionFormClip(cur_clip_pos, pre_clip_pos), 1.0);
}
