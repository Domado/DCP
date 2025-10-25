#include "dcp.h"
#include <string.h>
#include <stdlib.h>

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

static void list_add_before(DCPSEG *pos, DCPSEG *node) {
    node->next = pos;
    node->prev = pos->prev;
    pos->prev->next = node;
    pos->prev = node;
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

static inline void _dcp_encode_32u(char *p, uint32_t v) {
    p[0] = (char)(v >> 24);
    p[1] = (char)(v >> 16);
    p[2] = (char)(v >> 8);
    p[3] = (char)(v);
}

static inline void _dcp_decode_32u(const char *p, uint32_t *v) {
    *v = ((uint32_t)(unsigned char)p[0] << 24) |
         ((uint32_t)(unsigned char)p[1] << 16) |
         ((uint32_t)(unsigned char)p[2] << 8) |
         ((uint32_t)(unsigned char)p[3]);
}

static char* dcp_encode_seg(char *ptr, const DCPSEG *seg) {
    _dcp_encode_32u(ptr, seg->conv_id); ptr += 4;
    _dcp_encode_32u(ptr, seg->cmd);     ptr += 4;
    _dcp_encode_32u(ptr, seg->frg);     ptr += 4;
    _dcp_encode_32u(ptr, seg->wnd);     ptr += 4;
    _dcp_encode_32u(ptr, seg->ts);      ptr += 4;
    _dcp_encode_32u(ptr, seg->sn);      ptr += 4;
    _dcp_encode_32u(ptr, seg->una);     ptr += 4;
    _dcp_encode_32u(ptr, seg->len);     ptr += 4;
    return ptr;
}

static const char* dcp_decode_seg(const char *ptr, DCPSEG *seg) {
    _dcp_decode_32u(ptr, &seg->conv_id); ptr += 4;
    _dcp_decode_32u(ptr, &seg->cmd);     ptr += 4;
    _dcp_decode_32u(ptr, &seg->frg);     ptr += 4;
    _dcp_decode_32u(ptr, &seg->wnd);     ptr += 4;
    _dcp_decode_32u(ptr, &seg->ts);      ptr += 4;
    _dcp_decode_32u(ptr, &seg->sn);      ptr += 4;
    _dcp_decode_32u(ptr, &seg->una);     ptr += 4;
    _dcp_decode_32u(ptr, &seg->len);     ptr += 4;
    return ptr;
}

static int _dcp_output_seg(DCPCB *dcp, DCPSEG *seg) {
    if (dcp->output == NULL) return -1;
    
    char buffer[DCP_MTU_DEF];
    if (seg->len + DCP_OVERHEAD > dcp->mtu) return -2;
    
    char *ptr = dcp_encode_seg(buffer, seg);
    if (seg->len > 0) {
        memcpy(ptr, seg->data, seg->len);
    }
    
    return dcp->output(buffer, seg->len + DCP_OVERHEAD, dcp, dcp->user);
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
    uint32_t cwnd_bytes = 32 * dcp->mss;
    uint32_t rmt_wnd_bytes = dcp->rmt_wnd * dcp->mss;
    
    if (dcp->nocwnd == 0) {
        cwnd_bytes = (cwnd_bytes < rmt_wnd_bytes) ? cwnd_bytes : rmt_wnd_bytes;
    }
    
    return cwnd_bytes;
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

static void dcp_flush_data(DCPCB *dcp, uint32_t now);
static void dcp_on_rto_timeout(DCPCB *dcp, uint32_t now);

static void dcp_on_rto_timeout(DCPCB *dcp, uint32_t now) {
    if (dcp->is_released) return;
    dcp->rto_timer_armed = 0;

    if (dcp->snd_buf_head.next == &dcp->snd_buf_head) {
        return;
    }

    DCPSEG *seg = dcp->snd_buf_head.next;
    
    dcp->rx_rto *= 2;
    if (dcp->rx_rto > 60000) dcp->rx_rto = 60000;
    
    seg->rto = dcp->rx_rto;
    seg->xmit++;
    seg->ts = now;
    seg->wnd = dcp->rcv_queue_len;
    seg->una = dcp->rcv_nxt;
    
    _dcp_output_seg(dcp, seg);
    
    if (dcp->cc_ops && dcp->cc_ops->on_loss) {
        dcp->cc_ops->on_loss(dcp, seg->sn, now);
    }
    
    if (dcp->snd_buf_head.next != &dcp->snd_buf_head) {
        dcp_scheduler_add(dcp->scheduler, dcp, dcp->rx_rto, dcp_on_rto_timeout);
        dcp->rto_timer_armed = 1;
    }
}

static void dcp_on_ack_delay_timeout(DCPCB *dcp, uint32_t now) {
    if (dcp->is_released) return;
    dcp->ack_delayed_until = 0;
    
    DCPSEG ack_seg;
    memset(&ack_seg, 0, sizeof(DCPSEG));
    ack_seg.conv_id = dcp->conv_id;
    ack_seg.cmd = DCP_CMD_ACK;
    ack_seg.wnd = dcp->rcv_queue_len;
    ack_seg.una = dcp->rcv_nxt;
    
    _dcp_output_seg(dcp, &ack_seg);
}

static void dcp_flush_data(DCPCB *dcp, uint32_t now) {
    if (dcp->is_released) return;
    dcp->pacing_timer_armed = 0;

    if (dcp->snd_queue_head.next == &dcp->snd_queue_head) {
        return;
    }
    
    uint32_t cwnd_pkts = dcp->cc_ops->get_cwnd(dcp) / dcp->mss;
    if (cwnd_pkts == 0) cwnd_pkts = 1;

    if (dcp->snd_buf_len >= cwnd_pkts) {
        return;
    }
    
    DCPSEG *seg = dcp->snd_queue_head.next;
    list_del_seg(seg);
    dcp->snd_queue_len--;
    
    list_add_tail_seg(&dcp->snd_buf_head, seg);
    dcp->snd_buf_len++;
    
    seg->sn = dcp->snd_nxt++;
    seg->ts = now;
    seg->wnd = dcp->rcv_queue_len;
    seg->una = dcp->rcv_nxt;
    seg->rto = dcp->rx_rto;
    seg->xmit = 1;
    
    _dcp_output_seg(dcp, seg);
    
    if (dcp->cc_ops && dcp->cc_ops->on_pkt_sent) {
        dcp->cc_ops->on_pkt_sent(dcp, seg->len + DCP_OVERHEAD);
    }
    
    if (dcp->rto_timer_armed == 0) {
        dcp_scheduler_add(dcp->scheduler, dcp, dcp->rx_rto, dcp_on_rto_timeout);
        dcp->rto_timer_armed = 1;
    }

    if (dcp->snd_queue_head.next != &dcp->snd_queue_head) {
        uint64_t rate = dcp->cc_ops->get_pacing_rate(dcp);
        uint32_t delay_ms = 1;
        if (rate > 0) {
            delay_ms = (uint32_t)((seg->len + DCP_OVERHEAD) * 1000 / rate);
            if (delay_ms == 0) delay_ms = 1;
        }
        
        dcp_scheduler_add(dcp->scheduler, dcp, delay_ms, dcp_flush_data);
        dcp->pacing_timer_armed = 1;
    }
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

static void dcp_update_rtt(DCPCB *dcp, int32_t rtt) {
    if (dcp->rx_srtt == 0) {
        dcp->rx_srtt = rtt;
        dcp->rx_rttval = rtt / 2;
    } else {
        int32_t delta = rtt - dcp->rx_srtt;
        if (delta < 0) delta = -delta;
        dcp->rx_rttval = (3 * dcp->rx_rttval + delta) / 4;
        dcp->rx_srtt = (7 * dcp->rx_srtt + rtt) / 8;
    }
    int32_t rto = dcp->rx_srtt + (4 * dcp->rx_rttval);
    dcp->rx_rto = (rto < dcp->rx_minrto) ? dcp->rx_minrto : rto;
}

static void dcp_parse_una(DCPCB *dcp, uint32_t una) {
    DCPSEG *node = dcp->snd_buf_head.next;
    while(node != &dcp->snd_buf_head) {
        if (node->sn < una) {
            DCPSEG *to_free = node;
            node = node->next;
            list_del_seg(to_free);
            dcp_seg_free(dcp, to_free);
            dcp->snd_buf_len--;
        } else {
            break;
        }
    }
    dcp->snd_una = una;
}

static void dcp_parse_fastack(DCPCB *dcp, uint32_t sn) {
    DCPSEG *node = dcp->snd_buf_head.next;
    while(node != &dcp->snd_buf_head) {
        if (node->sn < sn) {
            node = node->next;
        } else if (node->sn == sn) {
            node->fastack++;
            break;
        } else {
            break;
        }
    }
}

static void dcp_parse_data(DCPCB *dcp, DCPSEG *newseg) {
    uint32_t sn = newseg->sn;
    
    if (sn >= dcp->rcv_nxt + dcp->rcv_wnd || sn < dcp->rcv_nxt) {
        dcp_seg_free(dcp, newseg);
        return;
    }
    
    DCPSEG *p = dcp->rcv_buf_head.prev;
    while (p != &dcp->rcv_buf_head) {
        if (p->sn == sn) {
            dcp_seg_free(dcp, newseg);
            return;
        }
        if (p->sn < sn) {
            break;
        }
        p = p->prev;
    }
    
    list_add_before(p->next, newseg);
    dcp->rcv_buf_len++;

    while (dcp->rcv_buf_head.next != &dcp->rcv_buf_head) {
        DCPSEG *seg = dcp->rcv_buf_head.next;
        if (seg->sn == dcp->rcv_nxt) {
            list_del_seg(seg);
            list_add_tail_seg(&dcp->rcv_queue_head, seg);
            dcp->rcv_buf_len--;
            dcp->rcv_queue_len++;
            dcp->rcv_nxt++;
        } else {
            break;
        }
    }
}


int dcp_input(DCPCB *dcp, const char *data, long size, uint32_t now) {
    if (dcp == NULL || dcp->is_released || data == NULL || size < (long)DCP_OVERHEAD) {
        return -1;
    }
    
    DCPSEG seg;
    const char *ptr = data;
    
    ptr = dcp_decode_seg(ptr, &seg);
    
    if (seg.conv_id != dcp->conv_id) {
        return -1;
    }
    
    if (seg.len != (uint32_t)(size - DCP_OVERHEAD)) {
        return -1;
    }
    
    dcp->rmt_wnd = seg.wnd;
    
    dcp_parse_una(dcp, seg.una);
    
    switch(seg.cmd) {
        case DCP_CMD_PUSH: {
            if (seg.sn >= dcp->rcv_nxt + dcp->rcv_wnd || seg.sn < dcp->rcv_nxt) {
                break;
            }
            
            DCPSEG *newseg = dcp_seg_create(dcp, seg.len);
            if (newseg == NULL) break;
            
            newseg->conv_id = seg.conv_id;
            newseg->cmd = seg.cmd;
            newseg->frg = seg.frg;
            newseg->wnd = seg.wnd;
            newseg->ts = seg.ts;
            newseg->sn = seg.sn;
            newseg->una = seg.una;
            newseg->len = seg.len;
            
            if (seg.len > 0) {
                memcpy(newseg->data, ptr, seg.len);
            }
            
            dcp_parse_data(dcp, newseg);
            
            if (dcp->ack_delayed_until == 0) {
                dcp_scheduler_add(dcp->scheduler, dcp, 20, dcp_on_ack_delay_timeout);
                dcp->ack_delayed_until = now + 20;
            }
            break;
        }
        case DCP_CMD_ACK: {
            if (seg.ts == 0 || now < seg.ts) break;
            
            int32_t rtt = (int32_t)(now - seg.ts);
            dcp_update_rtt(dcp, rtt);
            
            dcp_parse_fastack(dcp, seg.sn);
            
            if (dcp->cc_ops && dcp->cc_ops->on_ack) {
                dcp->cc_ops->on_ack(dcp, rtt, 0, now);
            }
            
            if (dcp->pacing_timer_armed == 0 && dcp->snd_queue_len > 0) {
                dcp_scheduler_add(dcp->scheduler, dcp, 0, dcp_flush_data);
                dcp->pacing_timer_armed = 1;
            }
            break;
        }
        case DCP_CMD_PROBE: {
            break;
        }
        default:
            break;
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
    
    if (dcp->snd_queue_len + dcp->snd_buf_len + count > dcp->snd_wnd * 2) {
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
    
    if (dcp->pacing_timer_armed == 0) {
        dcp_scheduler_add(dcp->scheduler, dcp, 0, dcp_flush_data);
        dcp->pacing_timer_armed = 1;
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
