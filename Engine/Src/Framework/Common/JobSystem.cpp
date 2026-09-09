//
// Created by 22292 on 2024/10/11.
//
#include "Framework/Common/JobSystem.h"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/Profiler.h"

#include "Render/GraphicsContext.h"

#ifdef _WIN32
#include <combaseapi.h>
#endif

namespace Ailu
{
    void Job::OnComplete(std::exception_ptr exception)
    {
        if (exception)
            _promise->set_exception(exception);
        else
            _promise->set_value();
        for (auto *continuation: _continuations)
        {
            if (continuation->CompleteDependency())
            {
                _system->Dispatch(continuation);
            }
        }
    }
    JobSystem::JobPool::JobPool(u32 capacity)
    {
        _jobs.resize(capacity);
        for(u16 i = 0; i < capacity; ++i)
        {
            _jobs[i] = AL_NEW_TAG(EMemoryTag::kJobSystem, Job, i);
            _job_to_index[_jobs[i]] = i;
            _free_indices.push(i);
        }
    }
    JobSystem::JobPool::~JobPool()
    {
        for(auto& item : _jobs)
        {
            AL_DELETE(item);
        }
    }
    JobSystem* g_pJobSystem = nullptr;
    void JobSystem::Init(u32 thread_count)
    {
        if (g_pJobSystem == nullptr)
            g_pJobSystem = AL_NEW_TAG(EMemoryTag::kJobSystem, JobSystem, thread_count);
    }
    void JobSystem::Shutdown()
    {
        AL_DELETE(g_pJobSystem);
    }
    JobSystem& JobSystem::Get()
    {
        if (g_pJobSystem == nullptr)
            Init(6);
        return *g_pJobSystem;
    }

    Job *JobSystem::JobPool::Fetch()
    {
        std::lock_guard lock(_mutex);
        if (_free_indices.empty())
        {
            if (_jobs.size() < kMaxJobPoolSize)
            {
                u32 index = static_cast<u32>(_jobs.size());
                _jobs.push_back(AL_NEW_TAG(EMemoryTag::kJobSystem, Job, index));
                _job_to_index[_jobs.back()] = index;
                LOG_INFO("JobSystem::JobPool::Fetch: job pool resized to {}", _jobs.size());
                return _jobs.back();
            }
            else
            {
                LOG_INFO("JobSystem::JobPool::Fetch: job pool exceed {}",kMaxJobPoolSize);
                return nullptr;
            }
        }
        Job* job = _jobs[_free_indices.front()];
        _free_indices.pop();
        return job;
    }
    void JobSystem::JobPool::Release(Job *job)
    {
        std::lock_guard lock(_mutex);
        AL_ASSERT(_job_to_index.contains(job));
        _free_indices.push(_job_to_index[job]);
        job->Release();
    }
    bool JobSystem::JobPool::Contains(Job *job) const
    {
        std::lock_guard lock(_mutex);
        return _job_to_index.contains(job);
    }

    JobSystem::JobSystem(u32 thread_count): _stop(false)
    {
        if (thread_count == 0u)
            thread_count = 1u;
        for (u32 i = 0; i < thread_count; ++i)
        {
            _queues.emplace_back(std::make_unique<LockFreeJobQueue>(1024));
            _threads.emplace_back([this, i]()
                                { WorkerThread(i); });
        }
        for (u32 i = 0u; i < kMaxJobPoolSize; i++)
            _job_fence[i] = 0u;
        _pool = AL_NEW_TAG(EMemoryTag::kJobSystem, JobPool, 64);
    }

    JobSystem::~JobSystem()
    {
        Wait();
        _stop = true;
        _cv.notify_all();
        for (auto &thread: _threads)
        {
            if (thread.joinable())
                thread.join();
        }
        AL_DELETE(_pool);
    }

    WaitHandle JobSystem::Dispatch(Job *job)
    {
        job->SetJobSystem(this);
        size_t index = _thread_index++ % _queues.size();
        AL_ASSERT(job->IsValid());
        auto fu = job->GetFuture();
        {
            std::lock_guard<std::mutex> lock(_all_job_mutex);
            _temp_jobs.insert(job->Index());
        }
        if (!_queues[index]->Enqueue(job))
        {
            bool enqueued = false;
            for (auto &queue: _queues)
            {
                if (queue->Enqueue(job))
                {
                    enqueued = true;
                    break;
                }
            }
            AL_ASSERT(enqueued);
            if (!enqueued)
            {
                CompleteJob(job, std::make_exception_ptr(std::runtime_error("JobSystem queue is full")));
                return WaitHandle(fu);
            }
        }
        _cv.notify_one();
        //LOG_INFO("Notify job {}-{}", job->Index(), job->Name());
        return WaitHandle(fu);
    }
    WaitHandle JobSystem::Dispatch(const String& name,JobFunction func)
    {
        Job* job = _pool->Fetch();
        AL_ASSERT(job);
        if (job == nullptr)
        {
            auto promise = MakeRef<std::promise<void>>();
            auto future = MakeRef<std::future<void>>(promise->get_future());
            promise->set_exception(std::make_exception_ptr(std::runtime_error("JobSystem job pool is full")));
            return WaitHandle(future);
        }
        job->Construct(name,func);
        return Dispatch(job);
    }
    Job *JobSystem::CreateJob(const String& name,JobFunction func)
    {
        Job* job = _pool->Fetch();
        AL_ASSERT(job);
        if (job == nullptr)
            return nullptr;
        job->Construct(name,func);
        job->SetJobSystem(this);
        return job;
    }

