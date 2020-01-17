
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

/**
 * 一阶段一阶段的内存池，使用完一阶段就会统一销毁
 */

#ifndef _NGX_PALLOC_H_INCLUDED_
#define _NGX_PALLOC_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * NGX_MAX_ALLOC_FROM_POOL should be (ngx_pagesize - 1), i.e. 4095 on x86.
 * On Windows NT it decreases a number of locked pages in a kernel.
 */
#define NGX_MAX_ALLOC_FROM_POOL  (ngx_pagesize - 1)

#define NGX_DEFAULT_POOL_SIZE    (16 * 1024)

#define NGX_POOL_ALIGNMENT       16
#define NGX_MIN_POOL_SIZE                                                     \
    ngx_align((sizeof(ngx_pool_t) + 2 * sizeof(ngx_pool_large_t)),            \
              NGX_POOL_ALIGNMENT)


typedef void (*ngx_pool_cleanup_pt)(void *data);

typedef struct ngx_pool_cleanup_s  ngx_pool_cleanup_t;

struct ngx_pool_cleanup_s {
    ngx_pool_cleanup_pt   handler;  // 内存清理句柄，外部挂接用
    void                 *data;     // 外部传入的数据
    ngx_pool_cleanup_t   *next;
};


typedef struct ngx_pool_large_s  ngx_pool_large_t;

// 大块数据
struct ngx_pool_large_s {
    ngx_pool_large_t     *next;
    void                 *alloc;    // 存放申请过来的内存的指针
};


// 小块数据
typedef struct {
    u_char               *last; // 当前内存块已经使用的最后地址
    u_char               *end;  // 当前内存块最大可用空间的最后地址
    ngx_pool_t           *next; // 每次挂接到尾
    ngx_uint_t            failed;   // 记录申请失败次数，如果超过4次，就跳过这块内存
} ngx_pool_data_t;


// 实际的内存池接口
struct ngx_pool_s {
    ngx_pool_data_t       d;        // 第一个小块内存，同时相当于头节点
    size_t                max;      // 该内存池中每一块内存的最大大小,是该块儿
                                    // 内存的实际大小
    ngx_pool_t           *current;  // 指向当前有合适大小可用空闲的内存块,加快申请速度
    ngx_chain_t          *chain;    // 该内存池中空闲的chain链表表头，相当于小型的slab池子了
    ngx_pool_large_t     *large;    // 存放大块内存的地方，每次挂接到头
    ngx_pool_cleanup_t   *cleanup;  // 也是挂接到头
    ngx_log_t            *log;
};


typedef struct {
    ngx_fd_t              fd;
    u_char               *name;
    ngx_log_t            *log;
} ngx_pool_cleanup_file_t;


void *ngx_alloc(size_t size, ngx_log_t *log);
void *ngx_calloc(size_t size, ngx_log_t *log);

ngx_pool_t *ngx_create_pool(size_t size, ngx_log_t *log);
void ngx_destroy_pool(ngx_pool_t *pool);
void ngx_reset_pool(ngx_pool_t *pool);

// 带对齐的申请，会浪费一些空间，但能提高性能
void *ngx_palloc(ngx_pool_t *pool, size_t size);
// 不带对齐的申请，不会浪费空间，但性能会受影响
void *ngx_pnalloc(ngx_pool_t *pool, size_t size);
// 对齐申请，同时会把内存清0
void *ngx_pcalloc(ngx_pool_t *pool, size_t size);
/**
 * @brief 申请带对齐的大块内存
 *
 * @param pool
 * @param size
 * @param alignment
 *
 * @return
 */
void *ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment);
/**
 * @brief 清理掉某个大块儿内存
 *
 * @param pool
 * @param p 要清理的大内存
 *
 * @return
 */
ngx_int_t ngx_pfree(ngx_pool_t *pool, void *p);


/**
 * @brief 申请内存清理句柄
 *
 * @param p 所在内存池
 * @param size 内存清理句柄的data大小
 *
 * @return 返回所申请的内存句柄
 */
ngx_pool_cleanup_t *ngx_pool_cleanup_add(ngx_pool_t *p, size_t size);
/**
 * @brief 清理文件内存
 *
 * @param p 所在内存池
 * @param fd 要清理的文件fd
 */
void ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd);
/**
 * @brief 清理文件，其实就是关闭文件
 *
 * @param data
 */
void ngx_pool_cleanup_file(void *data);
/**
 * @brief 删除文件
 *
 * @param data
 */
void ngx_pool_delete_file(void *data);


#endif /* _NGX_PALLOC_H_INCLUDED_ */
