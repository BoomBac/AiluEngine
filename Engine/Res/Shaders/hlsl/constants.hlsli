#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__

#define MAX_DIRECTIONAL_LIGHT	2
#define MAX_POINT_LIGHT kMaxPointLight
#define MAX_SPOT_LIGHT	4
#define MAX_AREA_LIGHT	4

#define kRenderFeatureStaticDiffuseGi 1u
#define kRenderFeatureReflectionProbe 2u
#define kRenderFeatureEnvironmentIbl 4u

#define F3_WHITE float3(1.0,1.0,1.0)

#define PI       3.1415926535f
#define INV_PI   0.31830988618379067239521257108191f
#define DoublePI 6.283185307f
#define HalfPI   1.57079632675f
#define ToRadius 0.01745f
#define ToAngle  57.29582f

#define FLOAT_EPSILON 1e-6

#define UINT32_MAX 0xffffffff

#endif //__CONSTANTS_H__
