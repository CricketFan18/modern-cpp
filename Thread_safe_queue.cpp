#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

template <typename T>
class SafeQueue
{
public:
    SafeQueue() = default;
    ~SafeQueue() = default;

    // Prohibit copying: We don't want to accidentally duplicate the whole queue
    SafeQueue(const SafeQueue &) = delete;
    SafeQueue &operator=(const SafeQueue &) = delete;
    
    void push(const T &value)
    {
        push(T(value)); // Copy the lvalue into a temporary, then move it.
    }

    void push(T &&value)
    {
        std::lock_guard<std::mutex> lk(m);
        data_queue.push(std::move(value)); // The "Title Deed" is ours now.
        data_cond.notify_one();
    }

    // Blocking pop: Waits until an element is available
    T wait_and_pop()
    {
        {
            std::unique_lock<std::mutex> lk(m);
            data_cond.wait(lk, []()
                           { return !data_queue.empty() });

            auto data = std::move(data_queue.front());
            data_queue.pop();
            return data;
        }
    }

    // Non-blocking pop: Returns immediately
    // Returns std::optional<T> (empty if queue was empty)
    std::optional<T> try_pop()
    {
        {
            std::unique_lock<std::mutex> lk(m);
            if (data_queue.empty())
                return std::nullopt;
            else
            {
                auto data = std::move(data_queue.front());
                data_queue.pop();
                return data;
            }
        }
    }

    bool empty() const
    {
        std::unique_lock<std::mutex> lk(m);
        return data_queue.empty();
    }

private:
    std::queue<T> data_queue;
    mutable std::mutex m;
    std::condition_variable data_cond;
};