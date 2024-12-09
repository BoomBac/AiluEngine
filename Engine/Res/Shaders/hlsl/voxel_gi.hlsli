#ifndef __VXGI_H__
#define __VXGI_H__
#include "cbuffer.hlsli"
#include "common.hlsli"

uint3 FromLinearIndex(uint id,uint3 dim)
{
	uint3 index;
	index.z = id / (dim.x * dim.y);
	index.y = (id % (dim.x * dim.y)) / dim.x;
	index.x = id % dim.x;
	return index;
}
uint ToLinearIndex(uint3 pos,uint3 dim)
{
	return pos.z * dim.x * dim.y + pos.y * dim.x + pos.x;
}

float3 Orthogonal(float3 u)
{
	u = normalize(u);
	float3 v = float3(0.99146, 0.11664, 0.05832); // Pick any normalized vector.
	return abs(dot(u, v)) > 0.99999f ? cross(u, float3(0, 1, 0)) : cross(u, v);
}

TEXTURE3D(_VoxelLightTex)

float3 WorldToVoexlUV(float3 world_pos)
{
	float3 origin = g_VXGI_Center.xyz - g_VXGI_Size.xyz * 0.5f;
	float3 pos_voxel_space = world_pos - origin;
	return pos_voxel_space / g_VXGI_Size.xyz;
}
// Returns true if the point p is inside the unity cube. 
bool IsInsideCube(const float3 p, float e) 
{ 
	return abs(p.x) < 1 + e && abs(p.y) < 1 + e && abs(p.z) < 1 + e; 
}


#define ToRadius 0.0072
#define _StepCount 32
#define _Mipmap 0
#define _ONE_RAY_ON
#define _ConeAngle 90
#define _StepLength 0.5
#define kMaxRayLength g_VXGI_Center.w
#define kStartOffset  g_VXGI_GridSize.w
#define kConeSpread   g_VXGI_Size.w

#define HIGH_QUALITY 1

#define TSQRT2 2.828427
#define SQRT2 1.414213
#define ISQRT2 0.707106
#define VOXEL_SIZE (1/64.0) /* Size of a voxel. 128x128x128 => 1/128 = 0.0078125. */

// Scales and bias a given vector (i.e. from [-1, 1] to [0, 1]).
float3 ScaleAndBias(const float3 p) { return 0.5f * p + 0.5f.xxx; }

float ConeTraceShadow(float3 start,float3 dir)
{
	float occ = 0.f;
	dir = normalize(dir);
	const float voexl_texel_size = 1.0f / g_VXGI_GridNum.x;
	float3 start_uvw = WorldToVoexlUV(start);
	float dis = 3 * voexl_texel_size;
	
	while(dis < kMaxRayLength * 2 && occ < 1)
	{
		float3 c = start_uvw + dis * dir;
		if (!IsInsideCube(c,0))
			break;
		float l = pow(dis,2);
		float s1 = 0.82 * SAMPLE_TEXTURE3D_LOD(_VoxelLightTex,g_LinearBorderSampler,c,l + 0.75 * l).a;
		float s2 = 0;//0.135 * SAMPLE_TEXTURE3D_LOD(_VoxelLightTex,g_LinearBorderSampler,c,2.5 * l).a;
		float s = s1 + s2;
		occ += (1-occ) * s;
		dis += 0.2 * voexl_texel_size * (1+0.05*l);
	}
	return 1 - occ;//pow(smoothstep(0, 1, occ * 1.4), 1.0 / 1.4);
}

