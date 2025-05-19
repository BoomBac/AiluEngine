//
// Created by 22292 on 2024/10/11.
//
#include "Framework/Common/JobSystem.h"
#include "Framework/Common/Profiler.h"

#include "Render/GraphicsContext.h"

namespace Ailu
{
    void Job::OnComplete()
    {
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
            _jobs[i] = new Job();
            _job_to_index[_jobs[i]] = i;
            _free_indices.push(i);
        }
    }
    JobSystem::JobPool::~JobPool()
    {
        for(auto& item : _jobs)
            DESTORY_PTR(item);
    }
    JobSystem* g_pJobSystem = nullptr;
    void JobSystem::Init(u32 thread_count)
    {
        if (g_pJobSystem == nullptr)
            g_pJobSystem = new JobSystem(thread_count);
    }
    void JobSystem::Shutdown()
    {
        DESTORY_PTR(g_pJobSystem);
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
                _jobs.push_back(new Job());
                u32 index = static_cast<i32>(_jobs.size() - 1);
                _free_indices.push(index);
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

    JobSystem::JobSystem(u32 thread_count): _stop(false)
    {
        for (u32 i = 0; i < thread_count; ++i)
        {
            _queues.emplace_back(std::make_unique<LockFreeJobQueue>(1024));
            _threads.emplace_back([this, i]()
                                { WorkerThread(i); });
        }
        for (u32 i = 0u; i < kMaxJobPoolSize; i++)
            _job_fence[i] = 0u;
        _pool = new JobPool(64);
    }

    JobSystem::~JobSystem()
    {
        _stop = true;
        _cv.notify_all();
        for (auto &thread: _threads)
        {
            if (thread.joinable())
                thread.join();
        }
        DESTORY_PTR(_pool);
    }

    WaitHandle JobSystem::Dispatch(Job *job)
    {
        job->SetJobSystem(this);
        size_t index = _thread_index++ % _queues.size();
        u64 fence_value = _job_fence[job->Index()].load();
        auto fu = job->GetFuture();
        _queues[index]->Enqueue(job);
        _cv.notify_one();
        //LOG_INFO("Notify job {}-{}", job->Index(), job->Name());
        {
            std::lock_guard<std::mutex> lock(_all_job_mutex);
            _temp_jobs.insert(job->Index());
        }
        //LOG_INFO("JobSystem::Dispatch: job {}-{},fence value {}", job->Index(), job->Name(), fence_value);
        //return JobHandle(job->Index(),fence_value + 1u);
        return WaitHandle(fu);
    }
    WaitHandle JobSystem::Dispatch(const String& name,JobFunction func)
    {
        Job* job = _pool->Fetch();
        job->Construct(name,func);
        return Dispatch(job);
    }
    Job *JobSystem::CreateJob(const String& name,JobFunction func)
    {
        Job* job = _pool->Fetch();
        job->Construct(name,func);
        job->SetJobSystem(this);
        return job;
    }

    void JobSystem::WorkerThread(size_t index)
    {
        SetThreadName(std::format("JobWorker_{0}", index));
        while (!_stop)
        {
            Job *job = nullptr;
            if (_queues[index]->Dequeue(job) || TrySteal(job, index))
            {
                //LOG_INFO("JobSystem::WorkerThread: job {}-{} executed by {}", job->Index(), job->Name(), _thread_names[std::this_thread::get_id()])
                job->Execute();
                job->OnComplete();
                ++_job_fence[job->Index()];
                //LOG_INFO("JobSystem::WorkerThread: job {}-{} completed,fence value {}", job->Index(),job->Name(),_job_fence[job->Index()].load());
                _fence_cv.notify_all();
                {
                    std::lock_guard<std::mutex> lock(_all_job_mutex);
                    _temp_jobs.erase(job->Index());
                    if (_temp_jobs.empty())
                        _all_job_cv.notify_all();
                }
                _pool->Release(job);
            }
            else
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _cv.wait(lock);
            }
        }
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

    void JobSystem::Wait(const WaitHandle& handle) const
    {
        handle.Wait();
        // std::unique_lock<std::mutex> lock(_fence_mutex);
        // while (_job_fence[handle._job_index] < handle._fence_value)
        //    std::this_thread::yield();
    }

    void JobSystem::Wait()
    {
        std::unique_lock<std::mutex> lock(_all_job_mutex);
        _all_job_cv.wait(lock,[&]()->bool {return _temp_jobs.empty();});
    }
}// namespace Ailu