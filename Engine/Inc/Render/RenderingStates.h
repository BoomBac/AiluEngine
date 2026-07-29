#pragma once
#include "Framework/Core/CoreMinimal.h"

namespace Ailu::Render
{
    struct RenderingStatesData
    {
        // Counters — accumulated per frame by render thread
        u32 VertexNum       = 0u;
        u32 TriangleNum     = 0u;
        u32 DrawCall        = 0u;
        u32 DispatchCall    = 0u;
        u32 GfxPsoBindCount = 0u;
        u32 GfxResBindCount = 0u;
        u32 GfxPsoDirtyCount = 0u;
        u64 DrawCommandCount = 0u;
        u64 PsoLookupCount = 0u;
        u64 PsoCacheHitCount = 0u;
        u64 PsoCacheMissCount = 0u;
        u64 MaterialCaptureCount = 0u;
        u64 MaterialBindingResolveCount = 0u;
        u64 MaterialBindingCacheHitCount = 0u;
        u64 PipelineResourceSubmitCount = 0u;
        u64 PipelineResourceOverrideCount = 0u;
        u64 ActualRootSlotBindCount = 0u;
        u64 SkippedRootSlotBindCount = 0u;
        u64 MaterialCBufferUploadCount = 0u;
        u64 MaterialCBufferUploadBytes = 0u;
        u64 MaterialCBufferCacheHitCount = 0u;
        u64 ResourceMarkRequestCount = 0u;
        u64 UniqueResourceMarkCount = 0u;
        u32 _flag;

        // Properties — set periodically by render thread
        f32 GpuLatency      = 0.0f;
        f32 FrameTime       = 0.0f;
        f32 FrameRate       = 0.0f;
    };

    struct AILU_API RenderingStates
    {
        // Render thread writes here (accumulate / set)
        static RenderingStatesData& RenderData()  { return s_render_data; }

        // Main thread reads here (snapshot from last Reset)
        static const RenderingStatesData& DisplayData() { return s_display_data; }

        // Called at frame boundary: snapshots render data for display, then zeros counters
        static void Reset()
        {
            s_display_data = s_render_data;
            memset(&s_render_data,0,offsetof(RenderingStatesData,_flag));
        }
    private:
        inline static RenderingStatesData s_render_data;
        inline static RenderingStatesData s_display_data;
    };
}
