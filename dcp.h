#ifndef __DCP_H__
#define __DCP_H__

#include <stddef.h>
#include <stdint.h>
#include "dcp_scheduler.h"

#define DCP_CMD_PUSH     81
#define DCP_CMD_ACK      82
#define DCP_CMD_PROBE    85

#define DCP_OVERHEAD     24
#define DCP_MTU_DEF      1400

struct DCPCB;

typedef int (*dcp_output_callback)(const char *buffer, int len, 
                                   struct DCPCB *dcp, void *user);

struct dcp_cc_ops {
    void (*init)(struct DCPCB *dcp);
    void (*release)(struct DCPCB *dcp);
    void (*on_ack)(struct DCPCB *dcp, int32_t rtt_sample, uint32_t bytes_acked, uint32_t now);
    void (*on_loss)(struct DCPCB *dcp, uint32_t lost_sn, uint32_t now);
    void (*on_pkt_sent)(struct DCPCB *dcp, uint32_t bytes_sent);
    uint32_t (*get_cwnd)(struct DCPCB *dcp);
    uint64_t (*get_pacing_rate)(struct DCPCB *dcp);
};

typedef struct {
    int state; 
    uint64_t btl_bw;
    uint32_t rt_prop;
} DCPBBRState;

typedef struct DCPSEG {
    struct DCPSEG *prev, *next;
    uint32_t conv_id;
    uint32_t cmd;
    uint32_t frg;
    uint32_t wnd;
    uint32_t ts;
    uint32_t sn;
    uint32_t una;
    uint32_t len;
    uint32_t resendts;
    uint32_t rto;
    uint32_t fastack;
    uint32_t xmit;
    char data[1];
} DCPSEG;

typedef struct DCPCB {
    void *user;
    uint32_t conv_id;
    uint32_t token;
    uint32_t state;
    uint32_t mtu;
    uint32_t mss;
    
    DCPScheduler *scheduler;
    int is_released;

    uint32_t snd_una;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint32_t snd_wnd;
    uint32_t rcv_wnd;
    uint32_t rmt_wnd;

    int32_t rx_rttval;
    int32_t rx_srtt;
    int32_t rx_rto;
    int32_t rx_minrto;

    const struct dcp_cc_ops *cc_ops;
    void *congestion_control_state;
    
    uint64_t pace_rate_bytes_per_sec;
    uint64_t next_send_time_us;

    dcp_output_callback output;

    struct DCPSEG snd_queue_head;
    struct DCPSEG rcv_queue_head;
    struct DCPSEG snd_buf_head;
    struct DCPSEG rcv_buf_head;
    
    uint32_t snd_queue_len;
    uint32_t rcv_queue_len;
    uint32_t snd_buf_len;
    uint32_t rcv_buf_len;

    uint32_t ack_delayed_until;
    uint32_t ack_count;
    uint32_t ack_list[128];
    
    uint32_t fastresend;
    int32_t nocwnd;

} DCPCB;

DCPCB* dcp_create(uint32_t conv_id, uint32_t token, void *user, 
                  DCPScheduler *scheduler);

void dcp_release(DCPCB *dcp);

void dcp_set_output(DCPCB *dcp, dcp_output_callback output);

int dcp_input(DCPCB *dcp, const char *data, long size, uint32_t now);

int dcp_send(DCPCB *dcp, const char *buffer, int len, uint32_t now);

int dcp_recv(DCPCB *dcp, char *buffer, int len);

int dcp_set_congestion_control(DCPCB *dcp, const char *algo_name);

int dcp_setmtu(DCPCB *dcp, int mtu);

#endif
