#ifndef _CS_COMMON_H
#define _CS_COMMON_H
SamplerState g_LinearWrapSampler : register(s0);
SamplerState g_LinearClampSampler : register(s1);
SamplerState g_LinearBorderSampler : register(s2);

#endif //_CS_COMMON_H