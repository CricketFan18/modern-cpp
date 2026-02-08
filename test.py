import socket, struct, time

def send_cmd(cmd):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(('localhost', 8080))
    msg = cmd.encode()
    s.send(struct.pack('!I', len(msg)) + msg)
    resp_len = struct.unpack('!I', s.recv(4))[0]
    print(f"Server Replied: {s.recv(resp_len).decode()}")
    s.close()

# Test Calculation
send_cmd("SUM 10 20")

# Test Heartbeat
send_cmd("HEARTBEAT 1") 
print("Waiting 6 seconds to trigger monitor failure...")
time.sleep(6) 
# Check server terminal -> Should see "CRITICAL: Node 1 is DEAD"