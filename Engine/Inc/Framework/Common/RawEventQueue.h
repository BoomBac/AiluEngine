#ifndef __RAW_EVENT_QUEUE_H__
#define __RAW_EVENT_QUEUE_H__
#include "Container.hpp"

namespace Ailu
{
    class Event;
    namespace Core
    {
        class RawEventQueue
        {
        public:
            static constexpr u32 MAX_EVENT_SIZE = 64;
            struct EventData
            {
                u32 _type;
                alignas(std::max_align_t) u8 _payload[MAX_EVENT_SIZE];
            };
            template<typename T>
            void Push(const T &e)
            {
                static_assert(std::is_base_of_v<Event, T>);
                EventData data;
                data._type = static_cast<u32>(T::GetStaticType());
                new (data._payload) T(e);
                _events.Push(data);
            }
            Event* Pop(u8* event_mem);
        private:
            Core::LockFreeQueue<EventData, 512> _events;
            Vector<WString> _drop_files;
        };
    }
}
#endif// !__RAW_EVENT_QUEUE_H__
