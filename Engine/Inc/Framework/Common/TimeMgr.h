#pragma once
#ifndef __TIMEMGR_H__
#define __TIMEMGR_H__

#include "Framework/Interface/IRuntimeModule.h"

namespace Ailu
{
	class AILU_API TimeMgr : IRuntimeModule
	{
	public:
		using ALSecond = std::chrono::seconds;
		using ALMSecond = std::chrono::duration<float, std::ratio<1, 1000>>;
		using ALTimeStamp = std::chrono::high_resolution_clock::time_point;
		inline static float _s_delta_time = 0.0f;
		inline static float _s_time_since_load = 0.0f;

		int Initialize() override;
		void Finalize() override;
		void Tick() override;
		void Pause();
		void Mark();
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
}


#endif // !TIMEMGR_H__
