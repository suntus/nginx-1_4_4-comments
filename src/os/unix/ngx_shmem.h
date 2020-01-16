
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_SHMEM_H_INCLUDED_
#define _NGX_SHMEM_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct {
    // 指向共享内存的起始地址
    u_char      *addr;
    // 共享内存长度
    size_t       size;
    // 共享内存名称
    ngx_str_t    name;
    // 记录日志的ngx_log_t对象
    ngx_log_t   *log;
    // 表示共享内存是否已经分配过的标志位
    ngx_uint_t   exists;   /* unsigned  exists:1;  */
} ngx_shm_t;


// 共享内存的实际分配
ngx_int_t ngx_shm_alloc(ngx_shm_t *shm);
// 共享内存的实际释放
void ngx_shm_free(ngx_shm_t *shm);


#endif /* _NGX_SHMEM_H_INCLUDED_ */
