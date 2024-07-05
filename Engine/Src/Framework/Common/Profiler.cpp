#include "pch.h"
#include "Framework/Common/Profiler.h"
#include "Render/GraphicsContext.h"

namespace Ailu
{
	Profiler Profiler::g_Profiler;


	void Profiler::Initialize()
	{
		_p_gpu_timer = g_pGfxContext->GetTimer();

	}
	void Profiler::Shutdown()
	{
	}
	u64 Profiler::StartProfile(CommandBuffer* cmdList, const String& name)
	{
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
			AL_ASSERT(numProfiles >= kMaxProfileNum);
			profileIdx = numProfiles++;
			profiles[profileIdx].Name = name;
		}
		ProfileData& profile_data = profiles[profileIdx];
		AL_ASSERT(profile_data.QueryStarted == true);
		AL_ASSERT(profile_data.QueryFinished == false);
		profile_data.CPUProfile = false;
		profile_data.Active = true;
		_p_gpu_timer->Start(cmdList, profileIdx);
		profile_data.QueryStarted = true;
		profile_data.QueryFinished = false;
		return profileIdx;
	}
	void Profiler::EndProfile(CommandBuffer* cmdList, u64 idx)
	{
		AL_ASSERT(idx >= numProfiles);
		ProfileData& profile_data = profiles[idx];
		AL_ASSERT(profile_data.QueryStarted == false);
		AL_ASSERT(profile_data.QueryFinished == true);
		_p_gpu_timer->Stop(cmdList, idx);
		profile_data._time = _p_gpu_timer->GetElapsedMS(idx);
		profile_data.QueryStarted = false;
		profile_data.QueryFinished = true;
	}
	u64 Profiler::StartCPUProfile(const String& name)
	{
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
			AL_ASSERT(numCPUProfiles >= kMaxProfileNum);
			profileIdx = numCPUProfiles++;
			profiles[profileIdx].Name = name;
		}
		ProfileData& profile_data = cpuProfiles[profileIdx];
		AL_ASSERT(profile_data.QueryStarted == true);
		AL_ASSERT(profile_data.QueryFinished == false);
		profile_data.Name = name;
		profile_data.CPUProfile = true;
		profile_data.Active = true;
		g_pTimeMgr->Mark();
		//g_pTimeMgr _cpu_timer.Mark();
		profile_data.QueryStarted = true;
		profile_data.QueryFinished = false;
		return profileIdx;
	}
	void Profiler::EndCPUProfile(u64 idx)
	{
		AL_ASSERT(idx >= numCPUProfiles);
		ProfileData& profile_data = cpuProfiles[idx];
		AL_ASSERT(profile_data.QueryStarted == false);
		AL_ASSERT(profile_data.QueryFinished == true);
		profile_data._time = g_pTimeMgr->GetElapsedSinceLastMark();
		profile_data.QueryStarted = false;
		profile_data.QueryFinished = true;
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
	}
	//----------------------------------------------------------------------------------ProfileBlock------------------------------------------------------------------------------
	ProfileBlock::ProfileBlock(CommandBuffer* cmdList, const String& name) : cmdList(cmdList)
	{
		idx = Profiler::g_Profiler.StartProfile(cmdList, name);
		Profiler::g_Profiler.AddGPUProfilerHierarchy(true, idx);
	}
	ProfileBlock::~ProfileBlock()
	{
		Profiler::g_Profiler.EndProfile(cmdList, idx);
		Profiler::g_Profiler.AddGPUProfilerHierarchy(false, idx);
	}
	//----------------------------------------------------------------------------------ProfileBlock------------------------------------------------------------------------------

	//----------------------------------------------------------------------------------CPUProfileBlock------------------------------------------------------------------------------
	CPUProfileBlock::CPUProfileBlock(const String& name)
	{
		idx = Profiler::g_Profiler.StartCPUProfile(name);
		Profiler::g_Profiler.AddCPUProfilerHierarchy(true, idx);
	}
	CPUProfileBlock::~CPUProfileBlock()
	{
		Profiler::g_Profiler.EndCPUProfile(idx);
		Profiler::g_Profiler.AddCPUProfilerHierarchy(false, idx);
	}
	//----------------------------------------------------------------------------------CPUProfileBlock------------------------------------------------------------------------------
}