    bool JobSystem::WorkOnce()
    {
        Job *job = nullptr;
        size_t index = _thread_index % _queues.size();

        if (_queues[index]->Dequeue(job) || TrySteal(job, index))//任意队列偷取
        {
            AL_ASSERT(job->IsValid());
            //LOG_INFO("JobSystem::WorkerThread: job {}-{} executed by {}", job->Index(), job->Name(), _thread_names[std::this_thread::get_id()])
            std::exception_ptr exception = nullptr;
            try
            {
                job->Execute();
            }
            catch (...)
            {
                exception = std::current_exception();
            }
            CompleteJob(job, exception);
            return true;
        }
        return false;
    }

    thread_local static size_t s_worker_thread_id;

    void JobSystem::WorkerThread(size_t index)
    {
        SetThreadName(std::format("JobWorker_{0}", index));
        s_worker_thread_id = index;
#ifdef _WIN32
        const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool should_uninitialize = com_result == S_OK || com_result == S_FALSE;
        if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE)
        {
            LOG_WARNING("JobSystem::WorkerThread: COM initialization failed (HRESULT=0x{:08x})",
                        static_cast<u32>(com_result));
        }
#endif
        while (!_stop)
        {
            Job *job = nullptr;
            if (_queues[index]->Dequeue(job) || TrySteal(job, index))
            {
                //LOG_INFO("JobSystem::WorkerThread: job {}-{} executed by {}", job->Index(), job->Name(), _thread_names[std::this_thread::get_id()])
                AL_ASSERT(job->IsValid());
                std::exception_ptr exception = nullptr;
                try
                {
                    job->Execute();
                }
                catch (...)
                {
                    exception = std::current_exception();
                }
                CompleteJob(job, exception);
            }
            else
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _cv.wait(lock, [this]() { return _stop.load(std::memory_order_relaxed) || HasPendingJob(); });
                //std::this_thread::yield();
                //if (WorkOnce()) 
                //    continue;
            }
        }
#ifdef _WIN32
        if (should_uninitialize)
            CoUninitialize();
#endif
    }
    bool JobSystem::HasPendingJob() const
    {
        for (const auto &queue: _queues)
        {
            if (!queue->Empty())
                return true;
        }
        return false;
    }
    void JobSystem::CompleteJob(Job *job, std::exception_ptr exception)
    {
        job->OnComplete(exception);
        if (job->Index() < kMaxJobPoolSize)
            ++_job_fence[job->Index()];
        //LOG_INFO("JobSystem::WorkerThread: job {}-{} completed,fence value {}", job->Index(),job->Name(),_job_fence[job->Index()].load());
        _fence_cv.notify_all();
        {
            std::lock_guard<std::mutex> lock(_all_job_mutex);
            _temp_jobs.erase(job->Index());
            if (_temp_jobs.empty())
                _all_job_cv.notify_all();
        }
        if (_pool->Contains(job))
            _pool->Release(job);
        else
            job->Release();
    }
    bool JobSystem::TrySteal(Job *&job, size_t current_index)
    {
        for (size_t i = 0; i < _queues.size(); ++i)
        {
            if (i == current_index) continue;
            auto stolen_job = _queues[i]->Steal();
            if (stolen_job.has_value())
            {
                job = stolen_job.value();
                //LOG_INFO("JobSystem::TrySteal: job {}-{} stolen by {}", job->Index(), job->Name(), _thread_names[std::this_thread::get_id()]);
                return true;
            }
        }
        return false;
    }

    void JobSystem::Wait(const WaitHandle& handle)
    {
        while (!handle.IsReady())
        {
            if (!WorkOnce())// RunOnePendingJob
                std::this_thread::yield();
        }
    }

    void JobSystem::Wait()
    {
        while (true)
        {
            if (WorkOnce())
                continue;
            std::unique_lock<std::mutex> lock(_all_job_mutex);
            if (_all_job_cv.wait_for(lock, std::chrono::milliseconds(1), [&]() -> bool { return _temp_jobs.empty(); }))
                return;
        }
    }
}// namespace Ailu
