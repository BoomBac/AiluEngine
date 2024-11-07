//
// Created by 22292 on 2024/11/7.
//

#ifndef AILU_TIMER_H
#define AILU_TIMER_H
#include <chrono>

class Timer
{
public:
    Timer() : _running(false), _elapsed(0) {}

    // 启动计时器
    void start() {
        if (!_running) {
            _start_time = std::chrono::high_resolution_clock::now();
            _running = true;
        }
    }

    // 停止计时器
    void stop() {
        if (_running) {
            auto end_time = std::chrono::high_resolution_clock::now();
            _elapsed += std::chrono::duration_cast<std::chrono::milliseconds>(end_time - _start_time).count();
            _running = false;
        }
    }

    // 重置计时器
    void reset() {
        _running = false;
        _elapsed = 0;
    }

    // 获取当前计时时间（毫秒）
    long long ElapsedMilliseconds() const {
        if (_running) {
            auto now = std::chrono::high_resolution_clock::now();
            return _elapsed + std::chrono::duration_cast<std::chrono::milliseconds>(now - _start_time).count();
        }
        return _elapsed;
    }

    // 获取当前计时时间（秒）
    double ElapsedSeconds() const {
        return ElapsedMilliseconds() / 1000.0;
    }

private:
    bool _running;
    long long _elapsed; // 累积的时间，单位为毫秒
    std::chrono::high_resolution_clock::time_point _start_time;
};

#endif//AILU_TIMER_H
