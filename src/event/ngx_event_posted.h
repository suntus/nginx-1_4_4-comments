
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_EVENT_POSTED_H_INCLUDED_
#define _NGX_EVENT_POSTED_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#if (NGX_THREADS)
extern ngx_mutex_t  *ngx_posted_events_mutex;
#endif

// 指针可以看成一个值，也可以看成一个地址。
// 解引用的时候，就看成地址，解这个地址后边一段内存的值。
// 看成值，就是这个值本身也有地址，它就是个内存值就行了。
// 变量名和变量地址，也可以看成是指针解引用和指针的关系，换个叫法，更方便操作

// 是双向链表，但prev指向的是前一个节点的next，也就是说prev的值是前一个节点的next的地址，
// 不是通常的指向前一个节点。方便删除

// 将ev添加到队列queue中，将ev加入到事件队列的首部
// ngx_event_t *ev, ngx_event_t **queue(&ngx_posted_events)
#define ngx_locked_post_event(ev, queue)                                      \
                                                                              \
    if (ev->prev == NULL) {                                                   \
        ev->next = (ngx_event_t *) *queue;                                    \
        ev->prev = (ngx_event_t **) queue;                                    \
        *queue = ev;                                                          \
                                                                              \
        if (ev->next) {                                                       \
            ev->next->prev = &ev->next;                                       \
        }                                                                     \
                                                                              \
        ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0, "post event %p", ev);  \
                                                                              \
    } else  {                                                                 \
        ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0,                        \
                       "update posted event %p", ev);                         \
    }

// 线程安全的将ev添加到队列queue中，没有使用线程的时候跟ngx_locked_poset_event一样
#define ngx_post_event(ev, queue)                                             \
                                                                              \
    ngx_mutex_lock(ngx_posted_events_mutex);                                  \
    ngx_locked_post_event(ev, queue);                                         \
    ngx_mutex_unlock(ngx_posted_events_mutex);


// 从队列中删除节点， ev->prev 指向的就是前一个节点的next，这样
// \*(ev->prev) = ev->next就是直接将前一个节点的next赋值成下一个节点，
// 接着修正一下下个节点的prev指针，这样就将ev从链表中删除了
#define ngx_delete_posted_event(ev)                                           \
                                                                              \
    *(ev->prev) = ev->next;                                                   \
                                                                              \
    if (ev->next) {                                                           \
        ev->next->prev = ev->prev;                                            \
    }                                                                         \
                                                                              \
    ev->prev = NULL;                                                          \
    ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0,                            \
                   "delete posted event %p", ev);



void ngx_event_process_posted(ngx_cycle_t *cycle,
    ngx_thread_volatile ngx_event_t **posted);
void ngx_wakeup_worker_thread(ngx_cycle_t *cycle);

#if (NGX_THREADS)
ngx_int_t ngx_event_thread_process_posted(ngx_cycle_t *cycle);
#endif


// 这两个值，就是指向队头地址，是头节点，指向事件本身
extern ngx_thread_volatile ngx_event_t  *ngx_posted_accept_events;
extern ngx_thread_volatile ngx_event_t  *ngx_posted_events;


#endif /* _NGX_EVENT_POSTED_H_INCLUDED_ */
