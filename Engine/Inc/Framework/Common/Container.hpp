#ifndef __CONTAINER_H__
#define __CONTAINER_H__
#include "GlobalMarco.h"

namespace Ailu
{
    namespace Core
    {
        //单生产/消费者
        template<typename T,u32 NUM>
        class LockFreeQueue
        {
        public:
            explicit LockFreeQueue()
                : _buffer(NUM), _capacity(NUM), _head(0), _tail(0) {}

            bool Push(T&& value)
            {
                if (Full())
                    return false;
                u64 current_tail = _tail.load(std::memory_order_relaxed);
                u64 next_tail = Increment(current_tail);
                _buffer[current_tail] = std::move(value);
                _tail.store(next_tail, std::memory_order_release);
                return true;
            }
            bool Push(const T& value)
            {
                u64 current_tail = _tail.load(std::memory_order_relaxed);
                u64 next_tail = Increment(current_tail);

                if (next_tail == _head.load(std::memory_order_acquire)) {
                    // 队列已满
                    return false;
                }

                _buffer[current_tail] = value;
                _tail.store(next_tail, std::memory_order_release);
                return true;
            }
            std::optional<T> Pop()
            {
                u64 current_head = _head.load(std::memory_order_relaxed);
    
                // 先检查队列是否为空
                if (current_head == _tail.load(std::memory_order_acquire)) {
                    // 队列为空
                    return std::nullopt;
                }
    
                // 这里的 CAS 检查确保没有其他线程在修改 _head
                if (_head.compare_exchange_strong(current_head, Increment(current_head), std::memory_order_acquire)) {
                    return std::make_optional(std::move(_buffer[current_head]));
                }
    
                return std::nullopt;  // 如果 CAS 失败，表示有其他线程已更新 _head
            }
    
            bool Empty() const
            {
                return _head.load(std::memory_order_acquire) == _tail.load(std::memory_order_acquire);
            }
            bool Full() const
            {
                u64 next_tail = Increment(_tail.load(std::memory_order_acquire));
                return next_tail == _head.load(std::memory_order_acquire);
            }
            u32 Size() const
            {
                u64 head = _head.load(std::memory_order_acquire);
                u64 tail = _tail.load(std::memory_order_acquire);
                return (u32)((tail - head + _capacity) % _capacity);
            }
            u32 Capacity() const
            {
                return (u32)_capacity;
            }
        private:
            u64 Increment(u64 index) const
            {
                return (index + 1) % _capacity;  // 环形缓冲区中的下一个位置
                //return (index + 1) & (_capacity - 1);  // 仅适用于 _capacity 为 2 的幂
            }
            std::vector<T> _buffer;
            u64 _capacity;
            std::atomic<u64> _head;  // 头指针
            std::atomic<u64> _tail;  // 尾指针
        };
    }
}
#endif