#pragma once
#ifndef __D3DGPUTIMER_H__
#define __D3DGPUTIMER_H__
#include "Framework/Common/TimeMgr.h"
#include "Render/RenderConstants.h"
#include <d3dx12.h>

namespace Ailu
{
    //https://github.com/TheRealMJP/DeferredTexturing/blob/experimental/SampleFramework12/v1.01/Graphics/Profiler.cpp
    class D3DGPUTimer : public IGPUTimer
    {
    public:
        D3DGPUTimer() :
            m_gpuFreqInv(1.f),
            m_avg{},
            m_timing{},
            m_maxframeCount(0)
        {}

        D3DGPUTimer(ID3D12Device* device, ID3D12CommandQueue* commandQueue, UINT maxFrameCount) :
            m_gpuFreqInv(1.f),
            m_avg{},
            m_timing{}
        {
            RestoreDevice(device, commandQueue, maxFrameCount);
        }

        D3DGPUTimer(const D3DGPUTimer&) = delete;
        D3DGPUTimer& operator=(const D3DGPUTimer&) = delete;

        D3DGPUTimer(D3DGPUTimer&&) = default;
        D3DGPUTimer& operator=(D3DGPUTimer&&) = default;

        ~D3DGPUTimer() { ReleaseDevice(); }

        // Indicate beginning & end of frame
        void BeginFrame(CommandBuffer* cmd) final;
        void EndFrame() final;

        // Start/stop a particular performance timer (don't start same index more than once in a single frame)
        void Start(CommandBuffer* cmd, u32 timerid = 0) final;
        void Stop(CommandBuffer* cmd, u32 timerid = 0) final;

        // Reset running average
        void Reset() final;

        // Returns delta time in milliseconds
        f64 GetElapsedMS(u32 timerid = 0) const final;

        // Returns running average in milliseconds
        f32 GetAverageMS(u32 timerid = 0) const final
        {
            return (timerid < kMaxGpuTimerNum) ? m_avg[timerid] : 0.f;
        }

        // Device management
        void ReleaseDevice() final;

        void RestoreDevice(ID3D12Device* device,ID3D12CommandQueue* commandQueue, UINT maxFrameCount);

    private:
        //kMaxGpuTimerNum * kBakcBufferCount
        static const size_t c_timerSlots = kMaxGpuTimerNum * RenderConstants::kFrameCount;

        Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_heap;
        Microsoft::WRL::ComPtr<ID3D12Resource>  m_buffer;
        // clock/per ms
        f64                                  m_gpuFreqInv;
        f32                                   m_avg[kMaxGpuTimerNum];
        //timestamp data
        u64                                  m_timing[c_timerSlots];
        //back buffer num
        u64                                  m_maxframeCount;

    };
}

#endif // !D3DGPUTIMER_H__

