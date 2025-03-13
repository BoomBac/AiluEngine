#pragma once
#ifndef __PROFILER_H__
#define __PROFILER_H__
#include "GlobalMarco.h"
#include "TimeMgr.h"
#include "Container.hpp"

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
		f64 _start_time_since_cur_frame = 0.0;

		f64 TimeSamples[FilterSize] = { };
		u64 CurrSample = 0;
	};

	struct ProfileFrameData
	{
		struct ProfileEditorData
		{
			String _name;
			f32 _start_time;
			f32 _duration;
			u16 _call_depth;
			ProfileEditorData(const String& name, f32 start_time, f32 duration, u16 call_depth) : _name(name), _start_time(start_time), _duration(duration), _call_depth(call_depth) {}
		};
		u64 _frame_index;
		f32 _start_time;
		f32 _end_time;
		Map<std::thread::id,List<ProfileEditorData>> _profile_data_list;
	};
	class CommandBuffer;
	class AILU_API Profiler
	{
		friend class ProfileBlock;
		friend class CPUProfileBlock;
	public:
		inline static constexpr u64 kMaxProfileNum = IGPUTimer::kMaxGpuTimerNum;
		inline static constexpr u32 kMaxCacheDataNum = 300;
		
		static Profiler& Get();
		static void Initialize();
		static void Shutdown();

		u64 StartProfile(CommandBuffer * cmdList, const String& name);
		void EndProfile(CommandBuffer * cmdList, u64 idx);

		u64 StartCPUProfile(const String& name);
		void EndCPUProfile(u64 idx);

		ProfileData* GetCPUProfileData(u64 idx) {return &cpuProfiles[idx];};
		ProfileData* GetProfileData(u64 idx) {return &profiles[idx];};
		ProfileFrameData GetLastFrameData()
		{
			return _cache_data.back();
		}
		void EndFrame();

		List<std::tuple<bool, u32,std::thread::id>> _cpu_profiler_queue;
		std::queue<std::tuple<bool, u32>> _gpu_profiler_queue;
	private:
		void AddCPUProfilerHierarchy(bool begin_profiler, u32 index)
		{
            std::lock_guard<std::mutex> lock(_lock);
			_cpu_profiler_queue.push_back(std::make_tuple(begin_profiler, index,std::this_thread::get_id()));
		}
		void AddGPUProfilerHierarchy(bool begin_profiler, u32 index)
		{
            std::lock_guard<std::mutex> lock(_lock);
			_gpu_profiler_queue.push(std::make_tuple(begin_profiler, index));
		}
	private:
		Array<ProfileData,kMaxProfileNum> profiles;
		Array<ProfileData,kMaxProfileNum> cpuProfiles;
		u64 numProfiles = 0;
		u64 numCPUProfiles = 0;
		TimeMgr _cpu_timer;
		IGPUTimer* _p_gpu_timer = nullptr;
        std::mutex _lock;
		std::deque<ProfileFrameData> _cache_data;
	};

	//command must been execute out of prifile scope
    class AILU_API ProfileBlock
	{
	public:

		ProfileBlock(CommandBuffer * cmdList, const String& name);
		~ProfileBlock();

	protected:
        CommandBuffer * cmdList = nullptr;
		u64 idx = u64(-1);
	};

	class AILU_API CPUProfileBlock
	{
	public:

		explicit CPUProfileBlock(const String& name);
		~CPUProfileBlock();

	protected:
		u64 idx = u64(-1);
	};
#define PROFILE_BLOCK_GPU(cmd, block_name) ProfileBlock block_name##_GPU(cmd, #block_name);
#define PROFILE_BLOCK_CPU(block_name) CPUProfileBlock block_name##_CPU(#block_name);
}


#endif // !PROFILER_H__

