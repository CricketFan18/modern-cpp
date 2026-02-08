#include <iostream>
#include <thread>
#include <vector>
#include <functional>
#include <atomic>
#include <csignal>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <string>
#include <cstdint>
#include <cstring>
#include <charconv>
#include <string_view>
#include <unordered_map>
#include <unistd.h>      // close, read, write
#include <arpa/inet.h>   // ntohl, htonl
#include <sys/socket.h>  // socket, bind, listen, accept
#include <netinet/in.h>  // sockaddr_in
#include <poll.h>        // For non-blocking accept

// Global Shutdown Flag
std::atomic<bool> running{true};

void signal_handler(int) {
    std::cout << "\n[System] Shutdown signal received. Cleaning up...\n";
    running = false;
}

// ==========================================
// MODULE 2: SafeQueue (With Shutdown Logic)
// ==========================================
template <typename T>
class SafeQueue {
public:
    SafeQueue() = default;

    // Add a task
    void push(T value) {
        std::lock_guard<std::mutex> lk(m);
        data_queue.push(std::move(value));
        data_cond.notify_one();
    }

    // Blocking Pop with Shutdown Support
    // Returns std::nullopt if the queue is shut down and empty
    std::optional<T> wait_and_pop() {
        std::unique_lock<std::mutex> lk(m);
        
        // Wait until: Data is available OR Shutdown is triggered
        data_cond.wait(lk, [this] { 
            return !data_queue.empty() || shutdown_flag; 
        });

        // If shutdown and empty, return nothing (signal to exit thread)
        if (shutdown_flag && data_queue.empty()) {
            return std::nullopt;
        }

        T data = std::move(data_queue.front());
        data_queue.pop();
        return data;
    }

    // Call this to wake up all workers and tell them to quit
    void shutdown() {
        std::lock_guard<std::mutex> lk(m);
        shutdown_flag = true;
        data_cond.notify_all(); // Wake up EVERYONE
    }

private:
    std::queue<T> data_queue;
    std::mutex m;
    std::condition_variable data_cond;
    bool shutdown_flag = false;
};

// ==========================================
// MODULE 4: HeartbeatMonitor
// ==========================================
class HeartbeatMonitor {
public:
    HeartbeatMonitor(double timeout_seconds) : timeout_duration(timeout_seconds) {}

    void on_heartbeat(int client_id) {
        std::lock_guard<std::mutex> lk(m);
        last_seen[client_id] = std::chrono::steady_clock::now();
    }

    std::vector<int> get_dead_clients() {
        std::vector<int> dead_clients;
        std::lock_guard<std::mutex> lk(m);
        auto now = std::chrono::steady_clock::now();

        for (auto it = last_seen.begin(); it != last_seen.end();) {
            std::chrono::duration<double> time_span = now - it->second;
            if (time_span.count() > timeout_duration) {
                dead_clients.push_back(it->first);
                it = last_seen.erase(it);
            } else {
                ++it;
            }
        }
        return dead_clients;
    }

private:
    std::mutex m;
    std::unordered_map<int, std::chrono::steady_clock::time_point> last_seen;
    double timeout_duration;
};

// ==========================================
// MODULE 3: RpcServer (With Non-Blocking Accept)
// ==========================================
class RpcServer {
public:
    RpcServer(int port) {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) throw std::runtime_error("Failed to create socket");

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0)
            throw std::runtime_error("Bind failed");
        if (listen(server_fd, 100) < 0)
            throw std::runtime_error("Listen failed");
    }

    ~RpcServer() { if (server_fd >= 0) close(server_fd); }

    // NON-BLOCKING ACCEPT using poll()
    // Returns -1 if timeout or error, otherwise returns client_fd
    int accept_with_timeout(int timeout_ms) {
        struct pollfd pfd;
        pfd.fd = server_fd;
        pfd.events = POLLIN; // Check if data is ready to read (new connection)

        // Wait for 'timeout_ms'
        int ret = poll(&pfd, 1, timeout_ms);

        if (ret > 0 && (pfd.revents & POLLIN)) {
            // Safe to call accept without blocking forever
            sockaddr_in client_addr{};
            socklen_t addr_len = sizeof(client_addr);
            return accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        }
        return -1; // Timeout or no connection
    }

    std::string read_request(int client_fd) {
        uint32_t net_len = 0;
        if (!read_n_bytes(client_fd, &net_len, 4)) return "";
        uint32_t len = ntohl(net_len);
        if (len > 10 * 1024 * 1024) return ""; 

        std::vector<char> buffer(len);
        if (!read_n_bytes(client_fd, buffer.data(), len)) return "";
        return std::string(buffer.begin(), buffer.end());
    }

    void send_response(int client_fd, const std::string& response) {
        uint32_t len = response.size();
        uint32_t net_len = htonl(len);
        send(client_fd, &net_len, 4, 0);
        send(client_fd, response.data(), len, 0);
    }

    std::string handle_request(const std::string& request) {
        if (request.substr(0, 3) == "SUM") {
             int a, b;
             if (sscanf(request.c_str(), "SUM %d %d", &a, &b) == 2) {
                 return std::to_string(a + b);
             }
        }
        return "ERR_UNKNOWN_CMD";
    }

