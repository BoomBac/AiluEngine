#include "pch.h"
#include "Render/GraphicsContext.h"
#include "RHI/DX12/D3DGPUTimer.h"
#include "RHI/DX12/D3DCommandBuffer.h"
//放到最后，否则编译有报错
#include "RHI/DX12/dxhelper.h"


namespace Ailu
{
#ifndef IID_GRAPHICS_PPV_ARGS
#define IID_GRAPHICS_PPV_ARGS(x) IID_PPV_ARGS(x)
#endif

    namespace
    {
        inline f32 lerp(f32 a, f32 b, f32 f)
        {
            return (1.f - f) * a + f * b;
        }

        inline f32 UpdateRunningAverage(f32 avg, f32 value)
        {
            return lerp(value, avg, 0.95f);
        }

        inline void DebugWarnings(u32 timerid, uint64_t start, uint64_t end)
        {
#if defined(_DEBUG)
            if (!start && end > 0)
            {
                char buff[128] = {};
                sprintf_s(buff, "ERROR: Timer %u stopped but not started\n", timerid);
                OutputDebugStringA(buff);
            }
            else if (start > 0 && !end)
            {
                char buff[128] = {};
                sprintf_s(buff, "ERROR: Timer %u started but not stopped\n", timerid);
                OutputDebugStringA(buff);
            }
#else
            UNREFERENCED_PARAMETER(timerid);
            UNREFERENCED_PARAMETER(start);
            UNREFERENCED_PARAMETER(end);
#endif
        }
    };

    void D3DGPUTimer::BeginFrame(CommandBuffer* cmd)
    {
        auto commandList = static_cast<D3DCommandBuffer*>(cmd)->GetCmdList();
        UNREFERENCED_PARAMETER(commandList);
    }

    void D3DGPUTimer::EndFrame()
    {
        //auto commandList = static_cast<D3DCommandBuffer*>(cmd)->GetCmdList();
        //// Resolve query for the current frame.
        //static UINT resolveToFrameID = 0;
        ////buffer大小为 sizeof(u64) * 3 * c_timerSlots，为每一帧维护c_timerSlots个 u64.
        ////开始查询和结束查询分别进行 query_index 和 query_index + 1两个查询
        //u64 resolveToBaseAddress = resolveToFrameID * c_timerSlots * sizeof(u64);
        ////这里第四个参数应该是实际使用的timer数量 * 2，对没查询索引的解析结果会报错
        //commandList->ResolveQueryData(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, m_buffer.Get(), resolveToBaseAddress);

        // Grab read-back data for the queries from a finished frame m_maxframeCount ago.

        SIZE_T readBackBaseOffset = g_pGfxContext->CurBackbufIndex() * c_timerSlots * sizeof(u64);
        D3D12_RANGE dataRange =
        {
            readBackBaseOffset,
            readBackBaseOffset + c_timerSlots * sizeof(u64),
        };

        u64* timingData;
        ThrowIfFailed(m_buffer->Map(0, &dataRange, reinterpret_cast<void**>(&timingData)));
        memcpy(m_timing, timingData, sizeof(u64) * c_timerSlots);
        //memcpy(timings.data(), timingData, sizeof(u64) * c_timerSlots);
        m_buffer->Unmap(0, nullptr);

        for (u32 j = 0; j < kMaxGpuTimerNum; ++j)
        {
            u64 start = m_timing[j * 2];
            u64 end = m_timing[j * 2 + 1];

            DebugWarnings(j, start, end);

            f32 value = f32(double(end - start) * m_gpuFreqInv);
            m_avg[j] = UpdateRunningAverage(m_avg[j], value);
        }
        //LOG_INFO("GPU Timer: {}", resolveToFrameID);
        //resolveToFrameID = readBackFrameID;
    }

    void D3DGPUTimer::Start(CommandBuffer* cmd, u32 timerid)
    {
        auto commandList = static_cast<D3DCommandBuffer*>(cmd)->GetCmdList();
        if (timerid >= kMaxGpuTimerNum)
            throw std::out_of_range("Timer ID out of range");

        commandList->EndQuery(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timerid * 2);
    }

