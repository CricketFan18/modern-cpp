#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <charconv>
#include <string_view>
#include <unistd.h>      // close, read, write
#include <arpa/inet.h>   // ntohl, htonl
#include <sys/socket.h>  // socket, bind, listen, accept
#include <netinet/in.h>  // sockaddr_in

class RpcServer {
public:
    // 1. Constructor: Sets up the Listening Socket
    RpcServer(int port) {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            throw std::runtime_error("Failed to create socket");
        }

        // Critical: Allow the port to be reused immediately after restart
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY; // Listen on 0.0.0.0
        address.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            throw std::runtime_error("Bind failed");
        }

        if (listen(server_fd, 100) < 0) { // Backlog of 100 pending connections
            throw std::runtime_error("Listen failed");
        }
    }

    // 2. Destructor: Cleanup
    ~RpcServer() {
        if (server_fd >= 0) close(server_fd);
    }

    // 3. Accept Logic: Returns a client file descriptor (blocking)
    // Used by the main loop to hand off connections to threads
    int accept_connection() {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        return client_fd; // Returns -1 on error, which we handle in main
    }

    // 4. Read Logic: Handles the "Length-Prefix" framing
    // Returns the raw string payload ("SUM 10 20")
    std::string read_request(int client_fd) {
        uint32_t net_len = 0;
        
        // Step A: Read the 4-byte header
        if (!read_n_bytes(client_fd, &net_len, 4)) {
            return ""; // Connection closed or error
        }

        // Step B: Convert Network Byte Order -> Host Byte Order
        uint32_t len = ntohl(net_len);

        // Security Check: Prevent buffer overflow attacks
        if (len > 10 * 1024 * 1024) { // 10MB Limit
            std::cerr << "Oversized packet detected! Dropping.\n";
            return "";
        }

        // Step C: Read the Payload
        std::vector<char> buffer(len);
        if (!read_n_bytes(client_fd, buffer.data(), len)) {
            return "";
        }

        return std::string(buffer.begin(), buffer.end());
    }

    // 5. Send Logic: Wraps response in "Length-Prefix" frame
    void send_response(int client_fd, const std::string &response) {
        // Step A: Create Header (Host -> Network Byte Order)
        uint32_t len = response.size();
        uint32_t net_len = htonl(len);

        // Step B: Send Header
        send(client_fd, &net_len, 4, 0);

        // Step C: Send Payload
        send(client_fd, response.data(), len, 0);
    }

    // 6. The "Brain": Parsing Logic (Your Implementation)
    std::string handle_request(const std::string &request) {
        std::string_view sv(request);

        // Helper to trim leading spaces
        auto trim_leading = [&](std::string_view& v) {
            auto first = v.find_first_not_of(' ');
            if (first != std::string_view::npos) {
                v.remove_prefix(first);
            }
        };

        trim_leading(sv);
        if (sv.empty()) return "ERR_EMPTY_REQ";

        // Extract Command
        auto cmd_end = sv.find(' ');
        std::string_view cmd = sv.substr(0, cmd_end);
        
        if (cmd_end != std::string_view::npos) {
            sv.remove_prefix(cmd_end);
        } else {
            sv = {}; 
        }

        if (cmd == "SUM") {
            int num1 = 0, num2 = 0;

            // Extract Number 1
            trim_leading(sv);
            if (sv.empty()) return "ERR_MISSING_ARGS";
            
            auto [ptr1, ec1] = std::from_chars(sv.data(), sv.data() + sv.size(), num1);
            if (ec1 != std::errc()) return "ERR_PARSE_NUM1";

            // Safe Advance: Use the pointer returned by from_chars
            sv.remove_prefix(ptr1 - sv.data()); 

            // Extract Number 2
            trim_leading(sv);
            if (sv.empty()) return "ERR_MISSING_ARG2";

            auto [ptr2, ec2] = std::from_chars(sv.data(), sv.data() + sv.size(), num2);
            if (ec2 != std::errc()) return "ERR_PARSE_NUM2";

            return std::to_string(num1 + num2);
        }
        
        // Heartbeat handling for the integration
        if (cmd == "HEARTBEAT") {
            return "ACK";
        }

        return "ERR_UNKNOWN_CMD";
    }

private:
    int server_fd;

    // The "Engine": Reads exactly 'n' bytes or fails
    bool read_n_bytes(int client_socket, void *buffer, size_t n) {
        size_t total_read = 0;
        char *ptr = static_cast<char *>(buffer);
        
        while (total_read < n) {
            ssize_t read_bytes = recv(client_socket, ptr, n - total_read, 0);

            if (read_bytes == 0) return false; // Disconnect
            if (read_bytes == -1) {
                if (errno == EINTR) continue; // Interrupted, retry
                return false; // Fatal error
            }

            total_read += read_bytes;
            ptr += read_bytes; // Advance the pointer forward in memory
        }
        return true;
    }
};