#include "dcp.h"
#include <string.h>

static void list_init_seg_head(DCPSEG *head) {
    head->next = head;
    head->prev = head;
}

static void list_add_tail_seg(DCPSEG *head, DCPSEG *node) {
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}

static void list_del_seg(DCPSEG *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

static DCPSEG* dcp_seg_create(DCPCB *dcp, int size) {
    dcp_malloc_fn malloc_fn = dcp_get_malloc();
    int data_size = (size < 0) ? 0 : size;
    DCPSEG *seg = (DCPSEG*)malloc_fn(sizeof(DCPSEG) + data_size);
    if (seg == NULL) return NULL;
    memset(seg, 0, sizeof(DCPSEG));
    seg->len = data_size;
    return seg;
}

static void dcp_seg_free(DCPCB *dcp, DCPSEG *seg) {
    if (seg) {
        dcp_get_free()(seg);
    }
}

static void dcp_bbr_init(DCPCB *dcp) {
    dcp_malloc_fn malloc_fn = dcp_get_malloc();
    DCPBBRState *bbr_state = (DCPBBRState*)malloc_fn(sizeof(DCPBBRState));
    memset(bbr_state, 0, sizeof(DCPBBRState));
    dcp->congestion_control_state = bbr_state;
    dcp->pace_rate_bytes_per_sec = 500 * 1024;
}

static void dcp_bbr_release(DCPCB *dcp) {
    if (dcp->congestion_control_state) {
        dcp_get_free()(dcp->congestion_control_state);
        dcp->congestion_control_state = NULL;
    }
}

static void dcp_bbr_on_ack(DCPCB *dcp, int32_t rtt_sample, uint32_t bytes_acked, uint32_t now) {
    
}

static void dcp_bbr_on_loss(DCPCB *dcp, uint32_t lost_sn, uint32_t now) {

}

static uint32_t dcp_bbr_get_cwnd(DCPCB *dcp) {
    return 128 * dcp->mss;
}

static uint64_t dcp_bbr_get_pacing_rate(DCPCB *dcp) {
    return dcp->pace_rate_bytes_per_sec; 
}

static void dcp_bbr_on_pkt_sent(DCPCB *dcp, uint32_t bytes_sent) {

}

static const struct dcp_cc_ops cc_bbr_ops = {
    .init = dcp_bbr_init,
    .release = dcp_bbr_release,
    .on_ack = dcp_bbr_on_ack,
    .on_loss = dcp_bbr_on_loss,
    .on_pkt_sent = dcp_bbr_on_pkt_sent,
    .get_cwnd = dcp_bbr_get_cwnd,
    .get_pacing_rate = dcp_bbr_get_pacing_rate
};


static void dcp_on_rto_timeout(DCPCB *dcp, uint32_t now) {
    if (dcp->is_released) return;
    
    dcp->rx_rto *= 2;
    
    if (dcp->cc_ops && dcp->cc_ops->on_loss) {
        dcp->cc_ops->on_loss(dcp, dcp->snd_una, now);
    }
    
    dcp_scheduler_add(dcp->scheduler, dcp, dcp->rx_rto, dcp_on_rto_timeout);
}

static void dcp_on_ack_delay_timeout(DCPCB *dcp, uint32_t now) {
    if (dcp->is_released) return;

    dcp->ack_delayed_until = 0;
}


DCPCB* dcp_create(uint32_t conv_id, uint32_t token, void *user, 
                  DCPScheduler *scheduler) {
                      
    dcp_malloc_fn malloc_fn = dcp_get_malloc();
    
    if (scheduler == NULL) return NULL;

    DCPCB *dcp = (DCPCB*)malloc_fn(sizeof(DCPCB));
    if (dcp == NULL) return NULL;
    
    memset(dcp, 0, sizeof(DCPCB));

    dcp->user = user;
    dcp->conv_id = conv_id;
    dcp->token = token;
    dcp->scheduler = scheduler;
    dcp->is_released = 0;
    
    dcp->mtu = DCP_MTU_DEF;
    dcp->mss = DCP_MTU_DEF - DCP_OVERHEAD;

    dcp->rx_minrto = 100;
    dcp->rx_rto = 200;
    dcp->snd_wnd = 32;
    dcp->rcv_wnd = 128;
    dcp->rmt_wnd = 128;
    dcp->fastresend = 2;
    
    list_init_seg_head(&dcp->snd_queue_head);
    list_init_seg_head(&dcp->rcv_queue_head);
    list_init_seg_head(&dcp->snd_buf_head);
    list_init_seg_head(&dcp->rcv_buf_head);

    dcp_set_congestion_control(dcp, "bbr");
    
    return dcp;
}

static void dcp_flush_queue(DCPCB *dcp, struct DCPSEG *head) {
    DCPSEG *node = head->next;
    while (node != head) {
        DCPSEG *to_free = node;
        node = node->next;
        dcp_seg_free(dcp, to_free);
    }
    list_init_seg_head(head);
}

void dcp_release(DCPCB *dcp) {
    if (dcp == NULL || dcp->is_released) return;

    dcp->is_released = 1;

    if (dcp->cc_ops && dcp->cc_ops->release) {
        dcp->cc_ops->release(dcp);
    }

    dcp_flush_queue(dcp, &dcp->snd_queue_head);
    dcp_flush_queue(dcp, &dcp->rcv_queue_head);
    dcp_flush_queue(dcp, &dcp->snd_buf_head);
    dcp_flush_queue(dcp, &dcp->rcv_buf_head);
    
    dcp_get_free()(dcp);
}

void dcp_set_output(DCPCB *dcp, dcp_output_callback output) {
    if (dcp) dcp->output = output;
}

int dcp_set_congestion_control(DCPCB *dcp, const char *algo_name) {
    if (dcp == NULL || algo_name == NULL) return -1;
    
    if (dcp->cc_ops && dcp->cc_ops->release) {
        dcp->cc_ops->release(dcp);
    }
    
    if (strcmp(algo_name, "bbr") == 0) {
        dcp->cc_ops = &cc_bbr_ops;
        dcp->cc_ops->init(dcp);
    } else {
        return -1;
    }
    return 0;
}

int dcp_setmtu(DCPCB *dcp, int mtu) {
    if (dcp == NULL || mtu < (DCP_OVERHEAD + 1)) return -1;
    dcp->mtu = mtu;
    dcp->mss = dcp->mtu - DCP_OVERHEAD;
    return 0;
}


int dcp_input(DCPCB *dcp, const char *data, long size, uint32_t now) {
    if (dcp == NULL || dcp->is_released || data == NULL || size < (long)DCP_OVERHEAD) {
        return -1;
    }
    
    return 0;
}

int dcp_send(DCPCB *dcp, const char *buffer, int len, uint32_t now) {
    if (dcp == NULL || dcp->is_released || len <= 0) return -1;

    int count = 0;
    if (len <= (int)dcp->mss) {
        count = 1;
    } else {
        count = (len + dcp->mss - 1) / dcp->mss;
    }
    
    if (dcp->snd_queue_len + count > dcp->snd_wnd * 2) {
        return -2;
    }

    int offset = 0;
    for (int i = 0; i < count; i++) {
        int size = (len > (int)dcp->mss) ? (int)dcp->mss : len;
        DCPSEG *seg = dcp_seg_create(dcp, size);
        if (seg == NULL) return -3;
        
        memcpy(seg->data, buffer + offset, size);
        offset += size;
        len -= size;
        
        seg->frg = (count - 1) - i;
        
        list_add_tail_seg(&dcp->snd_queue_head, seg);
        dcp->snd_queue_len++;
    }

    return 0;
}

int dcp_recv(DCPCB *dcp, char *buffer, int len) {
    if (dcp == NULL || dcp->is_released) return -1;

    if (dcp->rcv_queue_head.next == &dcp->rcv_queue_head) {
        return 0;
    }

    int peeksize = 0;
    DCPSEG *node = dcp->rcv_queue_head.next;
    while(node != &dcp->rcv_queue_head) {
        peeksize += node->len;
        if (node->frg == 0) break;
        node = node->next;
    }
    
    if (peeksize <= 0 || peeksize > len) {
        return -2;
    }

    int recovered_len = 0;
    while (dcp->rcv_queue_head.next != &dcp->rcv_queue_head) {
        DCPSEG *seg = dcp->rcv_queue_head.next;
        list_del_seg(seg);
        
        if (buffer) {
            memcpy(buffer + recovered_len, seg->data, seg->len);
        }
        recovered_len += seg->len;
        
        uint32_t frg = seg->frg;
        dcp_seg_free(dcp, seg);
        dcp->rcv_queue_len--;
        
        if (frg == 0) {
            break;
        }
    }
    
    return recovered_len;
}
