//info bein
//pass begin::
//name: voxel_drawer
//vert: ForwardVSMain
//pixel: ForwardPSMain
//Cull: Back
//ZWrite: On
//Fill: Solid
//Queue: Transparent
//Blend: Src,OneMinusSrc
//pass end::
//info end

#include "common.hlsli"
#include "voxel_gi.hlsli"

TEXTURE3D(_VoxelSrc);

PerMaterialCBufferBegin
	uint4  _GridNum;
	float4 _GridSize;
	float4 _GridCenter;
	uint   _DebugMip;
PerMaterialCBufferEnd

struct VSInput
{
	float3 position : POSITION;
	uint inst_id : SV_INSTANCEID;
};
struct PSInput
{
	float4 position : SV_POSITION;
	nointerpolation float3 uvw : TEXCOORD0;
};

PSInput ForwardVSMain(VSInput v)
{
	PSInput result;
	v.position *= _GridSize * 0.5f;
	float3 origin = _GridCenter.xyz - float3(_GridNum.xyz)*0.5 * _GridSize.xyz + _GridSize.xyz * 0.5;
	float3 obj_pos = v.position + origin;
	float3 pos = (float3)FromLinearIndex(v.inst_id,_GridNum.xyz);
	obj_pos += pos * _GridSize.xyz;
	result.position = TransformToClipSpaceNoJitter(obj_pos);
	result.uvw = pos;
	return result;
}
struct VoxelOut
{
	float4 color : SV_Target;
	//float depth : SV_Depth;
}; 
VoxelOut ForwardPSMain(PSInput input)
{
	VoxelOut ret;
	ret.color = LOAD_TEXTURE3D_LOD(_VoxelSrc,input.uvw,_DebugMip);
	clip(ret.color.a - 0.2);
	//ret.depth = input.color.a > 0.5f? input.position.z : 1.0f;
	return ret;
}