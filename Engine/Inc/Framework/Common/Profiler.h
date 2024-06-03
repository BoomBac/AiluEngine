#pragma once
#ifndef __PROFILER_H__
#define __PROFILER_H__
#include "GlobalMarco.h"
#include "TimeMgr.h"

namespace Ailu
{
	struct ProfileData
	{
		static const u64 FilterSize = 64;
		String Name;

		bool QueryStarted = false;
		bool QueryFinished = true;
		bool Active = false;

		bool CPUProfile = false;
		f64 _time = 0.0;
		f64 _max_time = 0.0;
		f64 _avg_time = 0.0;

		f64 TimeSamples[FilterSize] = { };
		u64 CurrSample = 0;
	};
	class CommandBuffer;
	class AILU_API Profiler
	{
		friend class ProfileBlock;
		friend class CPUProfileBlock;
	public:
		static Profiler g_Profiler;
		inline static constexpr u64 kMaxProfileNum = IGPUTimer::kMaxGpuTimerNum;

		void Initialize();
		void Shutdown();

		u64 StartProfile(CommandBuffer* cmdList, const String& name);
		void EndProfile(CommandBuffer* cmdList, u64 idx);

		u64 StartCPUProfile(const String& name);
		void EndCPUProfile(u64 idx);

		ProfileData* GetCPUProfileData(u64 idx) {return &cpuProfiles[idx];};
		ProfileData* GetProfileData(u64 idx) {return &profiles[idx];};
		void EndFrame();

		std::queue<std::tuple<bool, u32>> _cpu_profiler_queue;
		std::queue<std::tuple<bool, u32>> _gpu_profiler_queue;
	private:
		void AddCPUProfilerHierarchy(bool begin_profiler, u32 index)
		{
			_cpu_profiler_queue.push(std::make_tuple(begin_profiler, index));
		}
		void AddGPUProfilerHierarchy(bool begin_profiler, u32 index)
		{
			_gpu_profiler_queue.push(std::make_tuple(begin_profiler, index));
		}
	protected:
		Array<ProfileData,kMaxProfileNum> profiles;
		Array<ProfileData,kMaxProfileNum> cpuProfiles;
		u64 numProfiles = 0;
		u64 numCPUProfiles = 0;
		TimeMgr _cpu_timer;
		IGPUTimer* _p_gpu_timer = nullptr;
	private:
	};

	class ProfileBlock
	{
	public:

		ProfileBlock(CommandBuffer* cmdList, const String& name);
		~ProfileBlock();

	protected:

		CommandBuffer* cmdList = nullptr;
		u64 idx = u64(-1);
	};

	class CPUProfileBlock
	{
	public:

		CPUProfileBlock(const String& name);
		~CPUProfileBlock();

	protected:
		u64 idx = u64(-1);
	};

}


#endif // !PROFILER_H__

