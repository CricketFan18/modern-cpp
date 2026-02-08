#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <utility> // for std::move

template <typename T>
class SafeQueue
{
public:
    SafeQueue() = default;
    ~SafeQueue() = default;

    // Prohibit copying: Deep copying a concurrent queue is rarely what you want.
    // In systems code, we pass these by reference or shared_ptr.
    SafeQueue(const SafeQueue &) = delete;
    SafeQueue &operator=(const SafeQueue &) = delete;
    
    // Lvalue Overload: Copies the value into a temporary, then moves it.
    void push(const T &value)
    {
        // Efficiently delegate to the rvalue overload
        push(T(value)); 
    }

    // Rvalue Overload: The "Move" master.
    void push(T &&value)
    {
        std::lock_guard<std::mutex> lk(m);
        data_queue.push(std::move(value)); // Taking ownership
        data_cond.notify_one();            // Wake up one waiting thread
    }

    // Blocking pop: Waits efficiently until an element is available
    T wait_and_pop()
    {
        std::unique_lock<std::mutex> lk(m);
        
        // CRITICAL FIX: Added [this] to capture the member variable
        data_cond.wait(lk, [this] { 
            return !data_queue.empty(); 
        });

        // Move the data out before popping
        T data = std::move(data_queue.front());
        data_queue.pop();
        return data;
    }

    // Non-blocking pop: Returns immediately with std::nullopt if empty
    std::optional<T> try_pop()
    {
        std::lock_guard<std::mutex> lk(m);
        if (data_queue.empty())
        {
            return std::nullopt;
        }
        
        // Move the data out
        T data = std::move(data_queue.front());
        data_queue.pop();
        return std::make_optional(std::move(data));
    }

    // Thread-safe empty check
    bool empty() const
    {
        std::lock_guard<std::mutex> lk(m);
        return data_queue.empty();
    }

private:
    std::queue<T> data_queue;
    mutable std::mutex m; // Mutable allows locking in 'const' methods like empty()
    std::condition_variable data_cond;
};