float3 ConeTrace(float3 start,float3 dir)
{
	dir = normalize(dir);
	float4 color = 0;
	float3 start_uvw = WorldToVoexlUV(start);
	const float voexl_texel_size = 1.0f / g_VXGI_GridNum.x;
	// Controls bleeding from close surfaces.
	// Low values look rather bad if using shadow cone tracing.
	// Might be a better choice to use shadow maps and lower this value.
	float dis = kStartOffset;
	int setp_count = 0;
	
	while(dis < kMaxRayLength && color.a < 1)
	{
		float3 c = start_uvw + dis * dir;
		//c = ScaleAndBias(c);
		float l = 0.2 + (kConeSpread * dis / voexl_texel_size);
		float level = log2(l);
		float ll = level;
		ll = min(ll,5.4);
		ll = max(0.2,ll);
		float4 gi = SAMPLE_TEXTURE3D_LOD(_VoxelLightTex,g_LinearClampSampler,c,ll);
		// color += 0.275 * ll * gi * pow(1 - gi.a,2);
		// dis += ll * voexl_texel_size * 2;
		float a = 1 - color.a;
		float atten = 1;//saturate(0.5 - dis) / 0.5f;
		color.rgb += a * gi.rgb * atten;
		color.a += a * gi.a;
		dis += (ll+1) * voexl_texel_size;
		++setp_count;
	}
	return color.rgb;
}
static const float3 sphere_dir[4] = 
{
	float3(1,1,0),
	float3(-1,1,0),
	float3(0,1,1),
	float3(0,1,-1)
};

float3 VoxelGI(float3 world_pos,float3 normal)
{
	float3 ray_start = world_pos;
	// +
	float3 tangent = normalize(Orthogonal(normal));
	float3 bitangent = normalize(cross(tangent,normal));
	const static float kAngleMix = 0.5;
	//对角的两个轴x
	float3 corner = 0.5f * (tangent + bitangent);
	float3 corner2 = 0.5f * (tangent - bitangent);
// Find start position of trace (start with a bit of offset).
	const float3 N_OFFSET = normal * (1 + 4 * ISQRT2) * VOXEL_SIZE;
	const float3 C_ORIGIN = world_pos + N_OFFSET;

	float3 acc_gi = 0;
	// We offset forward in normal direction, and backward in cone direction.
	// Backward in cone direction improves GI, and forward direction removes
	// artifacts.
	const float CONE_OFFSET = -0.02;
	//acc_gi += ConeTrace(C_ORIGIN + CONE_OFFSET * normal,normal);
	acc_gi += ConeTrace(world_pos,normal);
	const float weight = 1; // cos(45);
	float3 axis0 = lerp(normal, tangent, kAngleMix);
	float3 axis1 = lerp(normal, -tangent, kAngleMix);
	float3 axis2 = lerp(normal, bitangent, kAngleMix);
	float3 axis3 = lerp(normal, -bitangent, kAngleMix);
	acc_gi += weight * ConeTrace(C_ORIGIN + CONE_OFFSET * tangent, axis0);
	acc_gi += weight * ConeTrace(C_ORIGIN - CONE_OFFSET * tangent, axis1);
	acc_gi += weight * ConeTrace(C_ORIGIN + CONE_OFFSET * bitangent, axis2);
	acc_gi += weight * ConeTrace(C_ORIGIN - CONE_OFFSET * bitangent, axis3);
#if defined(HIGH_QUALITY)
	float div = 9;
	//Trace 4 corner cones.
	float3 c1 = lerp(normal, corner,   1.15 * kAngleMix);
	float3 c2 = lerp(normal, -corner,  1.15 * kAngleMix);
	float3 c3 = lerp(normal, corner2,  1.15 * kAngleMix);
	float3 c4 = lerp(normal, -corner2, 1.15 * kAngleMix);
	acc_gi += weight * ConeTrace(C_ORIGIN + CONE_OFFSET * corner, normalize(c1));
	acc_gi += weight * ConeTrace(C_ORIGIN - CONE_OFFSET * corner, normalize(c2));
	acc_gi += weight * ConeTrace(C_ORIGIN + CONE_OFFSET * corner2, normalize(c3));
	acc_gi += weight * ConeTrace(C_ORIGIN - CONE_OFFSET * corner2, normalize(c4));
#else
	float div = 5;
#endif
	acc_gi /= div;
	return acc_gi;
}
#endif