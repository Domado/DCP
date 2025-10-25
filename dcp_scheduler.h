#ifndef __DCP_SCHEDULER_H__
#define __DCP_SCHEDULER_H__

#include <stdint.h>
#include "dcp_allocator.h"

#define DCP_TIMER_WHEEL_SIZE 1024
#define DCP_TIMER_RESOLUTION 10

struct DCPCB;

typedef struct DCPTimerNode {
    struct DCPTimerNode *prev;
    struct DCPTimerNode *next;
    
    struct DCPCB *dcp;
    uint32_t expires_at_ms;
    
    void (*callback)(struct DCPCB *dcp, uint32_t now);
    
} DCPTimerNode;

typedef struct DCPScheduler {
    dcp_malloc_fn alloc_fn;
    dcp_free_fn free_fn;

    uint32_t last_tick_ms;
    uint32_t current_slot;
    
    DCPTimerNode wheel[DCP_TIMER_WHEEL_SIZE];
    
} DCPScheduler;

DCPScheduler* dcp_scheduler_create(void);

void dcp_scheduler_release(DCPScheduler *scheduler);

void dcp_scheduler_run(DCPScheduler *scheduler, uint32_t current_time_ms);

void dcp_scheduler_add(DCPScheduler *scheduler, struct DCPCB *dcp, 
                       uint32_t timeout_ms, 
                       void (*callback)(struct DCPCB*, uint32_t));

#endif
