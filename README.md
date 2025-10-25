# DCP (Daiso Control Protocol)

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](https://github.com/your-username/dcp/actions)
[![License](https://img.shields.io/badge/license-MIT-blue)](https://opensource.org/licenses/MIT)
[![Version](https://img.shields.io/badge/version-0.1.0-informational)](./)

🌏 Reliable, fast, and advanced TCP protocol, made by Daiso

**DCP is a high-performance, event-driven, reliable UDP (RUDP) protocol written in C. It is designed from the ground up to overcome the CPU and congestion control limitations of traditional RUDP protocols, making it ideal for modern, high-load, and unstable network environments (e.g., 5G, IoT, and real-time gaming).**

---

## The Problem with KCP

KCP (KCP - A Fast and Reliable ARQ Protocol) is an excellent protocol renowned for its extremely low latency in real-time applications. However, it was designed with a philosophy that has two major drawbacks in modern, large-scale systems:

1.  **High CPU Usage at Scale:** KCP's core design relies on the user application to *manually and frequently* call `ikcp_update` for *every single connection*. This polling-based model has an `O(N)` complexity. For a server handling 10,000, 100,000, or 1,000,000 connections, this becomes the primary CPU bottleneck, as the server wastes cycles polling idle connections.
2.  **Outdated Congestion Control:** KCP uses a fast-retransmit ARQ model with congestion control similar to TCP Reno. This model is **loss-based**, meaning it assumes packet loss is a sign of network congestion. On modern wireless networks (5G, Wi-Fi, satellite), packet loss is common due to interference, *not* congestion. KCP/Reno will incorrectly "back off" and throttle its send rate, resulting in poor throughput.

## The DCP Solution

DCP is an evolution of TCP's low-latency ARQ principles, but with a completely re-architected core to solve these problems. It is built on two key principles: **efficiency** and **intelligence**.

* **Efficiency (Low CPU):** DCP is **event-driven**, not poll-based.
* **Intelligence (Smart CC):** DCP uses a **modern, bandwidth-based** congestion control algorithm (BBR-ready) that is decoupled from the ARQ logic.

## Key Features

### 🚀 O(1) Event-Driven Scheduling (Low CPU)
* **Principle:** DCP replaces KCP's `ikcp_update` polling with a central, `O(1)` **Timing Wheel Scheduler** (`dcp_scheduler`).
* **Advantage:** A `DCPCB` (control block) only registers a timer (for RTO, Pacing, or ACK delays) with the scheduler when it needs one. The server calls a *single* `dcp_scheduler_run` function per tick. This design scales to millions of connections with near-zero idle-connection overhead.

### 🧠 Smart, Pluggable Congestion Control (BBR-Ready)
* **Principle:** The ARQ logic is completely decoupled from the congestion control (CC) logic via a virtual function table (`dcp_cc_ops`).
* **Advantage:** This allows DCP to use any CC algorithm. It is designed to use **Google's BBR (Bottleneck Bandwidth and Round-trip propagation time)**. Unlike KCP/Reno, BBR is **bandwidth-based**. It doesn't mistake packet loss for congestion and actively probes for available bandwidth, providing *vastly* superior performance on lossy or high-jitter networks.

### ⚡ Integrated Packet Pacing
* **Principle:** To avoid bursting (which causes packet loss) and to enable BBR's probing, DCP has a natively integrated packet pacer.
* **Advantage:** `dcp_send()` places data in a queue, and the pacer, driven by the event scheduler and the BBR module's `get_pacing_rate()` function, sends packets out smoothly at the correct rate.

### 🛡️ Advanced Jitter & Loss Resilience
* **Principle:** DCP implements **SACK (Selective Acknowledgments)** and **Delayed ACKs**.
* **Advantage:** SACK prevents re-transmitting packets that were received out-of-order (a common symptom of network jitter). Delayed ACKs reduce bandwidth overhead by not sending an ACK for every single packet, instead bundling them or sending them on a short timer.

## Technical Principles: Architecture

DCP's architecture is fundamentally different from KCP's. It is an event-driven, pacing-first model.

```ascii
 [ User App ]
      |
      | 1. dcp_send()
      v
[ snd_queue ]
      |
      | 2. Pacing Timer Fires (via dcp_scheduler_run)
      v
+------------------+
| dcp_flush_data() |
| - Asks CC module | -> [ BBR Module ]
| - Gets Pacing    | <- (pacing_rate)
| - Sends 1 packet |
+------------------+
      |
      | 3. dcp->output()
      v
[ (Network) ] -> [ dcp_input() ] -> [ rcv_buf / rcv_queue ]
      |                 |
      |                 | 4. (If ACK Pkt)
      |                 v
      |            [ BBR Module ]
      |            (on_ack_recv)
      |
      | 5. (If Data Pkt)
      | -> (Triggers Delayed ACK Timer)
      |
      | 6. RTO Timer Fires (via dcp_scheduler_run)
      v
[ dcp_on_rto_timeout() ]
      |
      v
[ (Retransmit Packet) ]
```

## How to Build & Test
The project includes C source files and a C++ test harness (test.cpp) that creates a simulated network with loss and latency.

## Build
You need a C++ compiler (like g++ or clang++) to link the C and C++ files.

```Bash

# Compile the C and C++ files together
g++ -o dcp_test test.cpp \
    -x c dcp.c \
    -x c dcp_scheduler.c \
    -x c dcp_allocator.c \
    -I. -std=c++11 -lpthread
```

 The -x c flag tells g++ to compile .c files as C
 -I. includes the current directory for .h files
## Run Test
The test harness will simulate two endpoints and a lossy network, then verify data integrity.

```Bash

./dcp_test
Expected Output:


--- DCP Protocol Test Harness ---
[Test] Sent message from A (1024 bytes).
[Network] Packet DROPPED (Loss 10.0%)
...
[Test] Received 512 bytes at B. Total 512
[Test] Received 512 bytes at B. Total 1024
--- SUCCESS ---
Message integrity verified.
--- Test Harness Finished ---
```
## How to Contribute
Contributions are welcome! This project is in its early stages. The most critical area for contribution is the implementation of the BBR congestion control state machine within dcp_bbr_on_ack and dcp_bbr_on_loss.

## License
This project is licensed under the MIT License. See the LICENSE file for details.
