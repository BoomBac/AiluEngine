#pragma once
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

cbuffer SceneConstantBuffer : register(b2)
{
	float4x4 _MatrixV;
	float4x4 _MatrixVP;
//	float padding[32];
};



#endif // !CBUFFER_H__

