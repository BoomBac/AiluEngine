#pragma warning(push)
#pragma warning(disable: 4251) //std库直接暴露为接口dll在客户端上使用时可能会有问题，禁用该编译警告
#pragma once
#ifndef __TIMEMGR_H__
#define __TIMEMGR_H__
#include <stack>
#include <chrono>
#include "Framework/Interface/IRuntimeModule.h"
#include "Log.h"

namespace Ailu
{
	class AILU_API ModuleTimeStatics
	{
	public:
		inline static float RenderDeltatime = 0.0f;
	};
	class AILU_API TimeMgr : IRuntimeModule
	{
	public:
		using ALSecond = std::chrono::seconds;
		using ALMSecond = std::chrono::duration<float, std::ratio<1, 1000>>;
		using ALTimeStamp = std::chrono::high_resolution_clock::time_point;
		inline static f32 DeltaTime = 0.0f;
		inline static f32 TimeSinceLoad = 0.0f;
		inline static f32 s_smooth_delta_time = 0.0f;
		static f32 s_time_scale;
		static float GetScaledWorldTime(float scale = TimeMgr::s_time_scale,bool smooth_scale = true);
		static String CurrentTime(String format = "%Y-%m-%d_%H:%M:%S");
		//return elapsed time since current tick in ms
        static f32 GetElapsedSinceCurrentTick() { return ALMSecond(std::chrono::high_resolution_clock::now() - s_cur_tick_stamp).count(); };
		int Initialize() final;
        void Finalize() final;
		void Tick(f32 delta_time) final;
		void Pause();
		void Mark();
		float GetElapsedSinceLastMark();
		void Resume();
		void Reset();
	private:
		inline static ALTimeStamp s_pre_tick_stamp;
		inline static ALTimeStamp s_cur_tick_stamp;
		ALTimeStamp _init_stamp;
		ALTimeStamp _pause_stamp;
		std::stack<ALTimeStamp> _mark_stamps{};
		f32 _total_time = 0.0f;
		f32 _total_pause_time = 0.0f;
		f32 _last_pause_time = 0.0f;
		bool _b_stop = false;
	};
	extern TimeMgr* g_pTimeMgr;

	struct TimerBlock
    {
        TimerBlock(const String &msg, TimeMgr *timer = g_pTimeMgr) : _timer(timer), _msg(msg)
        {
            _timer->Mark();
        }
        ~TimerBlock()
        {
			f32 cost = _timer->GetElapsedSinceLastMark();
            if (cost > 1000.0f)
            {
                LOG_INFO("Time cost {}s : {}", cost * 0.001f, _msg);
            }
            else
                LOG_INFO("Time cost {}ms : {}", cost, _msg);
        }
    private:
        TimeMgr *_timer;
		String _msg;
    };
#ifdef _DEBUG
	#define TIMER_BLOCK(msg) TimerBlock tb(msg);
#else
	#define TIMER_BLOCK() 
#endif// _DEBUG

    class CommandBuffer;
	//GPUTimer create by GfxContext
    class IGPUTimer
    {
    public:
        static const u64 kMaxGpuTimerNum = 128u;
        IGPUTimer() = default;
        ~IGPUTimer() = default;

        IGPUTimer(const IGPUTimer&) = delete;
        IGPUTimer& operator=(const IGPUTimer&) = delete;
        IGPUTimer(IGPUTimer&&) = default;
        IGPUTimer& operator=(IGPUTimer&&) = default;

        // Indicate beginning & end of frame
        virtual void BeginFrame(CommandBuffer* cmd) = 0;
        virtual void EndFrame() = 0;
        // Start/stop a particular performance timer (don't start same index more than once in a single frame)
        virtual void Start(CommandBuffer* commandList, u32 timerid = 0) = 0;
        virtual void Stop(CommandBuffer* commandList, u32 timerid = 0) = 0;
        // Reset running average
        virtual void Reset() = 0;
        // Returns delta time in milliseconds
        virtual f64 GetElapsedMS(u32 timerid = 0) const = 0;
        // Returns running average in milliseconds
		virtual f32 GetAverageMS(u32 timerid = 0) const = 0;
        // Device management
		virtual void ReleaseDevice() = 0;
    };
}
#pragma warning(pop)

#endif // !TIMEMGR_H__
