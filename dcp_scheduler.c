#include "dcp_scheduler.h"
#include <string.h>

static void list_add_tail(DCPTimerNode *head, DCPTimerNode *node) {
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}

static void list_del(DCPTimerNode *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

static void list_init_head(DCPTimerNode *head) {
    head->next = head;
    head->prev = head;
}


DCPScheduler* dcp_scheduler_create(void) {
    dcp_malloc_fn malloc_fn = dcp_get_malloc();
    
    DCPScheduler *scheduler = (DCPScheduler*)malloc_fn(sizeof(DCPScheduler));
    if (scheduler == NULL) return NULL;
    
    memset(scheduler, 0, sizeof(DCPScheduler));
    scheduler->alloc_fn = malloc_fn;
    scheduler->free_fn = dcp_get_free();
    scheduler->last_tick_ms = 0;
    scheduler->current_slot = 0;

    for (int i = 0; i < DCP_TIMER_WHEEL_SIZE; i++) {
        list_init_head(&scheduler->wheel[i]);
    }
    
    return scheduler;
}

void dcp_scheduler_release(DCPScheduler *scheduler) {
    if (scheduler == NULL) return;

    dcp_free_fn free_fn = scheduler->free_fn;

    for (int i = 0; i < DCP_TIMER_WHEEL_SIZE; i++) {
        DCPTimerNode *head = &scheduler->wheel[i];
        DCPTimerNode *node = head->next;
        while (node != head) {
            DCPTimerNode *to_free = node;
            node = node->next;
            free_fn(to_free);
        }
    }
    
    free_fn(scheduler);
}

void dcp_scheduler_add(DCPScheduler *scheduler, struct DCPCB *dcp, 
                       uint32_t timeout_ms, 
                       void (*callback)(struct DCPCB*, uint32_t)) {

    dcp_malloc_fn malloc_fn = scheduler->alloc_fn;

    DCPTimerNode *node = (DCPTimerNode*)malloc_fn(sizeof(DCPTimerNode));
    if (node == NULL) return;

    node->dcp = dcp;
    node->callback = callback;
    
    uint32_t now = scheduler->last_tick_ms; 
    node->expires_at_ms = now + timeout_ms;
    
    if (timeout_ms < DCP_TIMER_RESOLUTION) {
        node->expires_at_ms = now + DCP_TIMER_RESOLUTION;
    }

    uint32_t ticks_to_expire = (node->expires_at_ms / DCP_TIMER_RESOLUTION);
    uint32_t slot = ticks_to_expire % DCP_TIMER_WHEEL_SIZE;
    
    list_add_tail(&scheduler->wheel[slot], node);
}


void dcp_scheduler_run(DCPScheduler *scheduler, uint32_t current_time_ms) {
    
    uint32_t now = current_time_ms - (current_time_ms % DCP_TIMER_RESOLUTION);
    
    if (now <= scheduler->last_tick_ms) {
        return;
    }

    uint32_t ticks_to_process = (now - scheduler->last_tick_ms) / DCP_TIMER_RESOLUTION;

    if (ticks_to_process > DCP_TIMER_WHEEL_SIZE) {
        ticks_to_process = DCP_TIMER_WHEEL_SIZE;
    }

    dcp_free_fn free_fn = scheduler->free_fn;

    for (uint32_t i = 0; i < ticks_to_process; i++) {
        
        scheduler->current_slot = (scheduler->current_slot + 1) % DCP_TIMER_WHEEL_SIZE;
        uint32_t processing_time = scheduler->last_tick_ms + (i + 1) * DCP_TIMER_RESOLUTION;
        
        DCPTimerNode *head = &scheduler->wheel[scheduler->current_slot];
        DCPTimerNode *node = head->next;

        while (node != head) {
            DCPTimerNode *current = node;
            node = node->next; 

            if (processing_time >= current->expires_at_ms) {
                list_del(current);
                
                if (current->callback) {
                    current->callback(current->dcp, processing_time);
                }
                
                free_fn(current);
            }
        }
    }

    scheduler->last_tick_ms = now;
}
