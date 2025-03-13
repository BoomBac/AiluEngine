#include "pch.h"
#include "Framework/Common/Profiler.h"
#include "Render/GraphicsContext.h"

namespace Ailu
{
	static Profiler g_Profiler;
	Profiler& Profiler::Get()
	{
		return g_Profiler;
	}

	void Profiler::Initialize()
	{
		g_Profiler._p_gpu_timer = g_pGfxContext->GetTimer();
	}
	void Profiler::Shutdown()
	{
	}
	u64 Profiler::StartProfile(CommandBuffer * cmdList, const String& name)
	{
        std::lock_guard<std::mutex> lock(_lock);
		u64 profileIdx = u64(-1);
		for (u64 i = 0; i < numProfiles; ++i)
		{
			if (profiles[i].Name == name)
			{
				profileIdx = i;
				break;
			}
		}
		if (profileIdx == u64(-1))
		{
			AL_ASSERT(numProfiles < kMaxProfileNum);
			profileIdx = numProfiles++;
			profiles[profileIdx].Name = name;
		}
		ProfileData& profile_data = profiles[profileIdx];
		AL_ASSERT(profile_data.QueryStarted != true);
		AL_ASSERT(profile_data.QueryFinished != false);
		profile_data.CPUProfile = false;
		profile_data.Active = true;
		_p_gpu_timer->Start(cmdList, profileIdx);
		profile_data.QueryStarted = true;
		profile_data.QueryFinished = false;
		profile_data._start_time_since_cur_frame = g_pTimeMgr->GetElapsedSinceCurrentTick();
		return profileIdx;
	}
	void Profiler::EndProfile(CommandBuffer * cmdList, u64 idx)
	{
        std::lock_guard<std::mutex> lock(_lock);
		AL_ASSERT(idx < numProfiles);
		ProfileData& profile_data = profiles[idx];
		AL_ASSERT(profile_data.QueryStarted != false);
		AL_ASSERT(profile_data.QueryFinished != true);
		_p_gpu_timer->Stop(cmdList, idx);
		profile_data._time = _p_gpu_timer->GetElapsedMS(idx);
		profile_data.QueryStarted = false;
		profile_data.QueryFinished = true;
	}
	u64 Profiler::StartCPUProfile(const String& name)
	{
        std::lock_guard<std::mutex> lock(_lock);
		u64 profileIdx = u64(-1);
		for (u64 i = 0; i < numCPUProfiles; ++i)
		{
			if (cpuProfiles[i].Name == name)
			{
				profileIdx = i;
				break;
			}
		}
		if (profileIdx == u64(-1))
		{
			AL_ASSERT(numCPUProfiles < kMaxProfileNum);
			profileIdx = numCPUProfiles++;
			profiles[profileIdx].Name = name;
		}
		ProfileData& profile_data = cpuProfiles[profileIdx];
		AL_ASSERT(profile_data.QueryStarted != true);
		AL_ASSERT(profile_data.QueryFinished != false);
		profile_data.Name = name;
		profile_data.CPUProfile = true;
		profile_data.Active = true;
		g_pTimeMgr->Mark();
		//g_pTimeMgr _cpu_timer.Mark();
		profile_data.QueryStarted = true;
		profile_data.QueryFinished = false;
		profile_data._start_time_since_cur_frame = g_pTimeMgr->GetElapsedSinceCurrentTick();
        //LOG_INFO("Start CPU Profile: {} at frame {} with name {}", profileIdx,g_pGfxContext->GetFrameCount(),name);
		return profileIdx;
	}
	void Profiler::EndCPUProfile(u64 idx)
	{
        std::lock_guard<std::mutex> lock(_lock);
		AL_ASSERT(idx < numCPUProfiles);
		ProfileData& profile_data = cpuProfiles[idx];
		AL_ASSERT(profile_data.QueryStarted != false);
		AL_ASSERT(profile_data.QueryFinished != true);
		profile_data._time = g_pTimeMgr->GetElapsedSinceLastMark();
		profile_data.QueryStarted = false;
		profile_data.QueryFinished = true;
        //LOG_INFO("Stop CPU Profile: {} at frame {}", idx,g_pGfxContext->GetFrameCount());
	}
	void Profiler::EndFrame()
	{
		static auto update_profile = [](ProfileData& profile) 
		{
			profile.TimeSamples[profile.CurrSample] = profile._time;
			profile.CurrSample = (profile.CurrSample + 1) % ProfileData::FilterSize;
			f64 maxTime = 0.0;
			f64 avgTime = 0.0;
			u64 avgTimeSamples = 0;
			for (UINT i = 0; i < ProfileData::FilterSize; ++i)
			{
				if (profile.TimeSamples[i] <= 0.0)
					continue;
				maxTime = std::max<f64>(profile.TimeSamples[i], maxTime);
				avgTime += profile.TimeSamples[i];
				++avgTimeSamples;
			}

			if (avgTimeSamples > 0)
				avgTime /= f64(avgTimeSamples);
			profile._avg_time = avgTime;
			profile._max_time = maxTime;
			profile.Active = false;
		};
		for (int i = 0; i < numCPUProfiles; i++)
		{
			update_profile(cpuProfiles[i]);
		}
		for (int i = 0; i < numProfiles; i++)
		{
			update_profile(profiles[i]);
		}
		while (_cache_data.size() > kMaxCacheDataNum)
			_cache_data.pop_front();
		ProfileFrameData frame_data;
		frame_data._frame_index = g_pGfxContext->GetFrameCount();
		frame_data._start_time = 1.0f;
		frame_data._end_time = 1.0f;
		Map<std::thread::id,List<std::tuple<bool,u32>>> cpu_profiler_queue_per_threads;
		for(auto& it : _cpu_profiler_queue)
		{
			auto& [is_start,profile_id,thread_id] = it;
			if (!cpu_profiler_queue_per_threads.contains(thread_id))
			{
				cpu_profiler_queue_per_threads[thread_id] = List<std::tuple<bool,u32>>();
				frame_data._profile_data_list[thread_id] = List<ProfileFrameData::ProfileEditorData>();
			}
			cpu_profiler_queue_per_threads[thread_id].push_back(std::make_tuple(is_start,profile_id));
		}
		for(auto& it : cpu_profiler_queue_per_threads)
		{
			i16 call_depth = -1;
			auto& [thread_id,data_list] = it;
			for(auto& [is_start, profile_id] : data_list)
			{
				auto& profile_data = cpuProfiles[profile_id];
				frame_data._start_time = std::min<f32>((f32)profile_data._start_time_since_cur_frame, frame_data._start_time);
				frame_data._end_time = std::max<f32>((f32)profile_data._start_time_since_cur_frame + (f32)profile_data._time, frame_data._end_time);
				call_depth = is_start? call_depth + 1 : call_depth - 1;
				if (is_start)
					frame_data._profile_data_list[thread_id].emplace_back(ProfileFrameData::ProfileEditorData{profile_data.Name, (f32)profile_data._start_time_since_cur_frame, (f32)profile_data._time, (u16)call_depth});
			}
		}
		_cpu_profiler_queue.clear();
		_cache_data.push_back(frame_data);
	}
	//----------------------------------------------------------------------------------ProfileBlock------------------------------------------------------------------------------
	ProfileBlock::ProfileBlock(CommandBuffer * cmdList, const String& name) : cmdList(cmdList)
	{
		idx = Profiler::Get().StartProfile(cmdList, name);
		Profiler::Get().AddGPUProfilerHierarchy(true, idx);
	}
	ProfileBlock::~ProfileBlock()
	{
		Profiler::Get().EndProfile(cmdList, idx);
		Profiler::Get().AddGPUProfilerHierarchy(false, idx);
	}
	//----------------------------------------------------------------------------------ProfileBlock------------------------------------------------------------------------------

	//----------------------------------------------------------------------------------CPUProfileBlock------------------------------------------------------------------------------
	CPUProfileBlock::CPUProfileBlock(const String& name)
	{
		idx = Profiler::Get().StartCPUProfile(name);
		Profiler::Get().AddCPUProfilerHierarchy(true, idx);
	}
	CPUProfileBlock::~CPUProfileBlock()
	{
		Profiler::Get().EndCPUProfile(idx);
		Profiler::Get().AddCPUProfilerHierarchy(false, idx);
	}
	//----------------------------------------------------------------------------------CPUProfileBlock------------------------------------------------------------------------------
}