    void D3DGPUTimer::Stop(CommandBuffer* cmd, u32 timerid)
    {
        auto commandList = static_cast<D3DCommandBuffer*>(cmd)->GetCmdList();
        if (timerid >= kMaxGpuTimerNum)
            throw std::out_of_range("Timer ID out of range");

        const u32 start_query_index = timerid * 2;
        const u32 end_query_index = start_query_index + 1;
        commandList->EndQuery(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, end_query_index);
        // Resolve query for the current frame.
        //static UINT resolveToFrameID = 0;
        //buffer大小为 sizeof(u64) * 3 * c_timerSlots，为每一帧维护c_timerSlots个 u64.
        //开始查询和结束查询分别进行 query_index 和 query_index + 1两个查询
        u64 dst_offset = (g_pGfxContext->CurBackbufIndex() * c_timerSlots + start_query_index) * sizeof(u64);
        //这里第四个参数应该是实际使用的timer数量 * 2，对没查询索引的解析结果会报错
        commandList->ResolveQueryData(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, start_query_index, 2, m_buffer.Get(), dst_offset);
    }

    void D3DGPUTimer::Reset()
    {
        memset(m_avg, 0, sizeof(m_avg));
    }

    f64 D3DGPUTimer::GetElapsedMS(u32 timerid) const
    {
        if (timerid >= kMaxGpuTimerNum)
        {
            g_pLogMgr->LogWarningFormat("Timer ID out of range");
            return 0.0;
        }

        u64 start = m_timing[timerid * 2];
        u64 end = m_timing[timerid * 2 + 1];

        if (end < start)
            return 0.0;

        return f64(end - start) * m_gpuFreqInv;
    }

    void D3DGPUTimer::ReleaseDevice()
    {
        m_heap.Reset();
        m_buffer.Reset();
    }

    void D3DGPUTimer::RestoreDevice(ID3D12Device* device, ID3D12CommandQueue* commandQueue, UINT maxFrameCount)
    {
        m_maxframeCount = maxFrameCount;

        // Filter a debug warning coming when accessing a readback resource for the timing queries.
        // The readback resource handles multiple frames data via per-frame offsets within the same resource and CPU
        // maps an offset written "frame_count" frames ago and the data is guaranteed to had been written to by GPU by this time. 
        // Therefore the race condition doesn't apply in this case.
        ComPtr<ID3D12InfoQueue> d3dInfoQueue;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&d3dInfoQueue))))
        {
            // Suppress individual messages by their ID.
            D3D12_MESSAGE_ID denyIds[] =
            {
                D3D12_MESSAGE_ID_EXECUTECOMMANDLISTS_GPU_WRITTEN_READBACK_RESOURCE_MAPPED,
            };

            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumIDs = _countof(denyIds);
            filter.DenyList.pIDList = denyIds;
            d3dInfoQueue->AddStorageFilterEntries(&filter);
            OutputDebugString(L"Warning: D3DGPUTimer is disabling an unwanted D3D12 debug layer warning: D3D12_MESSAGE_ID_EXECUTECOMMANDLISTS_GPU_WRITTEN_READBACK_RESOURCE_MAPPED.");
        }


        u64 gpuFreq;
        ThrowIfFailed(commandQueue->GetTimestampFrequency(&gpuFreq));
        LOG_WARNING("GPU Freq: {}", gpuFreq);
        m_gpuFreqInv = 1000.0 / double(gpuFreq);

        D3D12_QUERY_HEAP_DESC desc = {};
        desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        desc.Count = c_timerSlots;
        ThrowIfFailed(device->CreateQueryHeap(&desc, IID_GRAPHICS_PPV_ARGS(m_heap.ReleaseAndGetAddressOf())));
        SetName(m_heap.Get(),L"D3DGPUTimerHeap");

        auto readBack = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);

        // We allocate m_maxframeCount + 1 instances as an instance is guaranteed to be written to if maxPresentFrameCount frames
        // have been submitted since. This is due to a fact that Present stalls when none of the m_maxframeCount frames are done/available.
        size_t nPerFrameInstances = m_maxframeCount + 1;

        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(nPerFrameInstances * c_timerSlots * sizeof(u64));
        ThrowIfFailed(device->CreateCommittedResource(
            &readBack,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_GRAPHICS_PPV_ARGS(m_buffer.ReleaseAndGetAddressOf()))
        );
        SetName(m_buffer.Get(), L"D3DGPUTimerBuffer");
    }
}
