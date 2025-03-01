//info bein
//pass begin::
//name: editor_outline_gen
//vert: FullscreenVSMain
//pixel: PSMainCopy
//Cull: Back
//Queue: Transparent
//Blend: Src,OneMinusSrc
//Fill: Solid
//ZTest: Always
//ZWrite: Off
//pass end::
//pass begin::
//name: editor_outline_blur
//vert: FullscreenVSMain
//pixel: PSMainBlur
//Cull: Back
//Fill: Solid
//ZTest: Always
//ZWrite: Off
//pass end::
//pass begin::
//name: editor_outline_add
//vert: FullscreenVSMain
//pixel: PSMainAdd
//Cull: Back
//Queue: Transparent
//Blend: Src,OneMinusSrc
//Fill: Solid
//ZTest: Always
//ZWrite: Off
//pass end::
//info end
#include "common.hlsli"
#include "fullscreen_quad.hlsli"

//Texture2D _SourceTex : register(t0);

TEXTURE2D(_SelectBuffer)
PerMaterialCBufferBegin
    float4   _SelectBuffer_TexelSize;
PerMaterialCBufferEnd

static const half2 kOffsets[8] = {
                half2(-1,-1),
                half2(0,-1),
                half2(1,-1),
                half2(-1,0),
                half2(1,0),
                half2(-1,1),
                half2(0,1),
                half2(1,1)
            };

FullScreenPSInput FullscreenVSMain(FullScreenVSInput v);

float4 PSMainBlur(FullScreenPSInput input) : SV_TARGET
{
	half4 col = SAMPLE_TEXTURE2D(_SelectBuffer, g_LinearClampSampler, input.uv);
	//遍历当前像素周边8个像素。
	for (int tap = 0; tap < 8; ++tap)
	{
		col += SAMPLE_TEXTURE2D(_SelectBuffer, g_LinearClampSampler, input.uv + (kOffsets[tap] * _SelectBuffer_TexelSize.xy));
	}
	col /= 9.0;
	return saturate(col * 2);
}



float4 PSMainCopy(FullScreenPSInput input) : SV_TARGET
{
	half4 currentTexel = SAMPLE_TEXTURE2D(_SelectBuffer, g_LinearClampSampler, input.uv);
	float alpha = 0;

	//使用g通道判断前后遮挡关系
	//在上一个shader中，被遮挡的g值为0
	bool isFront = currentTexel.g > 0.0;

	//遍历当前像素周边8个像素。
	for (int tap = 0; tap < 8; ++tap){
		float id = SAMPLE_TEXTURE2D(_SelectBuffer, g_LinearClampSampler, input.uv + (kOffsets[tap] * _SelectBuffer_TexelSize.xy)).b;
		//如果出现b值差（选中物体的渲染区域的b值为1，未渲染的其他地方b值为0）
		//则此像素为“边缘”，进行alpha赋值
		if (id - currentTexel.b != 0)
		{
			alpha = 0.9;
			if(!isFront){
				alpha = 0.3;
			}
		}
	}
	//返回一个颜色，与原本的屏幕颜色进行alpha颜色混合
	float4 UnityOutlineColor = float4(1, 0, 0, alpha);
	return UnityOutlineColor;
}
float4 PSMainAdd(FullScreenPSInput input) : SV_TARGET
{
	half4 currentTexel = SAMPLE_TEXTURE2D(_SelectBuffer, g_LinearClampSampler, input.uv);
	return float4(1,0,0,currentTexel.a);
}