#ifndef __CONTAINER_H__
#define __CONTAINER_H__
#include "GlobalMarco.h"

namespace Ailu
{
    template<typename T,u32 capacity>
    class RingBuffer
    {
    public:
        explicit RingBuffer()
            : _capacity(capacity + 1), _buffer(_capacity), _head(0), _tail(0) {}

        // 尝试写入数据，成功返回 true，失败返回 false
        bool Push(const T& item) 
        {
            u32 head = _head.load(std::memory_order_relaxed);
            u32 next_head = (head + 1) % _capacity;
    
            // 检查队列是否已满
            if (next_head == _tail.load(std::memory_order_acquire)) {
                return false; // 缓冲区已满
            }
    
            _buffer[head] = item; // 写入数据
            _head.store(next_head, std::memory_order_release);
            return true;
        }
    
        // 尝试读取数据，成功返回 true，失败返回 false
        bool Pop(T& item) 
        {
            u32 tail = _tail.load(std::memory_order_relaxed);
    
            // 检查队列是否为空
            if (tail == _head.load(std::memory_order_acquire)) {
                return false; // 缓冲区为空
            }
    
            item = _buffer[tail]; // 读取数据
            _tail.store((tail + 1) % _capacity, std::memory_order_release);
            return true;
        }
        bool Pop()
        {
            u32 tail = _tail.load(std::memory_order_relaxed);

            // 检查队列是否为空
            if (tail == _head.load(std::memory_order_acquire)) {
                return false; // 缓冲区为空
            }

            _tail.store((tail + 1) % _capacity, std::memory_order_release);
            return true;
        }
    
        // 检查缓冲区是否为空
        bool Empty() const {return _head.load(std::memory_order_acquire) == _tail.load(std::memory_order_acquire);}
    
        // 检查缓冲区是否满
        bool Full() const {return (_head.load(std::memory_order_acquire) + 1) % _capacity == _tail.load(std::memory_order_acquire);}

    private:
        const u32 _capacity;               // 缓冲区容量（比实际可用容量多1）
        Vector<T> _buffer;               // 存储数据的缓冲区
        std::atomic<u32> _head, _tail;      // 头尾指针
    };

    template <typename T>
    class LockFreeQueue
    {
    public:
        explicit LockFreeQueue(size_t capacity = 64u)
            : _buffer(capacity), _capacity(capacity), _head(0), _tail(0) {}

        bool Push(const T& value)
        {
            size_t current_tail = _tail.load(std::memory_order_relaxed);
            size_t next_tail = Increment(current_tail);

            if (next_tail == _head.load(std::memory_order_acquire)) {
                // 队列已满
                return false;
            }

            _buffer[current_tail] = value;  // 将数据存入缓冲区
            _tail.store(next_tail, std::memory_order_release);
            return true;
        }

        // 出队操作（多消费者）
        std::optional<T> Pop()
        {
            size_t current_head = _head.load(std::memory_order_relaxed);

            // 先检查队列是否为空
            if (current_head == _tail.load(std::memory_order_acquire)) {
                // 队列为空
                return std::nullopt;
            }

            // 这里的 CAS 检查确保没有其他线程在修改 _head
            if (_head.compare_exchange_strong(current_head, Increment(current_head), std::memory_order_acquire)) {
                return std::make_optional(_buffer[current_head]);
            }

            return std::nullopt;  // 如果 CAS 失败，表示有其他线程已更新 _head
        }

        bool Empty() const
        {
            return _head.load(std::memory_order_acquire) == _tail.load(std::memory_order_acquire);
        }
        u32 Size() const
        {
            u64 head = _head.load(std::memory_order_acquire);
            u64 tail = _tail.load(std::memory_order_acquire);
            return (u32)((tail - head + _capacity) % _capacity);
        }
    private:
        size_t Increment(size_t index) const
        {
            return (index + 1) % _capacity;  // 环形缓冲区中的下一个位置
            //return (index + 1) & (_capacity - 1);  // 仅适用于 _capacity 为 2 的幂
        }
        std::vector<T> _buffer;
        size_t _capacity;
        std::atomic<size_t> _head;  // 头指针
        std::atomic<size_t> _tail;  // 尾指针
    };
}
#endif