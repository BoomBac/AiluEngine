//
// Created by 22292 on 2024/10/11.
//

#ifndef AILU_JOBSYSTEM_H
#define AILU_JOBSYSTEM_H

#include "GlobalMarco.h"
#include <set>
#include <future>
namespace Ailu
{
    using JobFunction = std::function<void()>;

    class JobSystem;
    class Job
    {
    public:
        Job() : _index(s_global_index++) {};

        Job(String  name, std::function<void()> task)
            : _index(s_global_index++),_name(std::move(name)), _function(std::move(task)){};

        void Construct(const String& name,JobFunction func)
        {
            _name = name;
            _function = std::move(func);
            _promise = MakeRef<std::promise<void>>();
        }
        const String& Name() const { return _name; }

        void Execute() { _function();}

        bool CompleteDependency() { return _remaining_dependencies.fetch_sub(1, std::memory_order_acq_rel) == 1; }

        /// @brief 添加一个后继任务
        /// @param dependent 该任务的依赖任务
        void AddContinuation(Job *dependent)
        {
            dependent->AddDependency();
            _continuations.emplace_back(dependent);
        }

        void OnComplete();

        void SetJobSystem(JobSystem *system) { _system = system; }

        void Release()
        {
            _remaining_dependencies.store(0);
            _continuations.clear();
            _name.clear();
            _function = nullptr;
            _promise.reset();
        }
        const u32 Index() const {return _index;}
        Ref<std::future<void>> GetFuture()
        {
            return MakeRef<std::future<void>>(_promise->get_future());
        }

    private:
        void AddDependency() { _remaining_dependencies.fetch_add(1, std::memory_order_relaxed); }
    private:
        inline static std::atomic<u32> s_global_index = 0u;
        JobFunction _function;
        std::atomic<int> _remaining_dependencies;
        std::vector<Job *> _continuations;
        u32 _index;
        JobSystem *_system = nullptr;
        String _name;
        Ref<std::promise<void>> _promise;
    };

    struct JobHandle
    {
        u32 _job_index;
        u64 _fence_value;
        JobHandle() : _job_index(0u),_fence_value(0u) {};
        JobHandle(u32 idx,u64 fence) : _job_index(idx),_fence_value(fence) {};
        bool operator<(const JobHandle& other) const {return _job_index < other._job_index;}
    };

    class WaitHandle
    {
    public:
        DISALLOW_COPY_AND_ASSIGN(WaitHandle)
        explicit WaitHandle(Ref<std::future<void>> future)
            : _future(std::move(future))
        {}
        WaitHandle(WaitHandle &&other) noexcept
        {
            _future = std::move(other._future);
        }
        WaitHandle& operator=(WaitHandle&& other) noexcept
        {
            _future = std::move(other._future);
        }
        void Wait() const
        {
            _future->wait();
        }
        bool IsReady() const
        {
            return _future->wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }
    private:
        Ref<std::future<void>> _future;
    };

    class JobSystem
    {
        class JobPool
        {
        public:
            explicit JobPool(u32 capacity);
            ~JobPool();
            Job* Fetch();
            void Release(Job* job);
        private:
            Vector<Job*> _jobs;
            Queue<u32> _free_indices;
            Map<Job*,u32> _job_to_index;
            std::mutex _mutex;
        };
        class LockFreeJobQueue
        {
        public:
            explicit LockFreeJobQueue(size_t capacity = 1024)
                : _buffer(capacity), _head(0), _tail(0), _capacity(capacity) {}
    
            bool Enqueue(Job *job)
            {
                size_t current_tail = _tail.load(std::memory_order_relaxed);
                size_t next_tail = (current_tail + 1) % _capacity;
    
                if (next_tail == _head.load(std::memory_order_acquire))
                    return false;
    
                _buffer[current_tail] = job;
                _tail.store(next_tail, std::memory_order_release);
                return true;
            }
    
            bool Dequeue(Job *&job)
            {
                size_t current_head = _head.load(std::memory_order_relaxed);
                if (current_head == _tail.load(std::memory_order_acquire))
                    return false;
                //提前更新 _head，防止 Steal() 获取相同的 job
                //_head.store((current_head + 1) % _capacity, std::memory_order_release);
                job = _buffer[current_head];
                if (_head.compare_exchange_strong(current_head, (current_head + 1) % _capacity, std::memory_order_acquire))
                {
                    return true;
                }
                return false;
            }
    
    
            std::optional<Job *> Steal()
            {
                size_t current_head = _head.load(std::memory_order_relaxed);
                if (current_head == _tail.load(std::memory_order_acquire))
                    return std::nullopt;
    
                Job *job = _buffer[current_head];
                if (_head.compare_exchange_strong(current_head, (current_head + 1) % _capacity, std::memory_order_acquire))
                {
                    return job;
                }
                return std::nullopt;
            }
    
        private:
            std::vector<Job *> _buffer;
            std::atomic<size_t> _head;
            std::atomic<size_t> _tail;
            size_t _capacity;
        };
        inline static const u32 kMaxJobPoolSize = 128u;
    public:
        static void Init(u32 thread_count = std::thread::hardware_concurrency());
        static void Shutdown();
        static JobSystem& Get();
        explicit JobSystem(u32 thread_count = std::thread::hardware_concurrency());
        ~JobSystem();
        [[nodiscard]] Job *CreateJob(const String& name,JobFunction func);
        template<typename Callable, typename... Args>
        [[nodiscard]] Job* CreateJob(const String& name,Callable&& task, Args&&... args)
        {
            Job* job = _pool->Fetch();
            auto fn = std::bind(std::forward<Callable>(task), std::forward<Args>(args)...);
            job->Construct(name, fn);
            job->SetJobSystem(this);
            return job;
        }
        
        /// @brief 构建Job依赖
        /// @param dep 主Job
        /// @param dep_by 依赖于前者的Job，或者说前者的后继Job
        void AddDependency(Job* dep,Job* dep_by)
        {
            dep->AddContinuation(dep_by);
        }
        WaitHandle Dispatch(Job *job);
        WaitHandle Dispatch(const String& name,JobFunction func);
        template<typename Callable,typename... Args>
        WaitHandle Dispatch(Callable&& task,Args&&... args)
        {
            Job* job = _pool->Fetch();
            auto fn = std::bind(std::forward<Callable>(task), std::forward<Args>(args)...);
            job->Construct("noname", fn);
            return Dispatch(job);
        }
        void Wait();
        void Wait(const WaitHandle& handle) const;
        
    private:
        void WorkerThread(size_t index);
        bool TrySteal(Job *&job, size_t current_index);
    private:
        std::vector<std::unique_ptr<LockFreeJobQueue>> _queues;
        std::vector<std::thread> _threads;
        std::atomic<bool> _stop;
        std::atomic<size_t> _thread_index{0};
        std::mutex _mutex;
        std::condition_variable _cv;
        JobPool* _pool;
        Array<std::atomic<u64>,kMaxJobPoolSize> _job_fence;
        std::mutex _fence_mutex;
        std::condition_variable _fence_cv;
        //等待当前所有job
        std::mutex _all_job_mutex;
        std::condition_variable _all_job_cv;
        std::set<u32> _temp_jobs;
    };
};// namespace Ailu
#endif//AILU_JOBSYSTEM_H
