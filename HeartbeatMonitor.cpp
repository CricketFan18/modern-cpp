#include <unordered_map>
#include <chrono>
#include <mutex>
#include <vector>
#include <iostream>

class HeartbeatMonitor
{
public:
    // Config: How many seconds before we declare a node dead?
    HeartbeatMonitor(double timeout_seconds) 
        : timeout_duration(timeout_seconds) {}

    // Called when a UDP packet arrives from a clientID
    void on_heartbeat(int client_id)
    {
        std::lock_guard<std::mutex> lk(m);
        // Update the timestamp to "now"
        // If the client_id doesn't exist, this creates it (Node Join)
        last_seen[client_id] = std::chrono::steady_clock::now();
    }

    // Called periodically by a background thread to check for dead nodes
    // Returns a list of dead client IDs
    std::vector<int> get_dead_clients()
    {
        std::vector<int> dead_clients;
        std::lock_guard<std::mutex> lk(m);
        auto now = std::chrono::steady_clock::now();

        // Explicit iterator loop to safely handle erasure
        for (auto it = last_seen.begin(); it != last_seen.end();) 
        {
            // Calculate time elapsed since last heartbeat
            std::chrono::duration<double> time_span = now - it->second;
            
            if (time_span.count() > timeout_duration)
            {
                dead_clients.push_back(it->first);
                // erase() invalidates the current iterator but returns the next valid one
                it = last_seen.erase(it); 
            }
            else
            {
                ++it; // Only increment if we didn't erase
            }
        }
        return dead_clients;
    }

private:
    std::mutex m;
    // Map: ClientID -> Last Timestamp (using steady_clock)
    std::unordered_map<int, std::chrono::steady_clock::time_point> last_seen;
    double timeout_duration;
};