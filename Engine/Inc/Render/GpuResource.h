#pragma once
#ifndef __FRAME_RESOURCE_H__
#define __FRAME_RESOURCE_H__
#include "GlobalMarco.h"
#include "Objects/Object.h"
namespace Ailu
{
    class GpuResourceState
    {
        friend class GpuResource;
    public:
        GpuResourceState() = default;
        void Track();
        [[nodiscard]] bool IsReferenceByGpu() const;
        [[nodiscard]] u64 GetFenceValue() const { return _fence_value; };
        u64 GetSize() const { return _mem_size; }
        void SetSize(u64 size);
    private:
        u64 _mem_size = 0u;
        inline static u64 s_total_mem_size = 0u;
        u64 _fence_value = 0u;
    };
	class GpuResource : public Object
	{
	public:
        static u64 TotalMemSize() { return GpuResourceState::s_total_mem_size; }
        GpuResource();
		~GpuResource() override;
        void Bind();
        GpuResourceState &GetState() { return _state; }
	protected:
        GpuResourceState _state;
	};
}

#endif // !FRAME_RESOURCE_H__

