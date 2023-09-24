#ifndef __CBUFFER_H__
#define __CBUFFER_H__

cbuffer SceneObjectBuffer : register(b0)
{
	float4x4 _MatrixWorld;
}

cbuffer SceneMaterialBuffer : register(b1)
{
	float4 _Color;
//	float padding[32];
};

cbuffer SceneStatetBuffer : register(b2)
{
	float4x4 _MatrixV;
	float4x4 _MatrixP;
	float4x4 _MatrixVP;
	//float padding[16]; // Padding so the constant buffer is 256-byte aligned.
};



#endif // !CBUFFER_H__

