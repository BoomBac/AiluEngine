//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

//cbuffer SceneConstantBuffer : register(b0)
//{
//	float4 offset;
//	float4 padding[15];
//};
cbuffer SceneConstantBuffer : register(b0)
{
	float4x4 _MatrixV;
	float4x4 _MatrixVP;
	float padding[32];
};

struct PSInput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
};

PSInput VSMain(float4 position : POSITION,float4 color : COLOR)
{
	PSInput result;

	result.position = mul(position, _MatrixVP);
	result.color = color;
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return input.color;

}
