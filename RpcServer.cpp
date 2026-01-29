#include <string>
#include <vector>
#include <cstdint>

class RpcServer {
public:
    RpcServer(int port);
    ~RpcServer();

    // Starts the listen loop
    void run();

private:
    int server_fd;
    
    // Logic: Read exactly 'n' bytes from the socket
    // This handles the "Partial Read" problem.
    bool read_n_bytes(int client_socket, std::vector<uint8_t>& buffer, size_t n);

    // Logic: Process the command and return a result string
    std::string handle_request(const std::string& request);
    
    // Logic: Send a response with the length-prefix
    void send_response(int client_socket, const std::string& response);
};