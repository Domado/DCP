#ifndef __DCP_TEST_H__
#define __DCP_TEST_H__

#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <chrono>
#include <thread>
#include <mutex>

extern "C" {
#include "dcp_allocator.h"
#include "dcp_scheduler.h"
#include "dcp.h"
}

struct Packet {
    std::string data;
    uint32_t deliver_at_ms;
    struct DCPCB* target_dcp;
};

struct TestEndpoint {
    DCPCB *dcp;
    TestEndpoint *peer;
};

extern std::chrono::steady_clock::time_point g_start_time;
extern std::vector<Packet> g_network_queue;
extern std::mutex g_network_mutex;

uint32_t get_current_time_ms();

int test_output_callback(const char *buffer, int len, 
                         struct DCPCB *dcp, void *user);

#endif
