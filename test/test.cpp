#include "test.h"
#include <stdlib.h> 

std::chrono::steady_clock::time_point g_start_time;
std::vector<Packet> g_network_queue;
std::mutex g_network_mutex;

const uint32_t SIMULATED_LATENCY_MS = 30;
const double SIMULATED_PACKET_LOSS_RATE = 0.1;

uint32_t get_current_time_ms() {
    auto now = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - g_start_time).count();
}

int test_output_callback(const char *buffer, int len, 
                         struct DCPCB *dcp, void *user) {
    
    TestEndpoint *self = (TestEndpoint*)user;
    if (self == nullptr || self->peer == nullptr) {
        return -1;
    }
    
    if (((double)rand() / RAND_MAX) < SIMULATED_PACKET_LOSS_RATE) {
        std::cout << "[Network] Packet DROPPED (Loss " << SIMULATED_PACKET_LOSS_RATE * 100 << "%)" << std::endl;
        return 0;
    }

    uint32_t jitter = (uint32_t)(rand() % 10);
    uint32_t deliver_at = get_current_time_ms() + SIMULATED_LATENCY_MS + jitter;

    Packet pkt;
    pkt.data.assign(buffer, len);
    pkt.deliver_at_ms = deliver_at;
    pkt.target_dcp = self->peer->dcp;
    
    {
        std::lock_guard<std::mutex> lock(g_network_mutex);
        g_network_queue.push_back(pkt);
    }
    
    return 0;
}

int main(int argc, char **argv) {
    std::cout << "--- DCP Protocol Test Harness ---" << std::endl;
    
    g_start_time = std::chrono::steady_clock::now();
    
    DCPScheduler* scheduler = dcp_scheduler_create();
    if (scheduler == nullptr) {
        std::cerr << "Failed to create scheduler" << std::endl;
        return -1;
    }
    
    TestEndpoint endpoint_a;
    TestEndpoint endpoint_b;
    
    endpoint_a.peer = &endpoint_b;
    endpoint_b.peer = &endpoint_a;
    
    uint32_t conv = 12345;
    
    endpoint_a.dcp = dcp_create(conv, 0, &endpoint_a, scheduler);
    endpoint_b.dcp = dcp_create(conv, 0, &endpoint_b, scheduler);

    dcp_set_output(endpoint_a.dcp, test_output_callback);
    dcp_set_output(endpoint_b.dcp, test_output_callback);
    
    dcp_setmtu(endpoint_a.dcp, 1400);
    dcp_setmtu(endpoint_b.dcp, 1400);
    
    std::string test_message = "Hello, this is DCP! This message is repeated to be larger than MTU. ";
    for (int i = 0; i < 10; ++i) {
        test_message += "Payload part " + std::to_string(i) + ". ";
    }
    
    uint32_t start_time = get_current_time_ms();
    
    int ret = dcp_send(endpoint_a.dcp, test_message.c_str(), test_message.length(), start_time);
    if (ret < 0) {
        std::cerr << "dcp_send failed: " << ret << std::endl;
        return -1;
    }
    
    std::cout << "[Test] Sent message from A (" << test_message.length() << " bytes)." << std::endl;
    
    char recv_buffer[4096];
    std::string received_data;
    bool success = false;
    
    const uint32_t test_duration_ms = 10000;
    
    while (get_current_time_ms() < test_duration_ms) {
        uint32_t now = get_current_time_ms();
        
        dcp_scheduler_run(scheduler, now);
        
        {
            std::lock_guard<std::mutex> lock(g_network_mutex);
            for (auto it = g_network_queue.begin(); it != g_network_queue.end(); ) {
                if (now >= it->deliver_at_ms) {
                    dcp_input(it->target_dcp, it->data.c_str(), it->data.length(), now);
                    it = g_network_queue.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
        int n = dcp_recv(endpoint_b.dcp, recv_buffer, sizeof(recv_buffer));
        if (n > 0) {
            received_data.append(recv_buffer, n);
            std::cout << "[Test] Received " << n << " bytes at B. Total " << received_data.length() << std::endl;
            
            if (received_data.length() == test_message.length()) {
                if (received_data == test_message) {
                    std::cout << "\n--- SUCCESS ---" << std::endl;
                    std::cout << "Message integrity verified." << std::endl;
                    success = true;
                } else {
                    std::cout << "\n--- FAILURE ---" << std::endl;
                    std::cout << "Data corruption detected." << std::endl;
                }
                break;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(DCP_TIMER_RESOLUTION));
    }

    if (!success) {
        std::cout << "\n--- FAILURE ---" << std::endl;
        std::cout << "Test timed out after " << test_duration_ms << "ms." << std::endl;
        std::cout << "Expected " << test_message.length() << " bytes, but received " << received_data.length() << std::endl;
    }

    dcp_release(endpoint_a.dcp);
    dcp_release(endpoint_b.dcp);
    dcp_scheduler_release(scheduler);

    std::cout << "--- Test Harness Finished ---" << std::endl;
    
    return success ? 0 : -1;
}
