
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

/**
 * slab主要是对齐和缓存
 *
 */

#ifndef _NGX_SLAB_H_INCLUDED_
#define _NGX_SLAB_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct ngx_slab_page_s  ngx_slab_page_t;

// slab管理页内内存，最大也就是1页，用一个uintptr_t管理一页
struct ngx_slab_page_s {
    uintptr_t         slab; // 相当于bitmap
    ngx_slab_page_t  *next;
    uintptr_t         prev;
};

// slab池，相当于slab句柄
typedef struct {
    ngx_shmtx_sh_t    lock;

    size_t            min_size;     // 8， 最小划分块大小
    size_t            min_shift;    // 3， 对应min_size

    ngx_slab_page_t  *pages;
    ngx_slab_page_t   free; // 相当于一个头结点

    u_char           *start;
    u_char           *end;

    ngx_shmtx_t       mutex;

    u_char           *log_ctx;
    u_char            zero;

    void             *data;
    void             *addr;
} ngx_slab_pool_t;


void ngx_slab_init(ngx_slab_pool_t *pool);
void *ngx_slab_alloc(ngx_slab_pool_t *pool, size_t size);
void *ngx_slab_alloc_locked(ngx_slab_pool_t *pool, size_t size);
void ngx_slab_free(ngx_slab_pool_t *pool, void *p);
void ngx_slab_free_locked(ngx_slab_pool_t *pool, void *p);


#endif /* _NGX_SLAB_H_INCLUDED_ */
