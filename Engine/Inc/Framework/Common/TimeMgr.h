#pragma warning(push)
#pragma warning(disable: 4251) //std库直接暴露为接口dll在客户端上使用时可能会有问题，禁用该编译警告
#pragma once
#ifndef __TIMEMGR_H__
#define __TIMEMGR_H__

#include "Framework/Interface/IRuntimeModule.h"

namespace Ailu
{
	class ModuleTimeStatics
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
		inline static float DeltaTime = 0.0f;
		inline static float TimeSinceLoad = 0.0f;

		int Initialize() override;
		void Finalize() override;
		void Tick() override;
		void Pause();
		void Mark();
		float GetElapsedSinceLastMark() const;
		void Resume();
		void Reset();
	private:
		ALTimeStamp _pre_stamp;
		ALTimeStamp _cur_stamp;
		ALTimeStamp _init_stamp;
		ALTimeStamp _pause_stamp;
		ALTimeStamp _mark_stamp;
		float _total_time = 0.0f;
		float _pause_time = 0.0f;
		bool _b_stop = false;
	};
	extern TimeMgr* g_pTimeMgr;
}
#pragma warning(pop)

#endif // !TIMEMGR_H__
