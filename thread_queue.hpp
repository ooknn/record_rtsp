#ifndef __THREAD_QUEUE_HPP__
#define __THREAD_QUEUE_HPP__

#include <mutex>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <optional>
#include <assert.h>

namespace ooknn
{
template <typename T>
class ThreadQueue
{
public:
    ThreadQueue()                    = default;
    ThreadQueue(const ThreadQueue &) = delete;
    ThreadQueue &    operator=(const ThreadQueue &) = delete;
    void             Push(T &&t);
    bool             Empty();
    T                Pop();
    std::optional<T> TryPop();
    size_t           Size();
    void             Clear();

private:
    T pop();

private:
    std::atomic<int>        size{0};
    std::deque<T>           d;
    std::mutex              mu;
    std::condition_variable cond;
};
}    // namespace ooknn

template <typename T>
bool ooknn::ThreadQueue<T>::Empty()
{
    return size == 0;
}

template <typename T>
void ooknn::ThreadQueue<T>::Clear()
{
    std::lock_guard<std::mutex> lock(mu);
    d.clear();
    size.store(0);
}

template <typename T>
size_t ooknn::ThreadQueue<T>::Size()
{
    return size;
}

template <typename T>
void ooknn::ThreadQueue<T>::Push(T &&t)
{
    std::lock_guard<std::mutex> lock(mu);
    d.push_back(std::move(t));
    ++size;
    cond.notify_one();
}

template <typename T>
T ooknn::ThreadQueue<T>::Pop()
{
    std::unique_lock<std::mutex> lock(mu);
    cond.wait(lock, [&]() { return !d.empty(); });
    return pop();
}

template <typename T>
T ooknn::ThreadQueue<T>::pop()
{
    assert(!d.empty());
    auto val = std::move(d.front());
    d.pop_front();
    --size;
    return val;
}

template <typename T>
std::optional<T> ooknn::ThreadQueue<T>::TryPop()
{
    std::lock_guard<std::mutex> lock(mu);
    if (d.size())
    {
        return pop();
    }
    return std::nullopt;
}

#endif
