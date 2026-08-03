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
        u64 VbBindCacheHitCount = 0u;
        u64 VbBindCacheMissCount = 0u;
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
        u32 CommandGroupCount = 0u;
        u32 CommandListCount = 0u;
        u32 CommandSubmitCount = 0u;
        u32 CommandFenceSignalCount = 0u;
        u32 LastCommandSubmissionIndex = 0u;
        f32 CommandRecordingTimeMs = 0.0f;
        f32 CommandSubmissionTimeMs = 0.0f;
        u32 _flag;

        // Properties — set periodically by render thread
        f32 GpuLatency      = 0.0f;
        f32 FrameTime       = 0.0f;
        f32 FrameRate       = 0.0f;
    };

    struct CommandRenderingStatesData
    {
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
        u64 VbBindCacheHitCount = 0u;
        u64 VbBindCacheMissCount = 0u;
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
        u32 CommandGroupCount = 0u;
        u32 CommandListCount = 0u;
        u32 CommandSubmitCount = 0u;
        u32 CommandFenceSignalCount = 0u;
        u32 LastCommandSubmissionIndex = 0u;
        f32 CommandRecordingTimeMs = 0.0f;
        f32 CommandSubmissionTimeMs = 0.0f;
        u32 _flag;

        void Reset() { memset(this, 0, offsetof(CommandRenderingStatesData, _flag)); }

        void Accumulate(const CommandRenderingStatesData &other)
        {
            VertexNum += other.VertexNum;
            TriangleNum += other.TriangleNum;
            DrawCall += other.DrawCall;
            DispatchCall += other.DispatchCall;
            GfxPsoBindCount += other.GfxPsoBindCount;
            GfxResBindCount += other.GfxResBindCount;
            GfxPsoDirtyCount += other.GfxPsoDirtyCount;
            DrawCommandCount += other.DrawCommandCount;
            PsoLookupCount += other.PsoLookupCount;
            PsoCacheHitCount += other.PsoCacheHitCount;
            PsoCacheMissCount += other.PsoCacheMissCount;
            VbBindCacheHitCount += other.VbBindCacheHitCount;
            VbBindCacheMissCount += other.VbBindCacheMissCount;
            MaterialCaptureCount += other.MaterialCaptureCount;
            MaterialBindingResolveCount += other.MaterialBindingResolveCount;
            MaterialBindingCacheHitCount += other.MaterialBindingCacheHitCount;
            PipelineResourceSubmitCount += other.PipelineResourceSubmitCount;
            PipelineResourceOverrideCount += other.PipelineResourceOverrideCount;
            ActualRootSlotBindCount += other.ActualRootSlotBindCount;
            SkippedRootSlotBindCount += other.SkippedRootSlotBindCount;
            MaterialCBufferUploadCount += other.MaterialCBufferUploadCount;
            MaterialCBufferUploadBytes += other.MaterialCBufferUploadBytes;
            MaterialCBufferCacheHitCount += other.MaterialCBufferCacheHitCount;
            ResourceMarkRequestCount += other.ResourceMarkRequestCount;
            UniqueResourceMarkCount += other.UniqueResourceMarkCount;
            CommandGroupCount += other.CommandGroupCount;
            CommandListCount += other.CommandListCount;
            CommandSubmitCount += other.CommandSubmitCount;
            CommandFenceSignalCount += other.CommandFenceSignalCount;
            LastCommandSubmissionIndex = other.LastCommandSubmissionIndex > LastCommandSubmissionIndex
                ? other.LastCommandSubmissionIndex : LastCommandSubmissionIndex;
            CommandRecordingTimeMs += other.CommandRecordingTimeMs;
            CommandSubmissionTimeMs += other.CommandSubmissionTimeMs;
        }

        void MergeTo(RenderingStatesData &data) const
        {
            data.VertexNum += VertexNum;
            data.TriangleNum += TriangleNum;
            data.DrawCall += DrawCall;
            data.DispatchCall += DispatchCall;
            data.GfxPsoBindCount += GfxPsoBindCount;
            data.GfxResBindCount += GfxResBindCount;
            data.GfxPsoDirtyCount += GfxPsoDirtyCount;
            data.DrawCommandCount += DrawCommandCount;
            data.PsoLookupCount += PsoLookupCount;
            data.PsoCacheHitCount += PsoCacheHitCount;
            data.PsoCacheMissCount += PsoCacheMissCount;
            data.VbBindCacheHitCount += VbBindCacheHitCount;
            data.VbBindCacheMissCount += VbBindCacheMissCount;
            data.MaterialCaptureCount += MaterialCaptureCount;
            data.MaterialBindingResolveCount += MaterialBindingResolveCount;
            data.MaterialBindingCacheHitCount += MaterialBindingCacheHitCount;
            data.PipelineResourceSubmitCount += PipelineResourceSubmitCount;
            data.PipelineResourceOverrideCount += PipelineResourceOverrideCount;
            data.ActualRootSlotBindCount += ActualRootSlotBindCount;
            data.SkippedRootSlotBindCount += SkippedRootSlotBindCount;
            data.MaterialCBufferUploadCount += MaterialCBufferUploadCount;
            data.MaterialCBufferUploadBytes += MaterialCBufferUploadBytes;
            data.MaterialCBufferCacheHitCount += MaterialCBufferCacheHitCount;
            data.ResourceMarkRequestCount += ResourceMarkRequestCount;
            data.UniqueResourceMarkCount += UniqueResourceMarkCount;
            data.CommandGroupCount += CommandGroupCount;
            data.CommandListCount += CommandListCount;
            data.CommandSubmitCount += CommandSubmitCount;
            data.CommandFenceSignalCount += CommandFenceSignalCount;
            data.LastCommandSubmissionIndex = LastCommandSubmissionIndex > data.LastCommandSubmissionIndex
                ? LastCommandSubmissionIndex : data.LastCommandSubmissionIndex;
            data.CommandRecordingTimeMs += CommandRecordingTimeMs;
            data.CommandSubmissionTimeMs += CommandSubmissionTimeMs;
        }
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
