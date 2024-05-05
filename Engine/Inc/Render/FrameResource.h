#pragma once
#ifndef __FRAME_RESOURCE_H__
#define __FRAME_RESOURCE_H__
#include "GlobalMarco.h"
#include "Objects/Object.h"
namespace Ailu
{
	class FrameResource : public Object
	{
	public:
		FrameResource();
		~FrameResource();
		void SetFenceValue(u64 fence) { _fence_value = fence; };
		u64 GetFenceValue() const { return _fence_value; };
		void SetLastAccessFrameCount(u64 frame_count) { _last_access_frame_count = frame_count; };
		u64 GetLastAccessFrameCount() const { return _last_access_frame_count; };
		u64 GetGpuMemerySize() const { return _gpu_memery_size; };
		bool _is_referenced_by_gpu;
	protected:
		u64 _fence_value;
		u64 _last_access_frame_count;
		u64 _gpu_memery_size = 0u;
	};
}

#endif // !FRAME_RESOURCE_H__

