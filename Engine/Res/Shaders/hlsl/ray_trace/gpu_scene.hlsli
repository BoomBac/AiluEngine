#ifndef __GPU_SCENE__
#define __GPU_SCENE__
#include "../../../../Inc/Render/ShaderInterop.h"

StructuredBuffer<ObjectInstanceData> g_instance_data : register(t1);
StructuredBuffer<UnifiedLightData> _UnifiedLights : register(t2);

StructuredBuffer<TriangleData> g_scene;
StructuredBuffer<LBVHNode> g_tlas_buffer;
StructuredBuffer<LBVHNode> g_blas_buffer;

#endif