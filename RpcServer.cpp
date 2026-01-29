#include <string>
#include <vector>
#include <cstdint>
#include <arpa/inet.h> // for ntohl/htonl
#include <charconv>
#include <string_view>

class RpcServer
{
public:
    RpcServer(int port);
    ~RpcServer();

    // The main loop: Accept connections and process them
    void run();

private:
    int server_fd;

    // The "Engine": Reads exactly 'n' bytes or fails
    // This is the most critical function in networking.
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

    // The "Brain": Parses "SUM 10 20" -> returns "30"
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

        // Move past digits of num1
        // ptr1 points to the first character that was NOT part of num1
        sv.remove_prefix(ptr1 - sv.data());

        // Extract Number 2
        trim_leading(sv);
        if (sv.empty()) return "ERR_MISSING_ARG2";

        auto [ptr2, ec2] = std::from_chars(sv.data(), sv.data() + sv.size(), num2);
        if (ec2 != std::errc()) return "ERR_PARSE_NUM2";

        return std::to_string(num1 + num2);
    }

    return "ERR_UNKNOWN_CMD";
}

    // The "Speaker": Packages the response with a length-prefix
    void send_response(int client_socket, const std::string &response);
};