private:
    int server_fd;
    bool read_n_bytes(int client_socket, void* buffer, size_t n) {
        size_t total_read = 0;
        char* ptr = static_cast<char*>(buffer);
        while (total_read < n) {
            ssize_t read_bytes = recv(client_socket, ptr, n - total_read, 0);
            if (read_bytes <= 0) return false;
            total_read += read_bytes;
            ptr += read_bytes;
        }
        return true;
    }
};

// ==========================================
// MAIN INTEGRATION: CephSimServer
// ==========================================
class CephSimServer {
public:
    CephSimServer(int port, int num_workers)
        : rpc_server(port),
          monitor(5.0),
          worker_queue()
    {
        std::cout << "[Server] Launching " << num_workers << " worker threads.\n";
        
        // 1. Launch Worker Pool
        for (int i = 0; i < num_workers; ++i) {
            workers.emplace_back([this, i] {
                // Workers use blocking pop. They sleep until work arrives or shutdown happens.
                while (running) {
                    auto task_opt = worker_queue.wait_and_pop();
                    if (task_opt) {
                        (*task_opt)(); // Execute
                    } else {
                        // std::nullopt means Shutdown triggered and queue empty
                        std::cout << "[Worker " << i << "] Shutting down.\n";
                        break;
                    }
                }
            });
        }

        // 2. Launch Monitor Thread
        monitor_thread = std::thread([this] {
            while (running) {
                // Check running status every second
                for(int i=0; i<10; ++i) {
                    if(!running) return; 
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                
                auto dead_nodes = monitor.get_dead_clients();
                for (int id : dead_nodes) {
                    std::cout << "[Monitor] CRITICAL: Node " << id << " is DEAD.\n";
                }
            }
        });
    }

    // THE DESTRUCTOR: The Orchestrator of Graceful Shutdown
    ~CephSimServer() {
        std::cout << "[Server] Stopping acceptor loop...\n";
        running = false; // 1. Signal main loops to stop

        std::cout << "[Server] Waking up workers...\n";
        worker_queue.shutdown(); // 2. Wake up sleeping workers

        std::cout << "[Server] Joining threads...\n";
        if (monitor_thread.joinable()) monitor_thread.join();
        for (auto& t : workers) {
            if (t.joinable()) t.join();
        }
        std::cout << "[Server] Shutdown complete. Bye.\n";
    }

    void start() {
        std::cout << "[Server] Listening on port 8080 (Ctrl+C to stop)...\n";
        while (running) {
            // Check for connection with 1000ms timeout
            // This prevents the loop from hanging inside accept() forever
            int client_fd = rpc_server.accept_with_timeout(1000);
            
            if (client_fd >= 0) {
                worker_queue.push([this, client_fd]() {
                    this->handle_client(client_fd);
                });
            }
        }
    }

private:
    RpcServer rpc_server;
    HeartbeatMonitor monitor;
    SafeQueue<std::function<void()>> worker_queue;
    std::vector<std::thread> workers;
    std::thread monitor_thread;

    void handle_client(int fd) {
        std::string req = rpc_server.read_request(fd);
        if (req.find("HEARTBEAT") != std::string::npos) {
            int client_id = 0;
            if (sscanf(req.c_str(), "HEARTBEAT %d", &client_id) == 1) {
                monitor.on_heartbeat(client_id);
                rpc_server.send_response(fd, "ACK");
            }
        } else if (req.find("SUM") != std::string::npos) {
            std::string result = rpc_server.handle_request(req);
            rpc_server.send_response(fd, result);
        }
        close(fd);
    }
};

int main() {
    // Register signal handler for Ctrl+C
    std::signal(SIGINT, signal_handler);
    
    // Create server on stack. 
    // When main exits (or signal breaks the loop), destructor is called automatically.
    CephSimServer server(8080, 4); 
    
    try {
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << "\n";
    }
    
    return 0;
}